// render3d.cpp
// The 3D maze: meshes for walls and floors, a lighting shader with fog and
// up to 16 coloured point lights, all props (keys, gems, power-ups, doors,
// traps, teleporters, the exit beacon), the Minotaurs and the decorations
// of each world.
//
// World coordinates: every maze tile is a 1 x 1 square on the XZ plane.
// Tile (row, col) covers x = col..col+1 and z = row..row+1.

#include "app.h"

#include <algorithm>
#include <cmath>

namespace
{

// ---------------------------------------------------------------------------
// Shader
// ---------------------------------------------------------------------------

const char* const LIT_VERTEX = R"(#version 330
in vec3 vertexPosition;
in vec2 vertexTexCoord;
in vec3 vertexNormal;
in vec4 vertexColor;
uniform mat4 mvp;
uniform mat4 matModel;
uniform mat4 matNormal;
out vec3 fragPosition;
out vec2 fragTexCoord;
out vec4 fragColor;
out vec3 fragNormal;
void main()
{
    fragPosition = vec3(matModel * vec4(vertexPosition, 1.0));
    fragTexCoord = vertexTexCoord;
    fragColor = vertexColor;
    fragNormal = normalize(vec3(matNormal * vec4(vertexNormal, 0.0)));
    gl_Position = mvp * vec4(vertexPosition, 1.0);
}
)";

const char* const LIT_FRAGMENT = R"(#version 330
#define MAX_LIGHTS 16
in vec3 fragPosition;
in vec2 fragTexCoord;
in vec4 fragColor;
in vec3 fragNormal;
out vec4 finalColor;
uniform sampler2D texture0;
uniform vec4 colDiffuse;
uniform vec3 viewPos;
uniform vec3 ambient;
uniform vec3 fogColor;
uniform float fogDensity;
uniform int lightCount;
uniform vec3 lightPos[MAX_LIGHTS];
uniform vec3 lightColor[MAX_LIGHTS];
uniform float lightRadius[MAX_LIGHTS];
uniform float emissive;
uniform float texEmissive;
void main()
{
    vec4 tex = texture(texture0, fragTexCoord);
    vec3 base = tex.rgb * fragColor.rgb * colDiffuse.rgb;
    vec3 normal = normalize(fragNormal);
    vec3 light = ambient;
    for (int i = 0; i < MAX_LIGHTS; i++)
    {
        if (i >= lightCount) break;
        vec3 toLight = lightPos[i] - fragPosition;
        float dist = length(toLight);
        float attenuation = clamp(1.0 - dist / lightRadius[i], 0.0, 1.0);
        attenuation *= attenuation;
        float diffuse = max(dot(normal, toLight / max(dist, 0.001)), 0.0) * 0.75 + 0.25;
        light += lightColor[i] * attenuation * diffuse;
    }
    light = min(light, vec3(1.3));
    float glow = clamp(emissive + texEmissive * tex.a, 0.0, 1.0);
    vec3 color = base * light * (1.0 - glow) + base * glow * 1.7;
    float dist = length(viewPos - fragPosition);
    float fog = 1.0 - exp(-pow(dist * fogDensity, 2.0));
    color = mix(color, fogColor, clamp(fog, 0.0, 1.0));
    float alpha = texEmissive > 0.5 ? 1.0 : colDiffuse.a * fragColor.a;
    finalColor = vec4(color, alpha);
}
)";

const int MAX_LIGHTS = 16;

Shader litShader;
int locViewPos = -1;
int locAmbient = -1;
int locFogColor = -1;
int locFogDensity = -1;
int locLightCount = -1;
int locLightPos = -1;
int locLightColor = -1;
int locLightRadius = -1;
int locEmissive = -1;
int locTexEmissive = -1;

// ---------------------------------------------------------------------------
// Prop meshes (made once)
// ---------------------------------------------------------------------------

struct Prop
{
    Mesh mesh;
    Material material;
};

Prop cubeProp;
Prop sphereProp;
Prop gemProp;
Prop torusProp;
Prop cylinderProp;
Prop coneProp;
Prop knotProp;
Texture2D topTexture;

// World meshes (rebuilt for every maze)
bool worldLoaded = false;
Prop wallProp;
Prop wallTopProp;
Prop floorProp;
Prop groundProp;

struct Decor
{
    Vector3 position;
    Vector3 wallDirection; // points from the cell into the wall it hangs on
    float seed;
};
std::vector<Decor> decorations;
ThemeId worldTheme = ThemeId::Neon;

struct LightCandidate
{
    Vector3 position;
    Color color;
    float radius;
    float distance;
};

Prop makeProp(Mesh mesh)
{
    Prop prop;
    prop.mesh = mesh;
    if (prop.mesh.vaoId == 0 && prop.mesh.vertexCount > 0)
    {
        UploadMesh(&prop.mesh, false); // GenMesh...() meshes are already on the GPU
    }
    prop.material = LoadMaterialDefault();
    prop.material.shader = litShader;
    return prop;
}

void unloadProp(Prop& prop)
{
    UnloadMesh(prop.mesh);
    // The shader and textures are shared, so only the material's own map list is freed.
    MemFree(prop.material.maps);
    prop.material.maps = nullptr;
}

// Builds non-indexed meshes from quads.
struct MeshBuilder
{
    std::vector<float> vertices;
    std::vector<float> texcoords;
    std::vector<float> normals;
    std::vector<unsigned char> colors;

    void vertex(Vector3 p, Vector2 uv, Vector3 n, Color c)
    {
        vertices.insert(vertices.end(), {p.x, p.y, p.z});
        texcoords.insert(texcoords.end(), {uv.x, uv.y});
        normals.insert(normals.end(), {n.x, n.y, n.z});
        colors.insert(colors.end(), {c.r, c.g, c.b, c.a});
    }

    // A rectangle facing `normal`. `up` is the face's vertical direction.
    void face(Vector3 center, Vector3 normal, Vector3 up, float halfWidth, float halfHeight, Color color,
              float uScale = 1.0f, float vScale = 1.0f)
    {
        const Vector3 right = Vector3CrossProduct(Vector3Negate(normal), up);
        const Vector3 r = Vector3Scale(right, halfWidth);
        const Vector3 u = Vector3Scale(up, halfHeight);
        const Vector3 bl = Vector3Subtract(Vector3Subtract(center, r), u);
        const Vector3 br = Vector3Subtract(Vector3Add(center, r), u);
        const Vector3 tr = Vector3Add(Vector3Add(center, r), u);
        const Vector3 tl = Vector3Add(Vector3Subtract(center, r), u);
        vertex(bl, Vector2{0, vScale}, normal, color);
        vertex(br, Vector2{uScale, vScale}, normal, color);
        vertex(tr, Vector2{uScale, 0}, normal, color);
        vertex(bl, Vector2{0, vScale}, normal, color);
        vertex(tr, Vector2{uScale, 0}, normal, color);
        vertex(tl, Vector2{0, 0}, normal, color);
    }

