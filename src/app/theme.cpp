// theme.cpp
// The five visual worlds: colour palettes, procedural textures and skies.
//
// Every texture is generated in code when the app starts, so the game needs
// no image files. The alpha channel of the wall/floor textures is used as an
// "emission" mask by the lighting shader: alpha 255 = glows in the dark.

#include "app.h"

#include <cmath>

namespace
{

Theme themes[THEME_COUNT];

const int TEXTURE_SIZE = 256;

// ---- Small deterministic noise functions ----

float hash2(int x, int y, int seed)
{
    unsigned int h = static_cast<unsigned int>(x) * 374761393u + static_cast<unsigned int>(y) * 668265263u +
                     static_cast<unsigned int>(seed) * 2147483647u;
    h = (h ^ (h >> 13)) * 1274126177u;
    h ^= h >> 16;
    return static_cast<float>(h & 0xFFFFFF) / static_cast<float>(0x1000000);
}

float smooth(float t)
{
    return t * t * (3.0f - 2.0f * t);
}

// Value noise that tiles every `period` units.
float valueNoise(float x, float y, int seed, int period)
{
    const int x0 = static_cast<int>(std::floor(x));
    const int y0 = static_cast<int>(std::floor(y));
    const float fx = smooth(x - static_cast<float>(x0));
    const float fy = smooth(y - static_cast<float>(y0));
    auto wrap = [period](int v) { return ((v % period) + period) % period; };
    const float a = hash2(wrap(x0), wrap(y0), seed);
    const float b = hash2(wrap(x0 + 1), wrap(y0), seed);
    const float c = hash2(wrap(x0), wrap(y0 + 1), seed);
    const float d = hash2(wrap(x0 + 1), wrap(y0 + 1), seed);
    return (a + (b - a) * fx) + ((c + (d - c) * fx) - (a + (b - a) * fx)) * fy;
}

// Fractal noise in [0, 1] that tiles across the texture.
float fbm(float u, float v, int seed, int octaves = 4)
{
    float total = 0.0f;
    float amplitude = 0.5f;
    int period = 4;
    for (int i = 0; i < octaves; ++i)
    {
        total += amplitude * valueNoise(u * static_cast<float>(period), v * static_cast<float>(period), seed + i,
                                        period);
        amplitude *= 0.5f;
        period *= 2;
    }
    return total / 0.9375f;
}

// Distance to the edge between Voronoi cells (for cracks). Tiles.
float crackDistance(float u, float v, int seed, int cells)
{
    const float x = u * static_cast<float>(cells);
    const float y = v * static_cast<float>(cells);
    const int cx = static_cast<int>(std::floor(x));
    const int cy = static_cast<int>(std::floor(y));
    float first = 10.0f;
    float second = 10.0f;
    for (int dy = -1; dy <= 1; ++dy)
    {
        for (int dx = -1; dx <= 1; ++dx)
        {
            const int gx = cx + dx;
            const int gy = cy + dy;
            const int wx = ((gx % cells) + cells) % cells;
            const int wy = ((gy % cells) + cells) % cells;
            const float px = static_cast<float>(gx) + hash2(wx, wy, seed);
            const float py = static_cast<float>(gy) + hash2(wx, wy, seed + 17);
            const float distance = std::sqrt((px - x) * (px - x) + (py - y) * (py - y));
            if (distance < first)
            {
                second = first;
                first = distance;
            }
            else if (distance < second)
            {
                second = distance;
            }
        }
    }
    return second - first;
}

unsigned char toByte(float value)
{
    value = value < 0.0f ? 0.0f : (value > 1.0f ? 1.0f : value);
    return static_cast<unsigned char>(value * 255.0f + 0.5f);
}

Color scaled(Color base, float factor, float emission)
{
    return Color{toByte(base.r / 255.0f * factor), toByte(base.g / 255.0f * factor),
                 toByte(base.b / 255.0f * factor), toByte(emission)};
}

Color mixPixel(Color a, Color b, float t)
{
    Color c = lerpColor(a, b, t);
    c.a = toByte((a.a + (b.a - a.a) * t) / 255.0f);
    return c;
}

// Calls `shade(u, v)` for every pixel and builds a repeating, mip-mapped texture.
template <typename Shader>
Texture2D makeTexture(Shader shade)
{
    Image image = GenImageColor(TEXTURE_SIZE, TEXTURE_SIZE, BLANK);
    Color* pixels = static_cast<Color*>(image.data);
    for (int y = 0; y < TEXTURE_SIZE; ++y)
    {
        for (int x = 0; x < TEXTURE_SIZE; ++x)
        {
            const float u = (static_cast<float>(x) + 0.5f) / TEXTURE_SIZE;
            const float v = (static_cast<float>(y) + 0.5f) / TEXTURE_SIZE;
            pixels[y * TEXTURE_SIZE + x] = shade(u, v);
        }
    }
    Texture2D texture = LoadTextureFromImage(image);
    UnloadImage(image);
    GenTextureMipmaps(&texture);
    SetTextureFilter(texture, TEXTURE_FILTER_TRILINEAR);
    SetTextureWrap(texture, TEXTURE_WRAP_REPEAT);
    return texture;
}

// How far (0..0.5) a point is from the nearest edge of the unit square.
float edgeDistance(float u, float v)
{
    const float du = u < 0.5f ? u : 1.0f - u;
    const float dv = v < 0.5f ? v : 1.0f - v;
    return du < dv ? du : dv;
}

// A soft glowing line: 1 on the line, fading out over `glow`.
float glowLine(float distance, float width, float glow)
{
    if (distance < width)
    {
        return 1.0f;
    }
    const float t = 1.0f - (distance - width) / glow;
    return t > 0.0f ? t * t : 0.0f;
}

// Brick pattern: returns mortar strength (1 = mortar) and the brick id.
float bricks(float u, float v, int rows, int columns, float mortar, int& brickId)
{
    const float rowF = v * static_cast<float>(rows);
    const int row = static_cast<int>(rowF);
    const float offset = (row % 2 == 0) ? 0.0f : 0.5f;
    const float colF = u * static_cast<float>(columns) + offset;
    const int col = static_cast<int>(colF);
    brickId = row * 31 + (col % columns);
    const float fy = rowF - static_cast<float>(row);
    const float fx = colF - static_cast<float>(col);
    const float edge = std::fmin(std::fmin(fy, 1.0f - fy) / static_cast<float>(columns) * static_cast<float>(rows),
                                 std::fmin(fx, 1.0f - fx));
    return edge < mortar ? 1.0f : 0.0f;
}

// ---- Texture generators per theme ----

void makeNeonTextures(Theme& t)
{
    t.wallTexture = makeTexture([](float u, float v) {
        const float border = glowLine(edgeDistance(u, v), 0.015f, 0.08f);
        const float bands = glowLine(std::fmin(std::fabs(v - 0.12f), std::fabs(v - 0.88f)), 0.004f, 0.03f);
        const float traces = (hash2(static_cast<int>(u * 32), static_cast<int>(v * 48), 3) > 0.975f) ? 0.4f : 0.0f;
        const float base = 0.8f + 0.4f * v;
        Color color = scaled(Color{16, 10, 40, 255}, base, 0.0f);
        color = mixPixel(color, Color{170, 60, 255, 160}, traces * 0.5f);
        // Softer, less self-lit lines than before (QA: Neon Grid hurt the eyes).
        color = mixPixel(color, Color{50, 185, 215, 140}, bands * 0.6f);
        color = mixPixel(color, Color{205, 45, 175, 150}, border * 0.85f);
        return color;
    });
    t.floorTexture = makeTexture([](float u, float v) {
        const float grid = glowLine(edgeDistance(u, v), 0.012f, 0.06f);
        Color color = scaled(Color{8, 6, 22, 255}, 0.8f + 0.3f * fbm(u, v, 5), 0.0f);
        return mixPixel(color, Color{30, 160, 200, 140}, grid * 0.8f);
    });
    t.groundTexture = makeTexture([](float u, float v) {
        const float grid = glowLine(edgeDistance(u, v), 0.01f, 0.05f);
        return mixPixel(Color{6, 2, 18, 0}, Color{140, 30, 190, 130}, grid * 0.8f);
    });
}

void makeDungeonTextures(Theme& t)
{
    t.wallTexture = makeTexture([](float u, float v) {
        int brick = 0;
        const float mortar = bricks(u, v, 6, 3, 0.035f, brick);
        const float variation = 0.75f + 0.35f * hash2(brick, 1, 7);
        const float grain = 0.75f + 0.5f * fbm(u, v, 11, 5);
        Color stone = scaled(Color{120, 106, 92, 255}, variation * grain, 0.0f);
        const float moss = fbm(u, v, 23, 3) > 0.68f ? 0.4f : 0.0f;
        stone = mixPixel(stone, Color{70, 95, 45, 0}, moss);
        return mortar > 0.5f ? Color{38, 32, 28, 0} : stone;
    });
    t.floorTexture = makeTexture([](float u, float v) {
        int stone = 0;
        const float mortar = bricks(u, v, 2, 2, 0.03f, stone);
        const float variation = 0.7f + 0.3f * hash2(stone, 4, 9);
        const float grain = 0.7f + 0.5f * fbm(u, v, 31, 5);
        const float crack = crackDistance(u, v, 13, 4) < 0.03f ? 0.6f : 1.0f;
        Color color = scaled(Color{88, 82, 78, 255}, variation * grain * crack, 0.0f);
        return mortar > 0.5f ? Color{30, 26, 24, 0} : color;
    });
    t.groundTexture = makeTexture([](float u, float v) {
        return scaled(Color{40, 32, 26, 255}, 0.6f + 0.6f * fbm(u, v, 41, 5), 0.0f);
    });
}

void makeLavaTextures(Theme& t)
{
    t.wallTexture = makeTexture([](float u, float v) {
        const float crack = crackDistance(u, v, 51, 5);
        const float glow = glowLine(crack, 0.012f, 0.05f);
        Color rock = scaled(Color{52, 38, 40, 255}, 0.6f + 0.7f * fbm(u, v, 52, 5), 0.0f);
        const float heat = 1.0f - v; // glows more near the floor
        return mixPixel(rock, Color{255, 110, 20, 255}, glow * (0.5f + 0.5f * heat));
    });
    t.floorTexture = makeTexture([](float u, float v) {
        const float seam = glowLine(edgeDistance(u, v), 0.006f, 0.04f);
        const float crack = glowLine(crackDistance(u, v, 61, 3), 0.008f, 0.04f);
        Color rock = scaled(Color{60, 44, 42, 255}, 0.6f + 0.6f * fbm(u, v, 62, 5), 0.0f);
        return mixPixel(rock, Color{255, 140, 30, 255}, seam > crack ? seam : crack);
    });
    t.groundTexture = makeTexture([](float u, float v) {
        const float n = fbm(u, v, 71, 5);
        const Color lava = lerpColor(Color{180, 30, 0, 255}, Color{255, 200, 60, 255}, n * n);
        Color color = lava;
        color.a = 255; // the lava sea glows
        const float crust = crackDistance(u, v, 72, 6) > 0.18f ? 0.75f : 0.0f;
        return mixPixel(color, Color{40, 20, 18, 40}, crust * (1.0f - n));
    });
}

void makeIceTextures(Theme& t)
{
    t.wallTexture = makeTexture([](float u, float v) {
        int brick = 0;
        const float mortar = bricks(u, v, 5, 2, 0.03f, brick);
        const float variation = 0.85f + 0.2f * hash2(brick, 2, 81);
        const float frost = fbm(u, v, 82, 5);
        Color ice = scaled(Color{105, 160, 210, 255}, variation * (0.8f + 0.3f * frost), 12.0f / 255.0f);
        const float sparkle = hash2(static_cast<int>(u * 128), static_cast<int>(v * 128), 83) > 0.996f ? 1.0f : 0.0f;
        ice = mixPixel(ice, Color{255, 255, 255, 255}, sparkle);
        return mortar > 0.5f ? Color{190, 220, 245, 20} : ice;
    });
    t.floorTexture = makeTexture([](float u, float v) {
        const float tileEdge = glowLine(edgeDistance(u, v), 0.008f, 0.03f);
        const float streak = glowLine(std::fabs(std::fmod(u + v * 0.6f, 0.33f) - 0.16f), 0.004f, 0.03f);
        Color ice = scaled(Color{130, 175, 215, 255}, 0.85f + 0.25f * fbm(u, v, 91, 4), 8.0f / 255.0f);
        ice = mixPixel(ice, Color{255, 255, 255, 30}, streak * 0.6f);
        return mixPixel(ice, Color{120, 170, 210, 30}, tileEdge);
    });
    t.groundTexture = makeTexture([](float u, float v) {
        return scaled(Color{235, 242, 255, 255}, 0.85f + 0.2f * fbm(u, v, 95, 5), 10.0f / 255.0f);
    });
}

void makeCandyTextures(Theme& t)
{
    t.wallTexture = makeTexture([](float u, float v) {
        const float stripe = std::fmod(u + v * 0.66f, 0.25f) < 0.125f ? 1.0f : 0.0f;
        Color color = stripe > 0.5f ? Color{255, 95, 175, 255} : Color{255, 242, 250, 255};
        const float shine = glowLine(std::fabs(u - 0.3f), 0.03f, 0.1f) * 0.45f;
        color = mixPixel(color, Color{255, 255, 255, 255}, shine);
        color.a = 8;
        return color;
    });
    t.floorTexture = makeTexture([](float u, float v) {
        const bool checker = (static_cast<int>(u * 2) + static_cast<int>(v * 2)) % 2 == 0;
        Color color = checker ? Color{205, 175, 255, 6} : Color{170, 240, 215, 6};
        const int sx = static_cast<int>(u * 48);
        const int sy = static_cast<int>(v * 48);
        if (hash2(sx, sy, 101) > 0.94f)
        {
            const Color sprinkles[4] = {{255, 80, 120, 200}, {255, 220, 60, 200}, {80, 200, 255, 200},
                                        {255, 255, 255, 200}};
            color = sprinkles[static_cast<int>(hash2(sx, sy, 102) * 4.0f) % 4];
        }
        return color;
    });
    t.groundTexture = makeTexture([](float u, float v) {
        const float n = fbm(u, v, 111, 4);
        return Color{255, toByte(0.72f + 0.12f * n), toByte(0.85f + 0.08f * n), 6};
    });
}

void setPalette(Theme& t, const char* name, const char* tagline, Color accent, Color accent2, Color accent3,
                Color panel, Color skyTop, Color skyBottom, Color fog, float fogDensity, Color ambient, Color torch,
                float torchRadius, Color wallTop)
{
    t.name = name;
    t.tagline = tagline;
    t.accent = accent;
    t.accent2 = accent2;
    t.accent3 = accent3;
    t.panel = panel;
    t.skyTop = skyTop;
    t.skyBottom = skyBottom;
    t.fog = fog;
    t.fogDensity = fogDensity;
    t.ambient = ambient;
    t.torch = torch;
    t.torchRadius = torchRadius;
    t.wallTop = wallTop;
}

void setPropColors(Theme& t, Color door, Color key, Color gem, Color exit, Color hint, Color player)
{
    t.door = door;
    t.key = key;
    t.gem = gem;
    t.exit = exit;
    t.hint = hint;
    t.player = player;
}

void setGlow(Theme& t, float bloom, float threshold, float saturation, AmbientStyle style)
{
    t.bloomStrength = bloom;
    t.bloomThreshold = threshold;
    t.saturation = saturation;
    t.ambientStyle = style;
}

// ---- Skies ----

void drawStars(int width, int height, float time, int count, float maxY)
{
    for (int i = 0; i < count; ++i)
    {
        const float x = hash2(i, 1, 501) * static_cast<float>(width);
        const float y = hash2(i, 2, 501) * static_cast<float>(height) * maxY;
        const float twinkle = 0.5f + 0.5f * std::sin(time * (1.0f + hash2(i, 3, 501) * 3.0f) + static_cast<float>(i));
        DrawCircleV(Vector2{x, y}, 1.0f + hash2(i, 4, 501) * 1.5f, withAlpha(WHITE, 0.3f + 0.7f * twinkle));
    }
}

// Horizontal position of something that sits in a fixed compass direction.
float skyX(float yaw, float direction, int width)
{
    float angle = direction - yaw;
    while (angle > PI)
    {
        angle -= 2.0f * PI;
    }
    while (angle < -PI)
    {
        angle += 2.0f * PI;
    }
    return static_cast<float>(width) * (0.5f + angle / 1.6f);
}

void drawNeonSky(int width, int height, float time, float yaw)
{
    const float horizon = static_cast<float>(height) * 0.5f;
    drawStars(width, height, time, 160, 0.45f);
    const float sunX = skyX(yaw, 0.0f, width);
    const float radius = static_cast<float>(height) * 0.22f;
    // Glow, then the striped synthwave sun.
    DrawCircleGradient(static_cast<int>(sunX), static_cast<int>(horizon), radius * 2.0f, Color{255, 60, 160, 90},
                       Color{255, 60, 160, 0});
    for (int line = 0; line < static_cast<int>(radius * 2); ++line)
    {
        const float y = horizon - radius + static_cast<float>(line);
        if (y > horizon)
        {
            break;
        }
        const float dy = (y - (horizon - radius * 0.15f)) / radius;
        const float halfWidth = radius * std::sqrt(std::fmax(0.0f, 1.0f - dy * dy));
        const float t = static_cast<float>(line) / (radius * 2.0f);
        const bool gap = t > 0.55f && std::fmod(t * 22.0f - time * 0.5f, 2.2f) < 0.6f + t * 0.8f;
        if (!gap)
        {
            DrawLineEx(Vector2{sunX - halfWidth, y}, Vector2{sunX + halfWidth, y}, 1.2f,
                       lerpColor(Color{255, 230, 80, 255}, Color{255, 40, 160, 255}, t * 1.6f));
        }
    }
    // Neon mountains
    for (int i = 0; i < 14; ++i)
    {
        const float x = skyX(yaw, -PI + static_cast<float>(i) * 0.45f, width);
        const float peak = horizon - static_cast<float>(height) * (0.05f + 0.08f * hash2(i, 9, 7));
        DrawTriangle(Vector2{x, peak}, Vector2{x - 120, horizon}, Vector2{x + 120, horizon}, Color{30, 6, 50, 255});
        DrawLineEx(Vector2{x - 120, horizon}, Vector2{x, peak}, 2.0f, Color{255, 60, 220, 200});
        DrawLineEx(Vector2{x, peak}, Vector2{x + 120, horizon}, 2.0f, Color{255, 60, 220, 200});
    }
}

void drawLavaSky(int width, int height, float time, float yaw)
{
    const float horizon = static_cast<float>(height) * 0.5f;
    for (int i = 0; i < 6; ++i)
    {
        const float x = skyX(yaw, static_cast<float>(i) * 1.05f, width);
        const float top = horizon - static_cast<float>(height) * (0.12f + 0.06f * hash2(i, 3, 33));
        DrawTriangle(Vector2{x, top}, Vector2{x - 200, horizon}, Vector2{x + 200, horizon}, Color{40, 8, 6, 255});
        const float pulse = 0.6f + 0.4f * std::sin(time * 2.0f + static_cast<float>(i));
        DrawCircleGradient(static_cast<int>(x), static_cast<int>(top), 50.0f * pulse, Color{255, 140, 30, 200},
                           Color{255, 60, 0, 0});
    }
    DrawRectangleGradientV(0, static_cast<int>(horizon) - 60, width, 60, Color{255, 80, 0, 0},
                           Color{255, 90, 20, 120});
}

void drawIceSky(int width, int height, float time)
{
    drawStars(width, height, time, 140, 0.5f);
    // Aurora ribbons
    for (int ribbon = 0; ribbon < 3; ++ribbon)
    {
        const Color color = ribbon == 1 ? Color{180, 100, 255, 255} : Color{80, 255, 200, 255};
        for (int x = 0; x < width; x += 4)
        {
            const float fx = static_cast<float>(x) / static_cast<float>(width);
            const float y = static_cast<float>(height) *
                            (0.16f + 0.05f * ribbon + 0.05f * std::sin(fx * 6.0f + time * 0.4f + ribbon * 1.7f));
            const float alpha = 0.10f + 0.10f * std::sin(fx * 11.0f + time * 0.9f + ribbon);
            DrawRectangleGradientV(x, static_cast<int>(y), 4, static_cast<int>(height * 0.14f), withAlpha(color, alpha),
                                   withAlpha(color, 0.0f));
        }
    }
}

void drawCandySky(int width, int height, float time, float yaw)
{
    const float horizon = static_cast<float>(height) * 0.5f;
    const Color bands[6] = {{255, 90, 90, 90}, {255, 170, 70, 90}, {255, 240, 90, 90},
                            {110, 230, 120, 90}, {90, 180, 255, 90}, {180, 110, 255, 90}};
    const float rainbowX = skyX(yaw, 2.2f, width);
    for (int i = 0; i < 6; ++i)
    {
        const float inner = static_cast<float>(height) * 0.30f - static_cast<float>(i) * 14.0f;
        DrawRing(Vector2{rainbowX, horizon}, inner - 14.0f, inner, 180.0f, 360.0f, 48, bands[i]);
    }
    for (int i = 0; i < 9; ++i)
    {
        const float x = std::fmod(hash2(i, 1, 9) * static_cast<float>(width) + time * (8.0f + 6.0f * hash2(i, 2, 9)),
                                  static_cast<float>(width) + 200.0f) - 100.0f;
        const float y = static_cast<float>(height) * (0.08f + 0.3f * hash2(i, 3, 9));
        for (int puff = 0; puff < 4; ++puff)
        {
            DrawCircleV(Vector2{x + puff * 26.0f, y + (puff % 2) * 8.0f}, 22.0f + (puff % 2) * 8.0f,
                        Color{255, 255, 255, 170});
        }
    }
}

void drawDungeonSky(int width, int height, float time)
{
    for (int i = 0; i < 40; ++i)
    {
        const float x = hash2(i, 1, 77) * static_cast<float>(width);
        const float y = std::fmod(hash2(i, 2, 77) * static_cast<float>(height) + time * 6.0f, static_cast<float>(height));
        DrawCircleV(Vector2{x, y}, 1.5f, Color{255, 190, 120, 50});
    }
}

} // namespace

