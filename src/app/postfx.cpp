// postfx.cpp
// Post-processing: the whole 3D scene is drawn into an off-screen texture,
// then shaders add bloom (glow around bright things), CRT scanlines,
// chromatic aberration, a vignette, coloured screen flashes and the red
// "danger" pulse when the Minotaur is close.

#include "app.h"

namespace
{

const char* const VERTEX_SHADER = R"(#version 330
in vec3 vertexPosition;
in vec2 vertexTexCoord;
in vec4 vertexColor;
uniform mat4 mvp;
out vec2 fragTexCoord;
out vec4 fragColor;
void main()
{
    fragTexCoord = vertexTexCoord;
    fragColor = vertexColor;
    gl_Position = mvp * vec4(vertexPosition, 1.0);
}
)";

const char* const BRIGHT_SHADER = R"(#version 330
in vec2 fragTexCoord;
out vec4 finalColor;
uniform sampler2D texture0;
uniform float threshold;
void main()
{
    vec3 color = texture(texture0, fragTexCoord).rgb;
    float brightness = max(color.r, max(color.g, color.b));
    float amount = smoothstep(threshold, threshold + 0.3, brightness);
    finalColor = vec4(color * amount, 1.0);
}
)";

const char* const BLUR_SHADER = R"(#version 330
in vec2 fragTexCoord;
out vec4 finalColor;
uniform sampler2D texture0;
uniform vec2 direction;
void main()
{
    float weights[5] = float[](0.227027, 0.1945946, 0.1216216, 0.054054, 0.016216);
    vec3 sum = texture(texture0, fragTexCoord).rgb * weights[0];
    for (int i = 1; i < 5; i++)
    {
        vec2 offset = direction * float(i);
        sum += texture(texture0, fragTexCoord + offset).rgb * weights[i];
        sum += texture(texture0, fragTexCoord - offset).rgb * weights[i];
    }
    finalColor = vec4(sum, 1.0);
}
)";

const char* const COMPOSITE_SHADER = R"(#version 330
in vec2 fragTexCoord;
out vec4 finalColor;
uniform sampler2D texture0;
uniform sampler2D bloomTexture;
uniform float bloomStrength;
uniform float saturation;
uniform float crt;
uniform float time;
uniform float danger;
uniform float freeze;
uniform vec2 resolution;
uniform vec4 flash;
void main()
{
    vec2 uv = fragTexCoord;
    vec2 centered = uv - 0.5;
    float distance2 = dot(centered, centered);
    vec3 color;
    if (crt > 0.5)
    {
        vec2 offset = centered * (0.0035 + distance2 * 0.012);
        color.r = texture(texture0, uv + offset).r;
        color.g = texture(texture0, uv).g;
        color.b = texture(texture0, uv - offset).b;
    }
    else
    {
        color = texture(texture0, uv).rgb;
    }

    color += texture(bloomTexture, uv).rgb * bloomStrength;

    float luma = dot(color, vec3(0.299, 0.587, 0.114));
    color = mix(vec3(luma), color, saturation);

    color *= clamp(1.0 - distance2 * 0.95, 0.0, 1.0);

    float edge = smoothstep(0.12, 0.5, sqrt(distance2));
    color = mix(color, vec3(1.0, 0.05, 0.12), edge * danger * (0.55 + 0.45 * sin(time * 7.0)));
    color = mix(color, vec3(0.55, 0.85, 1.0), edge * freeze * 0.55);

    if (crt > 0.5)
    {
        float scan = 0.9 + 0.1 * sin(uv.y * resolution.y * 3.14159);
        float roll = 0.985 + 0.015 * sin(uv.y * 8.0 - time * 3.0);
        color *= scan * roll;
    }

    color = mix(color, flash.rgb, flash.a);
    color = color / (1.0 + max(color - 1.0, 0.0)); // soften blown-out highlights
    finalColor = vec4(color, 1.0);
}
)";

RenderTexture2D sceneTarget;
RenderTexture2D blurA;
RenderTexture2D blurB;
Shader brightShader;
Shader blurShader;
Shader compositeShader;
int targetWidth = 0;
int targetHeight = 0;

int thresholdLoc = -1;
int directionLoc = -1;
int bloomTextureLoc = -1;
int bloomStrengthLoc = -1;
int saturationLoc = -1;
int crtLoc = -1;
int timeLoc = -1;
int dangerLoc = -1;
int freezeLoc = -1;
int resolutionLoc = -1;
int flashLoc = -1;

void createTargets(int width, int height)
{
    targetWidth = width < 1 ? 1 : width;
    targetHeight = height < 1 ? 1 : height;
    sceneTarget = LoadRenderTexture(targetWidth, targetHeight);
    const int blurWidth = targetWidth / 4 < 1 ? 1 : targetWidth / 4;
    const int blurHeight = targetHeight / 4 < 1 ? 1 : targetHeight / 4;
    blurA = LoadRenderTexture(blurWidth, blurHeight);
    blurB = LoadRenderTexture(blurWidth, blurHeight);
    SetTextureFilter(sceneTarget.texture, TEXTURE_FILTER_BILINEAR);
    SetTextureFilter(blurA.texture, TEXTURE_FILTER_BILINEAR);
    SetTextureFilter(blurB.texture, TEXTURE_FILTER_BILINEAR);
    SetTextureWrap(blurA.texture, TEXTURE_WRAP_CLAMP);
    SetTextureWrap(blurB.texture, TEXTURE_WRAP_CLAMP);
}