    Mesh build() const
    {
        Mesh mesh{};
        mesh.vertexCount = static_cast<int>(vertices.size() / 3);
        mesh.triangleCount = mesh.vertexCount / 3;
        if (mesh.vertexCount == 0)
        {
            return mesh;
        }
        mesh.vertices = static_cast<float*>(MemAlloc(static_cast<unsigned int>(vertices.size() * sizeof(float))));
        mesh.texcoords = static_cast<float*>(MemAlloc(static_cast<unsigned int>(texcoords.size() * sizeof(float))));
        mesh.normals = static_cast<float*>(MemAlloc(static_cast<unsigned int>(normals.size() * sizeof(float))));
        mesh.colors = static_cast<unsigned char*>(MemAlloc(static_cast<unsigned int>(colors.size())));
        std::copy(vertices.begin(), vertices.end(), mesh.vertices);
        std::copy(texcoords.begin(), texcoords.end(), mesh.texcoords);
        std::copy(normals.begin(), normals.end(), mesh.normals);
        std::copy(colors.begin(), colors.end(), mesh.colors);
        return mesh;
    }
};

// ---------------------------------------------------------------------------
// Drawing helpers
// ---------------------------------------------------------------------------

Matrix trs(Vector3 position, Vector3 rotation, Vector3 scale)
{
    return MatrixMultiply(MatrixMultiply(MatrixScale(scale.x, scale.y, scale.z),
                                         MatrixRotateXYZ(rotation)),
                          MatrixTranslate(position.x, position.y, position.z));
}

void setEmissive(float emissive, float texEmissive = 0.0f)
{
    SetShaderValue(litShader, locEmissive, &emissive, SHADER_UNIFORM_FLOAT);
    SetShaderValue(litShader, locTexEmissive, &texEmissive, SHADER_UNIFORM_FLOAT);
}

void drawProp(Prop& prop, const Matrix& transform, Color color, float emissive)
{
    setEmissive(emissive);
    prop.material.maps[MATERIAL_MAP_DIFFUSE].color = color;
    DrawMesh(prop.mesh, prop.material, transform);
}

// Draws a part of an object: `local` is relative to the object's own transform.
void drawPart(Prop& prop, const Matrix& object, Vector3 position, Vector3 rotation, Vector3 scale, Color color,
              float emissive)
{
    drawProp(prop, MatrixMultiply(trs(position, rotation, scale), object), color, emissive);
}

float hashFloat(int a, int b)
{
    unsigned int h = static_cast<unsigned int>(a) * 73856093u ^ static_cast<unsigned int>(b) * 19349663u;
    h = (h ^ (h >> 13)) * 1274126177u;
    return static_cast<float>(h & 0xFFFF) / 65535.0f;
}

Vector3 tileCenter(int tileRow, int tileCol)
{
    return Vector3{static_cast<float>(tileCol) + 0.5f, 0.0f, static_cast<float>(tileRow) + 0.5f};
}

Vector3 directionVector(Direction direction)
{
    int rowOffset = 0;
    int colOffset = 0;
    getDirectionOffset(direction, rowOffset, colOffset);
    return Vector3{static_cast<float>(colOffset), 0.0f, static_cast<float>(rowOffset)};
}

// ---------------------------------------------------------------------------
// Building the world
// ---------------------------------------------------------------------------

void buildDecorations(const GameState& state)
{
    decorations.clear();
    const Direction sides[4] = {Direction::Up, Direction::Right, Direction::Down, Direction::Left};
    for (int row = 0; row < state.maze.size; ++row)
    {
        for (int col = 0; col < state.maze.size; ++col)
        {
            if (hashFloat(row * 3 + 1, col * 5 + 2) > 0.16f || (row == 0 && col == 0))
            {
                continue;
            }
            for (const Direction side : sides)
            {
                int rowOffset = 0;
                int colOffset = 0;
                getDirectionOffset(side, rowOffset, colOffset);
                const Tile wall = state.maze.tiles[cellToTile(row) + rowOffset][cellToTile(col) + colOffset];
                if (wall == Tile::Wall)
                {
                    Decor decor;
                    decor.wallDirection = directionVector(side);
                    decor.position = Vector3Add(cellCenter(row, col), Vector3Scale(decor.wallDirection, 0.38f));
                    decor.seed = hashFloat(row, col);
                    decorations.push_back(decor);
                    break;
                }
            }
        }
    }
}

// A six-sided double pyramid with sharp facets: a cut gem.
Mesh makeGemMesh()
{
    MeshBuilder builder;
    const int sides = 6;
    const Vector3 top{0, 0.5f, 0};
    const Vector3 bottom{0, -0.5f, 0};
    for (int i = 0; i < sides; ++i)
    {
        const float a0 = 2.0f * PI * static_cast<float>(i) / sides;
        const float a1 = 2.0f * PI * static_cast<float>(i + 1) / sides;
        const Vector3 p0{std::cos(a0) * 0.5f, 0.0f, std::sin(a0) * 0.5f};
        const Vector3 p1{std::cos(a1) * 0.5f, 0.0f, std::sin(a1) * 0.5f};
        const Vector3 upperNormal = Vector3Normalize(Vector3CrossProduct(Vector3Subtract(p1, top), Vector3Subtract(p0, top)));
        const Vector3 lowerNormal = Vector3Normalize(Vector3CrossProduct(Vector3Subtract(p0, bottom), Vector3Subtract(p1, bottom)));
        const Color light = i % 2 == 0 ? WHITE : Color{210, 210, 230, 255};
        builder.vertex(top, Vector2{0.5f, 0}, upperNormal, light);
        builder.vertex(p1, Vector2{1, 1}, upperNormal, light);
        builder.vertex(p0, Vector2{0, 1}, upperNormal, light);
        builder.vertex(bottom, Vector2{0.5f, 1}, lowerNormal, Color{170, 170, 200, 255});
        builder.vertex(p0, Vector2{0, 0}, lowerNormal, Color{170, 170, 200, 255});
        builder.vertex(p1, Vector2{1, 0}, lowerNormal, Color{170, 170, 200, 255});
    }
    return builder.build();
}

Texture2D makeTopTexture()
{
    const int size = 64;
    Image image = GenImageColor(size, size, WHITE);
    Color* pixels = static_cast<Color*>(image.data);
    for (int y = 0; y < size; ++y)
    {
        for (int x = 0; x < size; ++x)
        {
            const int edge = std::min(std::min(x, size - 1 - x), std::min(y, size - 1 - y));
            const float shade = edge < 3 ? 0.55f : (edge < 6 ? 1.0f : 0.82f);
            const unsigned char v = static_cast<unsigned char>(255.0f * shade);
            pixels[y * size + x] = Color{v, v, v, 255};
        }
    }
    Texture2D texture = LoadTextureFromImage(image);
    UnloadImage(image);
    GenTextureMipmaps(&texture);
    SetTextureFilter(texture, TEXTURE_FILTER_TRILINEAR);
    return texture;
}