// ---------------------------------------------------------------------------

Color withAlpha(Color color, float alpha)
{
    alpha = alpha < 0.0f ? 0.0f : (alpha > 1.0f ? 1.0f : alpha);
    color.a = static_cast<unsigned char>(color.a * alpha);
    return color;
}

Color lerpColor(Color a, Color b, float t)
{
    t = t < 0.0f ? 0.0f : (t > 1.0f ? 1.0f : t);
    return Color{static_cast<unsigned char>(a.r + (b.r - a.r) * t), static_cast<unsigned char>(a.g + (b.g - a.g) * t),
                 static_cast<unsigned char>(a.b + (b.b - a.b) * t), static_cast<unsigned char>(a.a + (b.a - a.a) * t)};
}

Color rainbow(float t, float saturation, float value)
{
    float hue = std::fmod(t, 1.0f);
    if (hue < 0.0f)
    {
        hue += 1.0f;
    }
    return ColorFromHSV(hue * 360.0f, saturation, value);
}

void loadThemes()
{
    Theme& neon = themes[static_cast<int>(ThemeId::Neon)];
    setPalette(neon, "Neon Grid", "Synthwave circuits under a striped sun", {255, 50, 210, 255},
               {40, 230, 255, 255}, {255, 230, 80, 255}, {16, 8, 38, 225}, {6, 2, 26, 255}, {110, 24, 120, 255},
               {40, 12, 58, 255}, 0.030f, {82, 70, 120, 255}, {230, 140, 220, 255}, 9.5f, {150, 52, 140, 255});
    setPropColors(neon, {255, 40, 160, 255}, {255, 215, 60, 255}, {60, 255, 240, 255}, {80, 255, 140, 255},
                  {60, 230, 255, 255}, {255, 240, 120, 255});
    setGlow(neon, 0.7f, 0.7f, 1.08f, AmbientStyle::NeonDust);
    makeNeonTextures(neon);

    Theme& dungeon = themes[static_cast<int>(ThemeId::Dungeon)];
    setPalette(dungeon, "The Dungeon", "Ancient stone, flickering torches", {255, 170, 60, 255},
               {220, 120, 60, 255}, {255, 225, 150, 255}, {26, 18, 14, 230}, {8, 6, 6, 255}, {44, 30, 24, 255},
               {16, 11, 9, 255}, 0.065f, {48, 40, 36, 255}, {255, 160, 80, 255}, 8.0f, {120, 108, 96, 255});
    setPropColors(dungeon, {170, 80, 255, 255}, {255, 200, 60, 255}, {80, 200, 255, 255}, {120, 255, 120, 255},
                  {255, 200, 90, 255}, {255, 220, 160, 255});
    setGlow(dungeon, 0.95f, 0.6f, 1.1f, AmbientStyle::DungeonMotes);
    makeDungeonTextures(dungeon);

    Theme& lava = themes[static_cast<int>(ThemeId::Lava)];
    setPalette(lava, "Lava Core", "Molten rivers and burning skies", {255, 90, 30, 255}, {255, 200, 40, 255},
               {255, 70, 70, 255}, {34, 10, 6, 230}, {30, 4, 4, 255}, {170, 44, 10, 255}, {100, 24, 8, 255},
               0.040f, {62, 40, 36, 255}, {255, 165, 95, 255}, 8.5f, {255, 110, 30, 255});
    setPropColors(lava, {80, 200, 255, 255}, {255, 240, 120, 255}, {255, 70, 140, 255}, {60, 255, 170, 255},
                  {255, 220, 60, 255}, {255, 255, 210, 255});
    setGlow(lava, 1.0f, 0.62f, 1.15f, AmbientStyle::Embers);
    makeLavaTextures(lava);

    Theme& ice = themes[static_cast<int>(ThemeId::Ice)];
    setPalette(ice, "Frozen Depths", "Crystal walls under the aurora", {120, 220, 255, 255}, {200, 160, 255, 255},
               {255, 255, 255, 255}, {10, 26, 48, 225}, {4, 10, 32, 255}, {50, 100, 150, 255}, {80, 120, 165, 255},
               0.038f, {62, 76, 100, 255}, {140, 175, 210, 255}, 8.0f, {180, 220, 245, 255});
    setPropColors(ice, {255, 80, 200, 255}, {255, 210, 80, 255}, {190, 120, 255, 255}, {120, 255, 200, 255},
                  {255, 255, 255, 255}, {255, 240, 200, 255});
    setGlow(ice, 0.65f, 0.82f, 1.1f, AmbientStyle::Snow);
    makeIceTextures(ice);

    Theme& candy = themes[static_cast<int>(ThemeId::Candy)];
    setPalette(candy, "Candy Kingdom", "Sugar-coated corridors and rainbows", {255, 100, 190, 255},
               {110, 215, 255, 255}, {255, 225, 80, 255}, {70, 24, 70, 220}, {255, 165, 215, 255},
               {170, 210, 255, 255}, {225, 165, 200, 255}, 0.028f, {112, 98, 118, 255}, {195, 165, 185, 255}, 9.0f,
               {130, 255, 200, 255});
    setPropColors(candy, {150, 90, 255, 255}, {255, 200, 40, 255}, {255, 60, 160, 255}, {60, 220, 140, 255},
                  {255, 120, 220, 255}, {255, 255, 255, 255});
    setGlow(candy, 0.55f, 0.86f, 1.15f, AmbientStyle::Sprinkles);
    makeCandyTextures(candy);
}

void unloadThemes()
{
    for (Theme& theme : themes)
    {
        UnloadTexture(theme.wallTexture);
        UnloadTexture(theme.floorTexture);
        UnloadTexture(theme.groundTexture);
    }
}

Theme& getThemeData(ThemeId id)
{
    return themes[static_cast<int>(id)];
}

void drawSky(const Theme& theme, ThemeId id, int width, int height, float time, float yaw)
{
    DrawRectangleGradientV(0, 0, width, height / 2 + 2, theme.skyTop, theme.skyBottom);
    DrawRectangleGradientV(0, height / 2, width, height - height / 2, theme.skyBottom, theme.fog);
    switch (id)
    {
    case ThemeId::Neon:
        drawNeonSky(width, height, time, yaw);
        break;
    case ThemeId::Dungeon:
        drawDungeonSky(width, height, time);
        break;
    case ThemeId::Lava:
        drawLavaSky(width, height, time, yaw);
        break;
    case ThemeId::Ice:
        drawIceSky(width, height, time);
        break;
    case ThemeId::Candy:
        drawCandySky(width, height, time, yaw);
        break;
    }
}