void destroyTargets()
{
    UnloadRenderTexture(sceneTarget);
    UnloadRenderTexture(blurA);
    UnloadRenderTexture(blurB);
}

// Draws a render texture so that it fills `target` (render textures are upside down).
void drawFullscreen(const RenderTexture2D& source, int width, int height)
{
    DrawTexturePro(source.texture,
                   Rectangle{0, 0, static_cast<float>(source.texture.width), -static_cast<float>(source.texture.height)},
                   Rectangle{0, 0, static_cast<float>(width), static_cast<float>(height)}, Vector2{0, 0}, 0.0f, WHITE);
}

void blurPass(const RenderTexture2D& source, RenderTexture2D& target, float dx, float dy)
{
    const float direction[2] = {dx / static_cast<float>(source.texture.width),
                                dy / static_cast<float>(source.texture.height)};
    BeginTextureMode(target);
    ClearBackground(BLACK);
    BeginShaderMode(blurShader);
    SetShaderValue(blurShader, directionLoc, direction, SHADER_UNIFORM_VEC2);
    drawFullscreen(source, target.texture.width, target.texture.height);
    EndShaderMode();
    EndTextureMode();
}

} // namespace

void initPostFx(int width, int height)
{
    brightShader = LoadShaderFromMemory(VERTEX_SHADER, BRIGHT_SHADER);
    blurShader = LoadShaderFromMemory(VERTEX_SHADER, BLUR_SHADER);
    compositeShader = LoadShaderFromMemory(VERTEX_SHADER, COMPOSITE_SHADER);
    thresholdLoc = GetShaderLocation(brightShader, "threshold");
    directionLoc = GetShaderLocation(blurShader, "direction");
    bloomTextureLoc = GetShaderLocation(compositeShader, "bloomTexture");
    bloomStrengthLoc = GetShaderLocation(compositeShader, "bloomStrength");
    saturationLoc = GetShaderLocation(compositeShader, "saturation");
    crtLoc = GetShaderLocation(compositeShader, "crt");
    timeLoc = GetShaderLocation(compositeShader, "time");
    dangerLoc = GetShaderLocation(compositeShader, "danger");
    freezeLoc = GetShaderLocation(compositeShader, "freeze");
    resolutionLoc = GetShaderLocation(compositeShader, "resolution");
    flashLoc = GetShaderLocation(compositeShader, "flash");
    createTargets(width, height);
}

void unloadPostFx()
{
    destroyTargets();
    UnloadShader(brightShader);
    UnloadShader(blurShader);
    UnloadShader(compositeShader);
}

void resizePostFx(int width, int height)
{
    if (width == targetWidth && height == targetHeight)
    {
        return;
    }
    destroyTargets();
    createTargets(width, height);
}

void beginScene()
{
    BeginTextureMode(sceneTarget);
    ClearBackground(BLACK);
}

void endScene()
{
    EndTextureMode();
}

void drawSceneWithEffects(const PostEffects& effects, bool bloomEnabled)
{
    const int width = GetScreenWidth();
    const int height = GetScreenHeight();

    if (bloomEnabled)
    {
        // 1. Keep only the bright parts, at quarter resolution.
        BeginTextureMode(blurA);
        ClearBackground(BLACK);
        BeginShaderMode(brightShader);
        SetShaderValue(brightShader, thresholdLoc, &effects.bloomThreshold, SHADER_UNIFORM_FLOAT);
        drawFullscreen(sceneTarget, blurA.texture.width, blurA.texture.height);
        EndShaderMode();
        EndTextureMode();

        // 2. Blur them (horizontal + vertical, twice, wider the second time).
        blurPass(blurA, blurB, 1.0f, 0.0f);
        blurPass(blurB, blurA, 0.0f, 1.0f);
        blurPass(blurA, blurB, 2.5f, 0.0f);
        blurPass(blurB, blurA, 0.0f, 2.5f);
    }
    else
    {
        BeginTextureMode(blurA);
        ClearBackground(BLACK);
        EndTextureMode();
    }

    // 3. Combine everything on the screen.
    const float bloomStrength = bloomEnabled ? effects.bloomStrength : 0.0f;
    const float crt = effects.crt ? 1.0f : 0.0f;
    const float resolution[2] = {static_cast<float>(width), static_cast<float>(height)};
    const float flash[4] = {effects.flashColor.r / 255.0f, effects.flashColor.g / 255.0f,
                            effects.flashColor.b / 255.0f, effects.flashColor.a / 255.0f};
    BeginShaderMode(compositeShader);
    SetShaderValueTexture(compositeShader, bloomTextureLoc, blurA.texture);
    SetShaderValue(compositeShader, bloomStrengthLoc, &bloomStrength, SHADER_UNIFORM_FLOAT);
    SetShaderValue(compositeShader, saturationLoc, &effects.saturation, SHADER_UNIFORM_FLOAT);
    SetShaderValue(compositeShader, crtLoc, &crt, SHADER_UNIFORM_FLOAT);
    SetShaderValue(compositeShader, timeLoc, &effects.time, SHADER_UNIFORM_FLOAT);
    SetShaderValue(compositeShader, dangerLoc, &effects.danger, SHADER_UNIFORM_FLOAT);
    SetShaderValue(compositeShader, freezeLoc, &effects.freeze, SHADER_UNIFORM_FLOAT);
    SetShaderValue(compositeShader, resolutionLoc, resolution, SHADER_UNIFORM_VEC2);
    SetShaderValue(compositeShader, flashLoc, flash, SHADER_UNIFORM_VEC4);
    drawFullscreen(sceneTarget, width, height);
    EndShaderMode();
}