// ---------------------------------------------------------------------------
// Props
// ---------------------------------------------------------------------------

// A soft glowing disc on the floor so items stand out, especially from above.
void drawFloorGlow(Vector3 position, Color color, float time)
{
    const float pulse = 0.85f + 0.15f * std::sin(time * 3.0f + position.x + position.z);
    drawProp(cylinderProp, trs(Vector3{position.x, 0.006f, position.z}, Vector3{0, 0, 0}, Vector3{0.75f * pulse, 0.01f, 0.75f * pulse}),
             withAlpha(color, 0.35f), 1.0f);
}

void drawKey(Vector3 position, Color color, float time)
{
    drawFloorGlow(position, color, time);
    const float bob = 0.45f + 0.08f * std::sin(time * 2.5f + position.x);
    const Matrix object = MatrixMultiply(MatrixRotateY(time * 1.8f), MatrixTranslate(position.x, bob, position.z));
    drawPart(torusProp, object, Vector3{0, 0.12f, 0}, Vector3{0, 0, 0}, Vector3{0.16f, 0.16f, 0.16f}, color, 0.55f);
    drawPart(cubeProp, object, Vector3{0, -0.08f, 0}, Vector3{0, 0, 0}, Vector3{0.04f, 0.3f, 0.04f}, color, 0.55f);
    drawPart(cubeProp, object, Vector3{0.05f, -0.18f, 0}, Vector3{0, 0, 0}, Vector3{0.08f, 0.04f, 0.04f}, color, 0.55f);
    drawPart(cubeProp, object, Vector3{0.05f, -0.1f, 0}, Vector3{0, 0, 0}, Vector3{0.06f, 0.03f, 0.04f}, color, 0.55f);
}

void drawGem(Vector3 position, Color color, float time)
{
    drawFloorGlow(position, color, time);
    const float bob = 0.42f + 0.07f * std::sin(time * 2.0f + position.z);
    const Matrix object = MatrixMultiply(MatrixRotateY(time * 1.3f + position.x), MatrixTranslate(position.x, bob, position.z));
    drawPart(gemProp, object, Vector3{0, 0, 0}, Vector3{0, 0, 0}, Vector3{0.22f, 0.34f, 0.22f}, color, 0.65f);
}

void drawPowerUp(Item item, Vector3 position, float time)
{
    const float bob = 0.45f + 0.07f * std::sin(time * 2.2f + position.x * 0.7f);
    const Matrix object = MatrixMultiply(MatrixMultiply(MatrixScale(1.3f, 1.3f, 1.3f), MatrixRotateY(time * 1.5f)),
                                         MatrixTranslate(position.x, bob, position.z));
    const Color color = itemColor(item);
    drawFloorGlow(position, color, time);
    // A glowing ring around every power-up so they are easy to spot.
    drawPart(torusProp, object, Vector3{0, -0.3f, 0}, Vector3{PI / 2, 0, 0}, Vector3{0.28f, 0.28f, 0.28f},
             withAlpha(color, 0.9f), 0.8f);
    switch (item)
    {
    case Item::Torch:
    {
        const float flicker = 1.0f + 0.15f * std::sin(time * 17.0f) + 0.1f * std::sin(time * 23.0f);
        drawPart(cylinderProp, object, Vector3{0, -0.2f, 0}, Vector3{0, 0, 0}, Vector3{0.06f, 0.3f, 0.06f},
                 Color{120, 80, 40, 255}, 0.1f);
        drawPart(sphereProp, object, Vector3{0, 0.16f, 0}, Vector3{0, 0, 0},
                 Vector3{0.16f * flicker, 0.24f * flicker, 0.16f * flicker}, Color{255, 170, 40, 255}, 1.0f);
        break;
    }
    case Item::Boots:
        drawPart(cubeProp, object, Vector3{0, 0.02f, 0}, Vector3{0, 0, 0}, Vector3{0.12f, 0.28f, 0.14f}, color, 0.5f);
        drawPart(cubeProp, object, Vector3{0.07f, -0.1f, 0}, Vector3{0, 0, 0}, Vector3{0.26f, 0.08f, 0.14f}, color, 0.5f);
        drawPart(cubeProp, object, Vector3{-0.12f, 0.08f, 0}, Vector3{0, 0, 0.6f}, Vector3{0.16f, 0.04f, 0.1f},
                 Color{255, 230, 120, 255}, 0.9f);
        break;
    case Item::Hammer:
        drawPart(cylinderProp, object, Vector3{0, -0.22f, 0}, Vector3{0, 0, 0}, Vector3{0.05f, 0.42f, 0.05f},
                 Color{150, 100, 60, 255}, 0.2f);
        drawPart(cubeProp, object, Vector3{0, 0.2f, 0}, Vector3{0, 0, 0}, Vector3{0.3f, 0.12f, 0.12f}, color, 0.5f);
        break;
    case Item::Hourglass:
        drawPart(coneProp, object, Vector3{0, 0.0f, 0}, Vector3{0, 0, 0}, Vector3{0.3f, 0.18f, 0.3f}, color, 0.6f);
        drawPart(coneProp, object, Vector3{0, 0.0f, 0}, Vector3{PI, 0, 0}, Vector3{0.3f, 0.18f, 0.3f}, color, 0.6f);
        drawPart(cylinderProp, object, Vector3{0, 0.18f, 0}, Vector3{0, 0, 0}, Vector3{0.34f, 0.03f, 0.34f},
                 Color{200, 160, 90, 255}, 0.3f);
        drawPart(cylinderProp, object, Vector3{0, -0.21f, 0}, Vector3{0, 0, 0}, Vector3{0.34f, 0.03f, 0.34f},
                 Color{200, 160, 90, 255}, 0.3f);
        break;
    case Item::Heart:
    {
        const float beat = 1.0f + 0.12f * std::fmax(0.0f, std::sin(time * 6.0f));
        drawPart(sphereProp, object, Vector3{-0.08f, 0.06f, 0}, Vector3{0, 0, 0},
                 Vector3{0.2f * beat, 0.2f * beat, 0.16f * beat}, color, 0.7f);
        drawPart(sphereProp, object, Vector3{0.08f, 0.06f, 0}, Vector3{0, 0, 0},
                 Vector3{0.2f * beat, 0.2f * beat, 0.16f * beat}, color, 0.7f);
        drawPart(coneProp, object, Vector3{0, 0.04f, 0}, Vector3{PI, 0, 0}, Vector3{0.34f * beat, 0.24f, 0.2f}, color,
                 0.7f);
        break;
    }
    case Item::Compass:
        drawPart(torusProp, object, Vector3{0, 0, 0}, Vector3{0, 0, 0}, Vector3{0.2f, 0.2f, 0.2f}, color, 0.6f);
        drawPart(coneProp, object, Vector3{0, 0, 0}, Vector3{0, 0, 0}, Vector3{0.06f, 0.16f, 0.03f},
                 Color{255, 60, 60, 255}, 0.9f);
        drawPart(coneProp, object, Vector3{0, 0, 0}, Vector3{PI, 0, 0}, Vector3{0.06f, 0.16f, 0.03f}, WHITE, 0.9f);
        break;
    default:
        break;
    }
}

void drawSpikes(const GameState& state, int row, int col, Vector3 position, bool fromAbove, float time)
{
    const float lift = getSpikeLift(state, row, col);
    const Matrix object = MatrixTranslate(position.x, 0.0f, position.z);
    // A red hazard plate, visible even when the spikes are down.
    drawPart(cylinderProp, object, Vector3{0, 0.0f, 0}, Vector3{0, 0, 0}, Vector3{0.86f, 0.03f, 0.86f},
             Color{120, 26, 30, 255}, 0.35f);
    if (fromAbove)
    {
        // From high above the cones are tiny, so add a bold warning ring.
        const float pulse = lift > 0.5f ? 1.0f : 0.55f + 0.2f * std::sin(time * 4.0f);
        drawPart(torusProp, object, Vector3{0, 0.06f, 0}, Vector3{PI / 2, 0, 0}, Vector3{0.42f, 0.42f, 0.42f},
                 Color{255, 50, 40, 255}, pulse);
        drawPart(cylinderProp, object, Vector3{0, 0.035f, 0}, Vector3{0, 0, 0}, Vector3{0.5f, 0.01f, 0.5f},
                 Color{255, 200, 60, 255}, 0.4f * pulse);
    }
    const Color tip = lift > 0.5f ? Color{230, 230, 240, 255} : Color{140, 140, 150, 255};
    const float height = 0.08f + 0.5f * lift;
    for (int i = 0; i < 5; ++i)
    {
        const float angle = static_cast<float>(i) * 2.0f * PI / 5.0f;
        const float radius = i == 0 ? 0.0f : 0.22f;
        const Vector3 at{std::cos(angle) * radius, 0.0f, std::sin(angle) * radius};
        drawPart(coneProp, object, at, Vector3{0, 0, 0}, Vector3{0.12f, height, 0.12f}, tip, lift > 0.5f ? 0.3f : 0.0f);
    }
    if (lift > 0.5f)
    {
        drawPart(cylinderProp, object, Vector3{0, 0.01f, 0}, Vector3{0, 0, 0}, Vector3{0.9f, 0.01f, 0.9f},
                 Color{255, 40, 40, 140}, 1.0f);
    }
}

void drawTeleporter(Vector3 position, Color color, float time)
{
    const Matrix base = MatrixTranslate(position.x, 0.0f, position.z);
    drawPart(cylinderProp, base, Vector3{0, 0.0f, 0}, Vector3{0, 0, 0}, Vector3{0.8f, 0.04f, 0.8f},
             Color{40, 30, 60, 255}, 0.1f);
    for (int ring = 0; ring < 3; ++ring)
    {
        const float height = std::fmod(time * 0.6f + static_cast<float>(ring) / 3.0f, 1.0f);
        const float size = 0.34f * (1.0f - height * 0.4f);
        drawPart(torusProp, base, Vector3{0, 0.05f + height * 1.2f, 0}, Vector3{PI / 2, 0, 0},
                 Vector3{size, size, size}, withAlpha(color, 1.0f - height), 1.0f);
    }
    const Matrix spin = MatrixMultiply(MatrixRotateY(time * 2.0f), MatrixTranslate(position.x, 0.55f, position.z));
    drawPart(knotProp, spin, Vector3{0, 0, 0}, Vector3{0, 0, 0}, Vector3{0.1f, 0.1f, 0.1f}, color, 0.9f);
}

void drawDoor(int tileRow, int tileCol, Color color, float progress, float time)
{
    // A door between left/right cells sits in an even column; it blocks movement along x.
    const bool blocksX = (tileCol % 2 == 0);
    const Vector3 center = tileCenter(tileRow, tileCol);
    const float sink = progress * WALL_HEIGHT;
    const float alpha = 1.0f - progress;
    const float pulse = 0.6f + 0.4f * std::sin(time * 4.0f);
    const Vector3 slabScale = blocksX ? Vector3{0.12f, WALL_HEIGHT, 1.0f} : Vector3{1.0f, WALL_HEIGHT, 0.12f};
    const Matrix object = MatrixTranslate(center.x, WALL_HEIGHT * 0.5f - sink, center.z);
    drawPart(cubeProp, object, Vector3{0, 0, 0}, Vector3{0, 0, 0}, slabScale, withAlpha(color, 0.55f * alpha),
             0.5f + 0.4f * pulse);
    // Energy bars
    for (int bar = -2; bar <= 2; ++bar)
    {
        const float offset = static_cast<float>(bar) * 0.18f;
        const Vector3 at = blocksX ? Vector3{0, 0, offset} : Vector3{offset, 0, 0};
        drawPart(cubeProp, object, at, Vector3{0, 0, 0}, Vector3{0.05f, WALL_HEIGHT * 0.96f, 0.05f},
                 withAlpha(WHITE, alpha), 0.9f);
    }
    // Keyhole emblem
    const Vector3 emblem = blocksX ? Vector3{0.08f, 0.1f, 0} : Vector3{0, 0.1f, 0.08f};
    drawPart(sphereProp, object, emblem, Vector3{0, 0, 0}, Vector3{0.16f, 0.16f, 0.16f},
             withAlpha(Color{255, 220, 80, 255}, alpha), 1.0f);
    drawPart(sphereProp, object, Vector3Negate(emblem), Vector3{0, 0, 0}, Vector3{0.16f, 0.16f, 0.16f},
             withAlpha(Color{255, 220, 80, 255}, alpha), 1.0f);
}

void drawExitBeacon(Vector3 position, Color color, float time, bool unlocked)
{
    const Color beam = unlocked ? color : Color{255, 60, 60, 255};
    const Matrix base = MatrixTranslate(position.x, 0.0f, position.z);
    drawPart(cylinderProp, base, Vector3{0, 0.0f, 0}, Vector3{0, 0, 0}, Vector3{0.9f, 0.06f, 0.9f}, beam, 0.9f);
    drawPart(cylinderProp, base, Vector3{0, 0.0f, 0}, Vector3{0, 0, 0}, Vector3{0.55f, 14.0f, 0.55f},
             withAlpha(beam, 0.22f), 1.0f);
    drawPart(cylinderProp, base, Vector3{0, 0.0f, 0}, Vector3{0, 0, 0}, Vector3{0.22f, 14.0f, 0.22f},
             withAlpha(WHITE, 0.35f), 1.0f);
    for (int ring = 0; ring < 3; ++ring)
    {
        const float height = 0.4f + static_cast<float>(ring) * 0.45f + 0.1f * std::sin(time * 2.0f + ring);
        const Matrix spin = MatrixMultiply(MatrixRotateY(time * (1.0f + ring * 0.5f)),
                                           MatrixTranslate(position.x, height, position.z));
        drawPart(torusProp, spin, Vector3{0, 0, 0}, Vector3{PI / 2 + 0.3f * std::sin(time + ring), 0, 0},
                 Vector3{0.36f, 0.36f, 0.36f}, beam, 1.0f);
    }
}

void drawMinotaur(Vector3 position, float yaw, float walk, bool frozen, bool hunting, float time)
{
    // 0.9 scale keeps arms and horns inside the 1-unit-wide corridors.
    const float modelScale = 0.9f;
    const Color fur = frozen ? Color{150, 200, 240, 255} : Color{110, 60, 40, 255};
    const Color dark = frozen ? Color{110, 160, 210, 255} : Color{70, 38, 26, 255};
    const Color horn = Color{240, 230, 200, 255};
    const Color eyes = frozen ? Color{120, 220, 255, 255} : (hunting ? Color{255, 30, 20, 255} : Color{255, 160, 40, 255});
    const float swing = frozen ? 0.0f : std::sin(walk * 9.0f) * 0.5f;
    const float breathe = frozen ? 0.0f : 0.02f * std::sin(time * 3.0f);
    const Matrix object = MatrixMultiply(MatrixMultiply(MatrixScale(modelScale, modelScale, modelScale), MatrixRotateY(yaw)),
                                         MatrixTranslate(position.x, 0.0f, position.z));

    // Legs
    drawPart(cubeProp, object, Vector3{-0.13f, 0.28f, 0}, Vector3{swing, 0, 0}, Vector3{0.14f, 0.5f, 0.14f}, dark, 0.0f);
    drawPart(cubeProp, object, Vector3{0.13f, 0.28f, 0}, Vector3{-swing, 0, 0}, Vector3{0.14f, 0.5f, 0.14f}, dark, 0.0f);
    // Body and chest
    drawPart(cubeProp, object, Vector3{0, 0.78f + breathe, 0}, Vector3{0, 0, 0}, Vector3{0.5f, 0.55f, 0.32f}, fur, 0.0f);
    drawPart(cubeProp, object, Vector3{0, 0.58f, 0}, Vector3{0, 0, 0}, Vector3{0.42f, 0.12f, 0.3f},
             Color{60, 40, 30, 255}, 0.0f);
    // Arms
    drawPart(cubeProp, object, Vector3{-0.33f, 0.8f, 0}, Vector3{-swing * 0.8f, 0, 0.15f}, Vector3{0.13f, 0.5f, 0.13f},
             fur, 0.0f);
    drawPart(cubeProp, object, Vector3{0.33f, 0.8f, 0}, Vector3{swing * 0.8f, 0, -0.15f}, Vector3{0.13f, 0.5f, 0.13f},
             fur, 0.0f);
    // Head, snout, horns, eyes
    drawPart(cubeProp, object, Vector3{0, 1.18f + breathe, 0.04f}, Vector3{0, 0, 0}, Vector3{0.32f, 0.3f, 0.3f}, fur, 0.0f);
    drawPart(cubeProp, object, Vector3{0, 1.1f + breathe, 0.2f}, Vector3{0, 0, 0}, Vector3{0.2f, 0.14f, 0.12f}, dark, 0.0f);
    drawPart(coneProp, object, Vector3{-0.17f, 1.28f, 0.02f}, Vector3{0, 0, 1.2f}, Vector3{0.08f, 0.3f, 0.08f}, horn, 0.2f);
    drawPart(coneProp, object, Vector3{0.17f, 1.28f, 0.02f}, Vector3{0, 0, -1.2f}, Vector3{0.08f, 0.3f, 0.08f}, horn, 0.2f);
    drawPart(sphereProp, object, Vector3{-0.08f, 1.22f + breathe, 0.19f}, Vector3{0, 0, 0}, Vector3{0.06f, 0.05f, 0.03f},
             eyes, 1.0f);
    drawPart(sphereProp, object, Vector3{0.08f, 1.22f + breathe, 0.19f}, Vector3{0, 0, 0}, Vector3{0.06f, 0.05f, 0.03f},
             eyes, 1.0f);
    // Golden nose ring
    drawPart(torusProp, object, Vector3{0, 1.04f + breathe, 0.27f}, Vector3{0, 0, 0}, Vector3{0.05f, 0.05f, 0.05f},
             Color{255, 210, 60, 255}, 0.8f);
}

void drawPlayerAvatar(Vector3 position, float yaw, Color color, float time, bool blink)
{
    if (blink)
    {
        return;
    }
    const Matrix object = MatrixMultiply(MatrixRotateY(yaw), MatrixTranslate(position.x, 0.0f, position.z));
    drawPart(sphereProp, object, Vector3{0, 0.55f + 0.05f * std::sin(time * 3.0f), 0}, Vector3{0, 0, 0},
             Vector3{0.36f, 0.36f, 0.36f}, color, 0.9f);
    drawPart(torusProp, object, Vector3{0, 0.55f, 0}, Vector3{PI / 2 + 0.4f * std::sin(time * 2.0f), time * 2.0f, 0},
             Vector3{0.3f, 0.3f, 0.3f}, WHITE, 0.9f);
    drawPart(coneProp, object, Vector3{0, 0.08f, 0.34f}, Vector3{PI / 2, 0, 0}, Vector3{0.2f, 0.26f, 0.1f}, color, 0.9f);
}

void drawDecoration(const Decor& decor, ThemeId theme, float time)
{
    const float yaw = std::atan2(decor.wallDirection.x, decor.wallDirection.z);
    const Matrix object = MatrixMultiply(MatrixRotateY(yaw), MatrixTranslate(decor.position.x, 0.0f, decor.position.z));
    const float flicker = 1.0f + 0.18f * std::sin(time * 13.0f + decor.seed * 40.0f) + 0.1f * std::sin(time * 29.0f);
    switch (theme)
    {
    case ThemeId::Neon:
    {
        const Color color = decor.seed > 0.5f ? Color{255, 50, 210, 255} : Color{40, 230, 255, 255};
        drawPart(torusProp, object, Vector3{0, 1.0f, 0.05f}, Vector3{0, 0, time * 0.8f + decor.seed * 6.0f},
                 Vector3{0.2f, 0.2f, 0.2f}, color, 1.0f);
        drawPart(cubeProp, object, Vector3{0, 0.6f, 0.08f}, Vector3{0, 0, 0}, Vector3{0.04f, 1.2f, 0.04f}, color, 1.0f);
        break;
    }
    case ThemeId::Dungeon:
        drawPart(cubeProp, object, Vector3{0, 0.95f, 0.04f}, Vector3{-0.5f, 0, 0}, Vector3{0.06f, 0.3f, 0.06f},
                 Color{90, 60, 35, 255}, 0.0f);
        drawPart(sphereProp, object, Vector3{0, 1.16f, -0.02f}, Vector3{0, 0, 0},
                 Vector3{0.13f * flicker, 0.2f * flicker, 0.13f * flicker}, Color{255, 150, 40, 255}, 1.0f);
        break;
    case ThemeId::Lava:
        drawPart(cylinderProp, object, Vector3{0, 0.0f, -0.1f}, Vector3{0, 0, 0}, Vector3{0.3f, 0.35f, 0.3f},
                 Color{50, 36, 34, 255}, 0.0f);
        drawPart(sphereProp, object, Vector3{0, 0.45f, -0.1f}, Vector3{0, 0, 0},
                 Vector3{0.24f * flicker, 0.3f * flicker, 0.24f * flicker}, Color{255, 110, 20, 255}, 1.0f);
        break;
    case ThemeId::Ice:
        for (int i = 0; i < 3; ++i)
        {
            const float angle = static_cast<float>(i) * 0.5f - 0.5f;
            drawPart(coneProp, object, Vector3{static_cast<float>(i - 1) * 0.12f, 0.0f, -0.05f}, Vector3{0, 0, angle},
                     Vector3{0.12f, 0.45f + 0.15f * static_cast<float>(i % 2), 0.12f}, Color{150, 220, 255, 230}, 0.7f);
        }
        break;
    case ThemeId::Candy:
        drawPart(cylinderProp, object, Vector3{0, 0.0f, -0.05f}, Vector3{0, 0, 0}, Vector3{0.04f, 0.8f, 0.04f}, WHITE,
                 0.3f);
        drawPart(torusProp, object, Vector3{0, 0.9f, -0.05f}, Vector3{0, 0, time + decor.seed * 5.0f},
                 Vector3{0.18f, 0.18f, 0.18f}, decor.seed > 0.5f ? Color{255, 80, 160, 255} : Color{120, 220, 255, 255},
                 0.6f);
        drawPart(sphereProp, object, Vector3{0, 0.9f, -0.05f}, Vector3{0, 0, 0}, Vector3{0.16f, 0.16f, 0.08f},
                 Color{255, 240, 120, 255}, 0.6f);
        break;
    }
}

Color decorLight(ThemeId theme, float seed)
{
    switch (theme)
    {
    case ThemeId::Neon:
        return seed > 0.5f ? Color{255, 50, 210, 255} : Color{40, 230, 255, 255};
    case ThemeId::Dungeon:
        return Color{255, 140, 50, 255};
    case ThemeId::Lava:
        return Color{255, 100, 20, 255};
    case ThemeId::Ice:
        return Color{120, 200, 255, 255};
    case ThemeId::Candy:
        return seed > 0.5f ? Color{255, 120, 200, 255} : Color{140, 220, 255, 255};
    }
    return WHITE;
}

void setLights(const Theme& theme, const std::vector<LightCandidate>& lights)
{
    float positions[MAX_LIGHTS * 3] = {};
    float colors[MAX_LIGHTS * 3] = {};
    float radii[MAX_LIGHTS] = {};
    const int count = static_cast<int>(std::min<std::size_t>(lights.size(), MAX_LIGHTS));
    for (int i = 0; i < count; ++i)
    {
        positions[i * 3 + 0] = lights[i].position.x;
        positions[i * 3 + 1] = lights[i].position.y;
        positions[i * 3 + 2] = lights[i].position.z;
        colors[i * 3 + 0] = lights[i].color.r / 255.0f;
        colors[i * 3 + 1] = lights[i].color.g / 255.0f;
        colors[i * 3 + 2] = lights[i].color.b / 255.0f;
        radii[i] = lights[i].radius;
    }
    SetShaderValue(litShader, locLightCount, &count, SHADER_UNIFORM_INT);
    SetShaderValueV(litShader, locLightPos, positions, SHADER_UNIFORM_VEC3, MAX_LIGHTS);
    SetShaderValueV(litShader, locLightColor, colors, SHADER_UNIFORM_VEC3, MAX_LIGHTS);
    SetShaderValueV(litShader, locLightRadius, radii, SHADER_UNIFORM_FLOAT, MAX_LIGHTS);
    (void)theme;
}

} // namespace

// ---------------------------------------------------------------------------
// Public functions
// ---------------------------------------------------------------------------

Vector3 cellCenter(int row, int col)
{
    return tileCenter(cellToTile(row), cellToTile(col));
}

void initRenderer()
{
    litShader = LoadShaderFromMemory(LIT_VERTEX, LIT_FRAGMENT);
    litShader.locs[SHADER_LOC_MATRIX_MODEL] = GetShaderLocation(litShader, "matModel");
    litShader.locs[SHADER_LOC_MATRIX_NORMAL] = GetShaderLocation(litShader, "matNormal");
    litShader.locs[SHADER_LOC_VECTOR_VIEW] = GetShaderLocation(litShader, "viewPos");
    locViewPos = litShader.locs[SHADER_LOC_VECTOR_VIEW];
    locAmbient = GetShaderLocation(litShader, "ambient");
    locFogColor = GetShaderLocation(litShader, "fogColor");
    locFogDensity = GetShaderLocation(litShader, "fogDensity");
    locLightCount = GetShaderLocation(litShader, "lightCount");
    locLightPos = GetShaderLocation(litShader, "lightPos");
    locLightColor = GetShaderLocation(litShader, "lightColor");
    locLightRadius = GetShaderLocation(litShader, "lightRadius");
    locEmissive = GetShaderLocation(litShader, "emissive");
    locTexEmissive = GetShaderLocation(litShader, "texEmissive");

    cubeProp = makeProp(GenMeshCube(1.0f, 1.0f, 1.0f));
    sphereProp = makeProp(GenMeshSphere(0.5f, 12, 16));
    gemProp = makeProp(makeGemMesh());
    torusProp = makeProp(GenMeshTorus(0.25f, 1.0f, 10, 24));
    cylinderProp = makeProp(GenMeshCylinder(0.5f, 1.0f, 16));
    coneProp = makeProp(GenMeshCone(0.5f, 1.0f, 12));
    knotProp = makeProp(GenMeshKnot(1.0f, 2.0f, 16, 64));
    topTexture = makeTopTexture();
}

void unloadRenderer()
{
    unloadWorld();
    unloadProp(cubeProp);
    unloadProp(sphereProp);
    unloadProp(gemProp);
    unloadProp(torusProp);
    unloadProp(cylinderProp);
    unloadProp(coneProp);
    unloadProp(knotProp);
    UnloadTexture(topTexture);
    UnloadShader(litShader);
}

void unloadWorld()
{
    if (!worldLoaded)
    {
        return;
    }
    unloadProp(wallProp);
    unloadProp(wallTopProp);
    unloadProp(floorProp);
    unloadProp(groundProp);
    worldLoaded = false;
}

void buildWorld(const GameState& state, ThemeId themeId)
{
    unloadWorld();
    const Theme& theme = getThemeData(themeId);
    const Maze& maze = state.maze;
    const int n = maze.tileCount;
    const Vector3 up{0, 1, 0};
    const Vector3 north{0, 0, -1};
    MeshBuilder walls;
    MeshBuilder tops;
    MeshBuilder floor;

    auto isWall = [&](int row, int col) {
        return row >= 0 && col >= 0 && row < n && col < n && maze.tiles[row][col] == Tile::Wall;
    };

    for (int row = 0; row < n; ++row)
    {
        for (int col = 0; col < n; ++col)
        {
            const Vector3 c = tileCenter(row, col);
            if (!isWall(row, col))
            {
                floor.face(Vector3{c.x, 0.0f, c.z}, up, north, 0.5f, 0.5f, WHITE);
                continue;
            }
            tops.face(Vector3{c.x, WALL_HEIGHT, c.z}, up, north, 0.5f, 0.5f, theme.wallTop);
            const float mid = WALL_HEIGHT * 0.5f;
            if (!isWall(row, col + 1))
            {
                walls.face(Vector3{c.x + 0.5f, mid, c.z}, Vector3{1, 0, 0}, up, 0.5f, mid, WHITE);
            }
            if (!isWall(row, col - 1))
            {
                walls.face(Vector3{c.x - 0.5f, mid, c.z}, Vector3{-1, 0, 0}, up, 0.5f, mid, WHITE);
            }
            if (!isWall(row + 1, col))
            {
                walls.face(Vector3{c.x, mid, c.z + 0.5f}, Vector3{0, 0, 1}, up, 0.5f, mid, WHITE);
            }
            if (!isWall(row - 1, col))
            {
                walls.face(Vector3{c.x, mid, c.z - 0.5f}, Vector3{0, 0, -1}, up, 0.5f, mid, WHITE);
            }
        }
    }

    // A big ground plane around the maze (lava sea, snow field, neon grid...).
    MeshBuilder ground;
    const float extent = static_cast<float>(n) + 80.0f;
    const float centerXZ = static_cast<float>(n) * 0.5f;
    ground.face(Vector3{centerXZ, -0.01f, centerXZ}, up, north, extent * 0.5f, extent * 0.5f, WHITE, extent * 0.5f,
                extent * 0.5f);

    wallProp = makeProp(walls.build());
    wallTopProp = makeProp(tops.build());
    floorProp = makeProp(floor.build());
    groundProp = makeProp(ground.build());
    wallProp.material.maps[MATERIAL_MAP_DIFFUSE].texture = theme.wallTexture;
    wallTopProp.material.maps[MATERIAL_MAP_DIFFUSE].texture = topTexture;
    floorProp.material.maps[MATERIAL_MAP_DIFFUSE].texture = theme.floorTexture;
    groundProp.material.maps[MATERIAL_MAP_DIFFUSE].texture = theme.groundTexture;
    worldLoaded = true;
    worldTheme = themeId;
    buildDecorations(state);
}

int teleporterPairIndex(const GameState& state, int row, int col)
{
    const int size = state.maze.size;
    const int here = row * size + col;
    const int twin = getTeleportTarget(state, row, col);
    if (twin < 0)
    {
        return 0;
    }
    // Pairs are numbered in reading order of their first pad.
    const int key = here < twin ? here : twin;
    int index = 0;
    for (int cell = 0; cell < key; ++cell)
    {
        const int other = state.teleportLinks[cell];
        if (other > cell && state.items[cell / size][cell % size] == Item::Teleporter)
        {
            ++index;
        }
    }
    return index;
}

Color teleporterPairColor(int pairIndex)
{
    static const Color COLORS[] = {
        {190, 100, 255, 255}, // violet
        {255, 150, 30, 255},  // orange
        {40, 230, 140, 255},  // green
        {60, 170, 255, 255},  // blue
    };
    return COLORS[pairIndex % 4];
}

Color teleporterColor(const GameState& state, int row, int col)
{
    return teleporterPairColor(teleporterPairIndex(state, row, col));
}

void drawWorld(const GameState& state, ThemeId themeId, const WorldView& view,
               const std::vector<OpeningDoor>& openingDoors)
{
    const Theme& theme = getThemeData(themeId);
    const Maze& maze = state.maze;
    const float time = view.time;
    const Vector3 eye = view.camera.position;
    const bool topDown = view.topDown;

    drawSky(theme, themeId, GetScreenWidth(), GetScreenHeight(), time,
            std::atan2(view.camera.target.x - eye.x, -(view.camera.target.z - eye.z)));

    // ---- Lights ----
    std::vector<LightCandidate> lights;
    const float torchBoost = 1.0f + 0.35f * static_cast<float>(state.torchLevel);
    const float fogShrink = state.config.fogOfWar ? 0.65f : 1.0f;
    const float flicker = 1.0f + 0.05f * std::sin(time * 11.0f) + 0.03f * std::sin(time * 23.0f);
    lights.push_back(LightCandidate{Vector3Add(view.playerPosition, Vector3{0, 0.9f, 0}), theme.torch,
                                    theme.torchRadius * torchBoost * fogShrink * flicker, 0.0f});
    const int last = maze.size - 1;
    const Vector3 exitPos = cellCenter(last, last);
    lights.push_back(LightCandidate{Vector3Add(exitPos, Vector3{0, 1.0f, 0}), theme.exit, 7.0f, 0.0f});

    std::vector<LightCandidate> candidates;
    auto consider = [&](Vector3 position, Color color, float radius) {
        const float distance = Vector3Distance(position, view.playerPosition);
        if (distance < 22.0f)
        {
            candidates.push_back(LightCandidate{position, color, radius, distance});
        }
    };
    for (const Decor& decor : decorations)
    {
        consider(Vector3Add(decor.position, Vector3{0, 1.0f, 0}), decorLight(themeId, decor.seed), 4.5f * flicker);
    }
    for (int row = 0; row < maze.size; ++row)
    {
        for (int col = 0; col < maze.size; ++col)
        {
            const Vector3 at = Vector3Add(cellCenter(row, col), Vector3{0, 0.5f, 0});
            if (getCellTile(maze, row, col) == Tile::Key)
            {
                consider(at, theme.key, 3.0f);
            }
            const Item item = state.items[row][col];
            if (item == Item::Gem)
            {
                consider(at, theme.gem, 2.5f);
            }
            else if (item == Item::Teleporter)
            {
                consider(at, teleporterColor(state, row, col), 3.5f);
            }
            else if (item != Item::None && item != Item::Spikes)
            {
                consider(at, itemColor(item), 3.0f);
            }
        }
    }
    if (view.minotaurPositions != nullptr)
    {
        for (std::size_t i = 0; i < view.minotaurPositions->size(); ++i)
        {
            if (i < static_cast<std::size_t>(state.minotaurCount) && !state.minotaurs[i].released)
            {
                continue;
            }
            consider(Vector3Add((*view.minotaurPositions)[i], Vector3{0, 1.3f, 0.2f}), Color{255, 40, 20, 255}, 3.0f);
        }
    }
    std::sort(candidates.begin(), candidates.end(),
              [](const LightCandidate& a, const LightCandidate& b) { return a.distance < b.distance; });
    for (const LightCandidate& candidate : candidates)
    {
        if (lights.size() >= static_cast<std::size_t>(MAX_LIGHTS))
        {
            break;
        }
        lights.push_back(candidate);
    }

    // ---- Shader settings ----
    const float viewPos[3] = {eye.x, eye.y, eye.z};
    const float ambient[3] = {theme.ambient.r / 255.0f, theme.ambient.g / 255.0f, theme.ambient.b / 255.0f};
    const float fogColor[3] = {theme.fog.r / 255.0f, theme.fog.g / 255.0f, theme.fog.b / 255.0f};
    float fogDensity = theme.fogDensity * (state.config.fogOfWar ? 2.6f : 1.0f);
    fogDensity *= view.fogScale;
    SetShaderValue(litShader, locViewPos, viewPos, SHADER_UNIFORM_VEC3);
    SetShaderValue(litShader, locAmbient, ambient, SHADER_UNIFORM_VEC3);
    SetShaderValue(litShader, locFogColor, fogColor, SHADER_UNIFORM_VEC3);
    SetShaderValue(litShader, locFogDensity, &fogDensity, SHADER_UNIFORM_FLOAT);
    setLights(theme, lights);

    BeginMode3D(view.camera);

    // ---- The maze ----
    if (worldLoaded)
    {
        const Matrix identity = MatrixIdentity();
        setEmissive(0.0f, 1.0f);
        DrawMesh(groundProp.mesh, groundProp.material, identity);
        DrawMesh(floorProp.mesh, floorProp.material, identity);
        DrawMesh(wallProp.mesh, wallProp.material, identity);
        setEmissive(themeId == ThemeId::Neon ? 0.24f : 0.2f, 0.0f);
        wallTopProp.material.maps[MATERIAL_MAP_DIFFUSE].color = WHITE;
        DrawMesh(wallTopProp.mesh, wallTopProp.material, identity);
    }

    const float drawDistance = topDown ? 60.0f : (view.showPlayer ? 34.0f : 26.0f);
    auto visible = [&](Vector3 position) {
        return Vector3Distance(Vector3{eye.x, 0, eye.z}, position) < drawDistance;
    };

    // ---- Footprints and hints ----
    for (int row = 0; row < maze.size; ++row)
    {
        for (int col = 0; col < maze.size; ++col)
        {
            const Vector3 at = cellCenter(row, col);
            if (!visible(at) || (view.hideUndiscovered && !state.discovered[row][col]))
            {
                continue;
            }
            if (state.visitedCells[row][col] && !(row == state.playerRow && col == state.playerCol))
            {
                drawProp(cylinderProp, trs(Vector3{at.x, 0.005f, at.z}, Vector3{0, 0, 0}, Vector3{0.22f, 0.01f, 0.22f}),
                         withAlpha(theme.accent, 0.55f), 0.8f);
            }
        }
    }
    if (state.hintVisible)
    {
        for (int row = 0; row < maze.tileCount; ++row)
        {
            for (int col = 0; col < maze.tileCount; ++col)
            {
                if (!state.hintTiles[row][col])
                {
                    continue;
                }
                const Vector3 at = tileCenter(row, col);
                if (!visible(at))
                {
                    continue;
                }
                const float wave = 0.5f + 0.5f * std::sin(time * 5.0f - static_cast<float>(row + col) * 0.5f);
                drawProp(sphereProp, trs(Vector3{at.x, 0.3f + 0.1f * wave, at.z}, Vector3{0, 0, 0},
                                         Vector3{0.12f + 0.06f * wave, 0.12f + 0.06f * wave, 0.12f + 0.06f * wave}),
                         theme.hint, 1.0f);
            }
        }
    }

    // ---- Items ----
    for (int row = 0; row < maze.size; ++row)
    {
        for (int col = 0; col < maze.size; ++col)
        {
            const Vector3 at = cellCenter(row, col);
            if (!visible(at) || (view.hideUndiscovered && !state.discovered[row][col]))
            {
                continue;
            }
            if (getCellTile(maze, row, col) == Tile::Key)
            {
                drawKey(at, theme.key, time);
            }
            const Item item = state.items[row][col];
            switch (item)
            {
            case Item::None:
                break;
            case Item::Gem:
                drawGem(at, theme.gem, time);
                break;
            case Item::Spikes:
                drawSpikes(state, row, col, at, topDown, time);
                break;
            case Item::Teleporter:
                drawTeleporter(at, teleporterColor(state, row, col), time);
                break;
            default:
                drawPowerUp(item, at, time);
                break;
            }
        }
    }

    // ---- Decorations ----
    for (const Decor& decor : decorations)
    {
        if (visible(decor.position))
        {
            drawDecoration(decor, themeId, time);
        }
    }

    // ---- Minotaurs and the player ----
    if (view.minotaurPositions != nullptr)
    {
        for (std::size_t i = 0; i < view.minotaurPositions->size(); ++i)
        {
            if (i < static_cast<std::size_t>(state.minotaurCount) && !state.minotaurs[i].released)
            {
                continue; // still waiting at the entrance (gauntlet)
            }
            const bool hunting = i < static_cast<std::size_t>(state.minotaurCount) && state.minotaurs[i].hunting;
            drawMinotaur((*view.minotaurPositions)[i], (*view.minotaurYaws)[i], (*view.minotaurWalk)[i],
                         state.freezeTime > 0.0f, hunting, time);
        }
    }
    if (view.showPlayer)
    {
        const bool blink = state.invulnerableTime > 0.0f && std::fmod(time, 0.2f) < 0.08f;
        drawPlayerAvatar(view.playerPosition, view.playerYaw, theme.player, time, blink);
    }

    // ---- Transparent things last ----
    for (int row = 0; row < maze.tileCount; ++row)
    {
        for (int col = 0; col < maze.tileCount; ++col)
        {
            if (maze.tiles[row][col] == Tile::Door && visible(tileCenter(row, col)))
            {
                drawDoor(row, col, theme.door, 0.0f, time);
            }
        }
    }
    for (const OpeningDoor& door : openingDoors)
    {
        drawDoor(door.tileRow, door.tileCol, theme.door, door.progress, time);
    }
    drawExitBeacon(exitPos, theme.exit, time, isExitUnlocked(state));

    // Fog-of-war clouds in the top-down view: a calm, dark blue-grey that
    // only takes a hint of the world's colour, so it is easy on the eyes.
    if (view.hideUndiscovered)
    {
        const Color cloud = lerpColor(theme.fog, Color{24, 28, 44, 255}, 0.75f);
        for (int row = 0; row < maze.size; ++row)
        {
            for (int col = 0; col < maze.size; ++col)
            {
                if (!state.discovered[row][col])
                {
                    const Vector3 at = cellCenter(row, col);
                    drawProp(cubeProp, trs(Vector3{at.x, WALL_HEIGHT * 0.5f + 0.05f, at.z}, Vector3{0, 0, 0},
                                           Vector3{2.02f, WALL_HEIGHT + 0.12f, 2.02f}),
                             cloud, 0.55f);
                }
            }
        }
    }

    drawParticles3D(view.camera);
    EndMode3D();
}
