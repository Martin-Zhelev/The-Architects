// widgets.cpp
// Fonts, text, panels, icons and the menu widgets (buttons, sliders,
// steppers, toggles, text fields) plus toast messages.
//
// The widgets are "immediate mode": a screen simply calls button(...) every
// frame and gets back true when it was clicked. Every widget gets a number in
// drawing order, so the keyboard / gamepad can move a focus highlight with
// the arrow keys and activate it with Enter.

#include "app.h"

#include "embedded_fonts.h"

#include <cmath>
#include <cstring>

UiState ui;

namespace
{

Font fonts[3];

struct Toast
{
    std::string title;
    std::string text;
    Color color;
    float age;
};
std::vector<Toast> toasts;

// raylib wants triangles in counter-clockwise order; this accepts any order.
void triangle(Vector2 a, Vector2 b, Vector2 c, Color color)
{
    const float cross = (b.x - a.x) * (c.y - a.y) - (b.y - a.y) * (c.x - a.x);
    if (cross < 0.0f)
    {
        DrawTriangle(a, b, c, color);
    }
    else
    {
        DrawTriangle(a, c, b, color);
    }
}

Font loadEmbedded(const unsigned char* data, int size, int fontSize)
{
    Font font = LoadFontFromMemory(".ttf", data, size, fontSize, nullptr, 0);
    GenTextureMipmaps(&font.texture);
    SetTextureFilter(font.texture, TEXTURE_FILTER_TRILINEAR);
    return font;
}

float spacingFor(FontKind kind, float size)
{
    return kind == FontKind::Display ? size * 0.06f : (kind == FontKind::Mono ? 0.0f : size * 0.02f);
}

bool mouseOver(Rectangle rect)
{
    return CheckCollisionPointRec(GetMousePosition(), rect);
}

// Shared focus / hover logic for every widget.
bool claimWidget(Rectangle rect, bool enabled, bool& hovered)
{
    const int id = ui.widgetCount++;
    hovered = enabled && mouseOver(rect);
    if (hovered && ui.mouseUsed && ui.focus != id)
    {
        ui.focus = id;
        playSfx(Sfx::MenuMove, 1.0f, 0.5f);
    }
    return ui.focus == id;
}

} // namespace

// ---------------------------------------------------------------------------
// Fonts and text
// ---------------------------------------------------------------------------

void loadFonts()
{
    fonts[static_cast<int>(FontKind::Display)] = loadEmbedded(FONT_DISPLAY_DATA, FONT_DISPLAY_SIZE, 96);
    fonts[static_cast<int>(FontKind::Body)] = loadEmbedded(FONT_BODY_DATA, FONT_BODY_SIZE, 64);
    fonts[static_cast<int>(FontKind::Mono)] = loadEmbedded(FONT_MONO_DATA, FONT_MONO_SIZE, 48);
}

void unloadFonts()
{
    for (Font& font : fonts)
    {
        UnloadFont(font);
    }
}

Font getFont(FontKind kind)
{
    return fonts[static_cast<int>(kind)];
}

float px(float value)
{
    return value * ui.scale;
}

Vector2 measure(FontKind kind, const char* text, float size)
{
    return MeasureTextEx(getFont(kind), text, size, spacingFor(kind, size));
}

void drawText(FontKind kind, const char* text, float x, float y, float size, Color color)
{
    DrawTextEx(getFont(kind), text, Vector2{std::round(x), std::round(y)}, size, spacingFor(kind, size), color);
}

void drawTextCentered(FontKind kind, const char* text, float centerX, float y, float size, Color color)
{
    drawText(kind, text, centerX - measure(kind, text, size).x * 0.5f, y, size, color);
}

void drawTextRight(FontKind kind, const char* text, float rightX, float y, float size, Color color)
{
    drawText(kind, text, rightX - measure(kind, text, size).x, y, size, color);
}

namespace
{
// Draws text with a clean outline: copies in a circle around the text (not a
// diagonal smear), then a soft drop shadow, then the text itself.
void drawOutlined(Font font, const char* text, Vector2 at, float size, float spacing, Color color, Color outline,
                  float outlineWidth, float softAlpha)
{
    const int directions = 16;
    // Soft outer halo
    if (softAlpha > 0.0f)
    {
        for (int i = 0; i < directions; ++i)
        {
            const float angle = static_cast<float>(i) * 2.0f * PI / static_cast<float>(directions);
            const Vector2 offset{std::cos(angle) * outlineWidth * 2.2f, std::sin(angle) * outlineWidth * 2.2f};
            DrawTextEx(font, text, Vector2Add(at, offset), size, spacing, withAlpha(outline, softAlpha));
        }
    }
    // Drop shadow
    DrawTextEx(font, text, Vector2{at.x, at.y + size * 0.06f}, size, spacing, Color{0, 0, 0, 150});
    // Tight outline
    for (int i = 0; i < directions; ++i)
    {
        const float angle = static_cast<float>(i) * 2.0f * PI / static_cast<float>(directions);
        const Vector2 offset{std::cos(angle) * outlineWidth, std::sin(angle) * outlineWidth};
        DrawTextEx(font, text, Vector2Add(at, offset), size, spacing, outline);
    }
    DrawTextEx(font, text, at, size, spacing, color);
}
} // namespace

void drawGlowText(FontKind kind, const char* text, float centerX, float y, float size, Color color, Color glow)
{
    const float x = std::round(centerX - measure(kind, text, size).x * 0.5f);
    // A dark, tinted outline keeps the letters crisp; the glow colour is a
    // faint halo around them instead of copies smeared in every direction.
    const Color outline = lerpColor(glow, Color{0, 0, 0, 255}, 0.65f);
    drawOutlined(getFont(kind), text, Vector2{x, std::round(y)}, size, spacingFor(kind, size), color, outline,
                 std::fmax(1.0f, size * 0.035f), 0.10f * static_cast<float>(glow.a) / 255.0f);
}

// The big title: one smooth gradient that slowly flows through the theme's
// synthwave colours (pink - violet - cyan), letters gently bobbing.
void drawRainbowTitle(const char* text, float centerX, float y, float size, float time)
{
    const Font font = getFont(FontKind::Display);
    const float spacing = spacingFor(FontKind::Display, size);
    float x = centerX - measure(FontKind::Display, text, size).x * 0.5f;
    const Color stops[3] = {{255, 90, 200, 255}, {170, 110, 255, 255}, {70, 220, 255, 255}};
    const int count = static_cast<int>(std::strlen(text));
    char letter[2] = {0, 0};
    for (int i = 0; text[i] != '\0'; ++i)
    {
        letter[0] = text[i];
        const float bob = std::sin(time * 2.2f + static_cast<float>(i) * 0.5f) * size * 0.035f;
        // position along the word plus a slow drift, mapped onto pink -> violet -> cyan -> violet -> pink
        float t = (count > 1 ? static_cast<float>(i) / static_cast<float>(count - 1) : 0.0f) + time * 0.08f;
        t = t - std::floor(t);
        const float wave = t < 0.5f ? t * 2.0f : (1.0f - t) * 2.0f; // 0 -> 1 -> 0
        const Color color = wave < 0.5f ? lerpColor(stops[0], stops[1], wave * 2.0f)
                                        : lerpColor(stops[1], stops[2], (wave - 0.5f) * 2.0f);
        const Color outline = lerpColor(color, Color{10, 0, 25, 255}, 0.75f);
        drawOutlined(font, letter, Vector2{std::round(x), std::round(y + bob)}, size, 0, color, outline,
                     std::fmax(1.5f, size * 0.04f), 0.10f);
        x += MeasureTextEx(font, letter, size, 0).x + spacing;
    }
}

// ---------------------------------------------------------------------------
// Panels and shapes
// ---------------------------------------------------------------------------

namespace
{
// A rounded band that hugs a rounded rectangle exactly: `from` and `to` are
// distances from the rectangle's edge (negative = inside), so the border and
// the glow always share the fill's corners - no gaps, no mismatched curves.
void drawRoundedBand(Rectangle rect, float radius, float from, float to, Color color)
{
    if (color.a == 0 || to <= from)
    {
        return;
    }
    const float inner = radius + from;
    const float outer = radius + to;
    const int segments = 10;
    const Vector2 corners[4] = {{rect.x + radius, rect.y + radius},
                                {rect.x + rect.width - radius, rect.y + radius},
                                {rect.x + rect.width - radius, rect.y + rect.height - radius},
                                {rect.x + radius, rect.y + rect.height - radius}};
    const float startAngles[4] = {180.0f, 270.0f, 0.0f, 90.0f};
    for (int i = 0; i < 4; ++i)
    {
        DrawRing(corners[i], std::fmax(0.0f, inner), outer, startAngles[i], startAngles[i] + 90.0f, segments, color);
    }
    const float thickness = to - from;
    const float straightW = rect.width - 2.0f * radius;
    const float straightH = rect.height - 2.0f * radius;
    DrawRectangleRec(Rectangle{rect.x + radius, rect.y - to, straightW, thickness}, color);                 // top
    DrawRectangleRec(Rectangle{rect.x + radius, rect.y + rect.height + from, straightW, thickness}, color); // bottom
    DrawRectangleRec(Rectangle{rect.x - to, rect.y + radius, thickness, straightH}, color);                 // left
    DrawRectangleRec(Rectangle{rect.x + rect.width + from, rect.y + radius, thickness, straightH}, color);  // right
}
} // namespace

void drawPanel(Rectangle rect, Color fill, Color border, float glow)
{
    const float radius = std::fmin(px(10.0f), std::fmin(rect.width, rect.height) * 0.5f);
    const float minSide = std::fmin(rect.width, rect.height);
    const float roundness = minSide > 0.0f ? 2.0f * radius / minSide : 0.0f;
    // Soft glow outside, fading out in touching bands.
    const float band = px(2.0f);
    for (int i = 0; i < 3; ++i)
    {
        drawRoundedBand(rect, radius, band * static_cast<float>(i), band * static_cast<float>(i + 1),
                        withAlpha(border, (0.16f - 0.05f * static_cast<float>(i)) * glow));
    }
    DrawRectangleRounded(rect, roundness, 10, fill);
    // The border sits exactly on the edge of the fill.
    const float width = std::fmax(1.0f, px(2.0f));
    drawRoundedBand(rect, radius, -width, 0.0f, withAlpha(border, 0.95f));
}

// A vertical gradient with rounded corners, drawn as thin horizontal strips
// that get narrower inside the corners (raylib's own gradients are square).
void drawRoundedGradientV(Rectangle rect, float radius, Color top, Color bottom)
{
    radius = std::fmin(radius, std::fmin(rect.width, rect.height) * 0.5f);
    const int rows = static_cast<int>(std::ceil(rect.height));
    for (int i = 0; i < rows; ++i)
    {
        const float y = rect.y + static_cast<float>(i);
        const float h = std::fmin(1.0f, rect.y + rect.height - y);
        const float middle = static_cast<float>(i) + 0.5f;
        float inset = 0.0f;
        const float fromEdge = std::fmin(middle, rect.height - middle); // distance to the top or bottom edge
        if (fromEdge < radius)
        {
            const float d = radius - fromEdge;
            inset = radius - std::sqrt(std::fmax(0.0f, radius * radius - d * d));
        }
        const float t = rows > 1 ? static_cast<float>(i) / static_cast<float>(rows - 1) : 0.0f;
        DrawRectangleRec(Rectangle{rect.x + inset, y, rect.width - 2.0f * inset, h}, lerpColor(top, bottom, t));
    }
}

// A small rounded chip in the bottom-right corner: green above 50 FPS,
// yellow above 30, red below.
void drawFpsCounter()
{
    const int fps = GetFPS();
    const Color color = fps >= 50 ? Color{90, 255, 150, 255} : fps >= 30 ? Color{255, 215, 60, 255} : Color{255, 90, 80, 255};
    const std::string text = std::to_string(fps) + " FPS";
    const float size = px(18);
    const Vector2 measured = measure(FontKind::Display, text.c_str(), size);
    const Rectangle chip{static_cast<float>(ui.width) - measured.x - px(40), static_cast<float>(ui.height) - px(44),
                         measured.x + px(24), px(32)};
    drawPanel(chip, Color{0, 0, 0, 190}, color, 0.5f);
    drawText(FontKind::Display, text.c_str(), chip.x + px(12), chip.y + (chip.height - size) * 0.5f, size, color);
}

void drawGradientPanel(Rectangle rect, Color top, Color bottom, Color border)
{
    const float radius = std::fmin(px(10.0f), std::fmin(rect.width, rect.height) * 0.5f);
    drawRoundedGradientV(rect, radius, top, bottom);
    drawRoundedBand(rect, radius, -std::fmax(1.0f, px(2.0f)), 0.0f, border);
}

void drawProgressBar(Rectangle rect, float fraction, Color fill, Color back)
{
    fraction = fraction < 0.0f ? 0.0f : (fraction > 1.0f ? 1.0f : fraction);
    DrawRectangleRounded(rect, 0.5f, 6, back);
    if (fraction > 0.0f)
    {
        DrawRectangleRounded(Rectangle{rect.x, rect.y, rect.width * fraction, rect.height}, 0.5f, 6, fill);
    }
}

void drawStar(Vector2 center, float radius, Color color, float rotation)
{
    Vector2 points[10];
    for (int i = 0; i < 10; ++i)
    {
        const float angle = rotation - PI / 2 + static_cast<float>(i) * PI / 5.0f;
        const float r = (i % 2 == 0) ? radius : radius * 0.45f;
        points[i] = Vector2{center.x + std::cos(angle) * r, center.y + std::sin(angle) * r};
    }
    for (int i = 0; i < 10; ++i)
    {
        triangle(center, points[i], points[(i + 1) % 10], color);
    }
}

void drawHeart(Vector2 center, float size, Color color)
{
    const float r = size * 0.27f;
    DrawCircleV(Vector2{center.x - r * 0.95f, center.y - r * 0.5f}, r, color);
    DrawCircleV(Vector2{center.x + r * 0.95f, center.y - r * 0.5f}, r, color);
    triangle(Vector2{center.x - r * 1.95f, center.y - r * 0.25f}, Vector2{center.x + r * 1.95f, center.y - r * 0.25f},
             Vector2{center.x, center.y + size * 0.48f}, color);
    DrawCircleV(Vector2{center.x - r * 1.0f, center.y - r * 0.8f}, r * 0.3f, withAlpha(WHITE, 0.5f));
}

void drawKeyIcon(Vector2 center, float size, Color color)
{
    const float s = size * 0.5f;
    DrawRing(Vector2{center.x - s * 0.45f, center.y}, s * 0.22f, s * 0.42f, 0, 360, 24, color);
    DrawRectangleRec(Rectangle{center.x - s * 0.08f, center.y - s * 0.09f, s * 0.95f, s * 0.18f}, color);
    DrawRectangleRec(Rectangle{center.x + s * 0.55f, center.y, s * 0.14f, s * 0.35f}, color);
    DrawRectangleRec(Rectangle{center.x + s * 0.28f, center.y, s * 0.12f, s * 0.25f}, color);
}

void drawMinotaurIcon(Vector2 center, float size, Color color)
{
    const float s = size * 0.5f;
    triangle(Vector2{center.x - s * 0.45f, center.y - s * 0.2f}, Vector2{center.x - s * 0.95f, center.y - s * 0.85f},
             Vector2{center.x - s * 0.25f, center.y - s * 0.45f}, Color{240, 230, 200, 255});
    triangle(Vector2{center.x + s * 0.45f, center.y - s * 0.2f}, Vector2{center.x + s * 0.95f, center.y - s * 0.85f},
             Vector2{center.x + s * 0.25f, center.y - s * 0.45f}, Color{240, 230, 200, 255});
    DrawCircleV(center, s * 0.55f, color);
    DrawEllipse(static_cast<int>(center.x), static_cast<int>(center.y + s * 0.3f), s * 0.32f, s * 0.22f,
                Color{60, 30, 20, 255});
    DrawCircleV(Vector2{center.x - s * 0.22f, center.y - s * 0.1f}, s * 0.1f, Color{255, 40, 30, 255});
    DrawCircleV(Vector2{center.x + s * 0.22f, center.y - s * 0.1f}, s * 0.1f, Color{255, 40, 30, 255});
}

void drawExitIcon(Vector2 center, float size, Color color, float time)
{
    const float s = size * 0.5f;
    const float pulse = 0.85f + 0.15f * std::sin(time * 4.0f);
    DrawRectangleRounded(Rectangle{center.x - s * 0.55f, center.y - s * 0.8f, s * 1.1f, s * 1.6f}, 0.4f, 6,
                         withAlpha(color, 0.35f));
    DrawRectangleRoundedLinesEx(Rectangle{center.x - s * 0.55f, center.y - s * 0.8f, s * 1.1f, s * 1.6f}, 0.4f, 6,
                                px(2.0f), color);
    triangle(Vector2{center.x - s * 0.3f * pulse, center.y + s * 0.1f}, Vector2{center.x + s * 0.3f * pulse, center.y + s * 0.1f},
             Vector2{center.x, center.y - s * 0.45f * pulse}, color);
    DrawRectangleRec(Rectangle{center.x - s * 0.1f, center.y + s * 0.05f, s * 0.2f, s * 0.4f}, color);
}

void drawTrophy(Vector2 center, float size, Color color)
{
    const float s = size * 0.5f;
    DrawRing(Vector2{center.x - s * 0.55f, center.y - s * 0.35f}, s * 0.18f, s * 0.3f, 90, 270, 12, color);
    DrawRing(Vector2{center.x + s * 0.55f, center.y - s * 0.35f}, s * 0.18f, s * 0.3f, -90, 90, 12, color);
    DrawCircleSector(Vector2{center.x, center.y - s * 0.55f}, s * 0.62f, 0, 180, 20, color);
    DrawRectangleRec(Rectangle{center.x - s * 0.1f, center.y, s * 0.2f, s * 0.45f}, color);
    DrawRectangleRec(Rectangle{center.x - s * 0.42f, center.y + s * 0.42f, s * 0.84f, s * 0.2f}, color);
    drawStar(Vector2{center.x, center.y - s * 0.3f}, s * 0.2f, withAlpha(WHITE, 0.7f));
}

void drawLock(Vector2 center, float size, Color color)
{
    const float s = size * 0.5f;
    DrawRing(Vector2{center.x, center.y - s * 0.15f}, s * 0.28f, s * 0.42f, 180, 360, 16, color);
    DrawRectangleRounded(Rectangle{center.x - s * 0.55f, center.y - s * 0.15f, s * 1.1f, s * 0.85f}, 0.25f, 6, color);
    DrawCircleV(Vector2{center.x, center.y + s * 0.2f}, s * 0.12f, Color{0, 0, 0, 160});
}

const char* itemName(Item item)
{
    switch (item)
    {
    case Item::Gem:
        return "Gem";
    case Item::Torch:
        return "Torch";
    case Item::Boots:
        return "Speed Boots";
    case Item::Hammer:
        return "Hammer";
    case Item::Hourglass:
        return "Hourglass";
    case Item::Heart:
        return "Heart";
    case Item::Compass:
        return "Compass";
    case Item::Spikes:
        return "Spike Trap";
    case Item::Teleporter:
        return "Teleporter";
    case Item::None:
        break;
    }
    return "";
}

const char* itemDescription(Item item)
{
    switch (item)
    {
    case Item::Gem:
        return "+50 points. Some exits stay sealed until you have them all.";
    case Item::Torch:
        return "Lights up more of the fog around you.";
    case Item::Boots:
        return "Run twice as fast for 12 seconds.";
    case Item::Hammer:
        return "2 charges. Press SPACE to smash the wall in front of you.";
    case Item::Hourglass:
        return "Freezes every Minotaur for 8 seconds.";
    case Item::Heart:
        return "One extra life.";
    case Item::Compass:
        return "Points to the exit for 20 seconds.";
    case Item::Spikes:
        return "Rises and falls. Step on it when it is down.";
    case Item::Teleporter:
        return "Step on a pad to jump to its twin.";
    case Item::None:
        break;
    }
    return "";
}

Color itemColor(Item item)
{
    switch (item)
    {
    case Item::Gem:
        return Color{60, 240, 255, 255};
    case Item::Torch:
        return Color{255, 160, 40, 255};
    case Item::Boots:
        return Color{60, 220, 255, 255};
    case Item::Hammer:
        return Color{200, 210, 230, 255};
    case Item::Hourglass:
        return Color{255, 215, 120, 255};
    case Item::Heart:
        return Color{255, 60, 90, 255};
    case Item::Compass:
        return Color{120, 255, 160, 255};
    case Item::Spikes:
        return Color{200, 200, 210, 255};
    case Item::Teleporter:
        return Color{190, 100, 255, 255};
    case Item::None:
        break;
    }
    return WHITE;
}

void drawItemIcon(Item item, Vector2 c, float size, float time)
{
    const float s = size * 0.5f;
    const Color color = itemColor(item);
    switch (item)
    {
    case Item::Gem:
        triangle(Vector2{c.x, c.y - s * 0.8f}, Vector2{c.x - s * 0.6f, c.y - s * 0.15f}, Vector2{c.x, c.y - s * 0.15f},
                 lerpColor(color, WHITE, 0.4f));
        triangle(Vector2{c.x, c.y - s * 0.8f}, Vector2{c.x + s * 0.6f, c.y - s * 0.15f}, Vector2{c.x, c.y - s * 0.15f},
                 color);
        triangle(Vector2{c.x - s * 0.6f, c.y - s * 0.15f}, Vector2{c.x, c.y + s * 0.8f}, Vector2{c.x, c.y - s * 0.15f},
                 lerpColor(color, BLACK, 0.2f));
        triangle(Vector2{c.x + s * 0.6f, c.y - s * 0.15f}, Vector2{c.x, c.y + s * 0.8f}, Vector2{c.x, c.y - s * 0.15f},
                 lerpColor(color, BLACK, 0.4f));
        break;
    case Item::Torch:
    {
        const float flicker = 1.0f + 0.12f * std::sin(time * 15.0f);
        DrawRectangleRec(Rectangle{c.x - s * 0.1f, c.y - s * 0.1f, s * 0.2f, s * 0.85f}, Color{140, 90, 50, 255});
        DrawCircleV(Vector2{c.x, c.y - s * 0.3f}, s * 0.38f * flicker, Color{255, 120, 20, 255});
        DrawCircleV(Vector2{c.x, c.y - s * 0.25f}, s * 0.22f * flicker, Color{255, 230, 120, 255});
        break;
    }
    case Item::Boots:
        DrawRectangleRounded(Rectangle{c.x - s * 0.35f, c.y - s * 0.7f, s * 0.4f, s * 1.1f}, 0.3f, 4, color);
        DrawRectangleRounded(Rectangle{c.x - s * 0.35f, c.y + s * 0.15f, s * 0.95f, s * 0.35f}, 0.4f, 4, color);
        triangle(Vector2{c.x - s * 0.35f, c.y - s * 0.4f}, Vector2{c.x - s * 0.95f, c.y - s * 0.7f},
                 Vector2{c.x - s * 0.35f, c.y - s * 0.1f}, Color{255, 230, 120, 255});
        break;
    case Item::Hammer:
        DrawRectanglePro(Rectangle{c.x, c.y, s * 0.18f, s * 1.3f}, Vector2{s * 0.09f, s * 0.4f}, 35.0f,
                         Color{150, 100, 60, 255});
        DrawRectanglePro(Rectangle{c.x - s * 0.25f, c.y - s * 0.35f, s * 0.9f, s * 0.42f}, Vector2{s * 0.45f, s * 0.21f},
                         35.0f, color);
        break;
    case Item::Hourglass:
        triangle(Vector2{c.x - s * 0.5f, c.y - s * 0.7f}, Vector2{c.x + s * 0.5f, c.y - s * 0.7f}, Vector2{c.x, c.y}, color);
        triangle(Vector2{c.x - s * 0.5f, c.y + s * 0.7f}, Vector2{c.x + s * 0.5f, c.y + s * 0.7f}, Vector2{c.x, c.y}, color);
        DrawRectangleRec(Rectangle{c.x - s * 0.6f, c.y - s * 0.85f, s * 1.2f, s * 0.15f}, Color{200, 150, 80, 255});
        DrawRectangleRec(Rectangle{c.x - s * 0.6f, c.y + s * 0.7f, s * 1.2f, s * 0.15f}, Color{200, 150, 80, 255});
        break;
    case Item::Heart:
        drawHeart(c, size * 0.9f, color);
        break;
    case Item::Compass:
    {
        DrawRing(c, s * 0.6f, s * 0.78f, 0, 360, 32, color);
        const float angle = time * 1.5f;
        const Vector2 tip{c.x + std::sin(angle) * s * 0.55f, c.y - std::cos(angle) * s * 0.55f};
        const Vector2 tail{c.x - std::sin(angle) * s * 0.55f, c.y + std::cos(angle) * s * 0.55f};
        const Vector2 side{std::cos(angle) * s * 0.14f, std::sin(angle) * s * 0.14f};
        triangle(tip, Vector2Add(c, side), Vector2Subtract(c, side), Color{255, 60, 60, 255});
        triangle(tail, Vector2Add(c, side), Vector2Subtract(c, side), WHITE);
        break;
    }
    case Item::Spikes:
        for (int i = -1; i <= 1; ++i)
        {
            const float x = c.x + static_cast<float>(i) * s * 0.5f;
            triangle(Vector2{x - s * 0.25f, c.y + s * 0.5f}, Vector2{x + s * 0.25f, c.y + s * 0.5f},
                     Vector2{x, c.y - s * 0.6f}, color);
        }
        DrawRectangleRec(Rectangle{c.x - s * 0.85f, c.y + s * 0.5f, s * 1.7f, s * 0.2f}, Color{90, 90, 100, 255});
        break;
    case Item::Teleporter:
        for (int ring = 0; ring < 3; ++ring)
        {
            const float r = s * (0.3f + 0.25f * static_cast<float>(ring));
            const float start = time * 120.0f * (ring % 2 == 0 ? 1.0f : -1.0f) + ring * 40.0f;
            DrawRing(c, r - s * 0.08f, r, start, start + 270.0f, 24, withAlpha(color, 1.0f - 0.25f * ring));
        }
        break;
    case Item::None:
        break;
    }
}

// ---------------------------------------------------------------------------
// Widgets
// ---------------------------------------------------------------------------

void setUiStyle(Color accent, Color accent2, Color panel)
{
    ui.width = GetScreenWidth();
    ui.height = GetScreenHeight();
    ui.scale = std::fmin(static_cast<float>(ui.width) / 1280.0f, static_cast<float>(ui.height) / 720.0f);
    ui.accent = accent;
    ui.accent2 = accent2;
    ui.panel = panel;
}

void beginUi(Color accent, Color accent2, Color panel)
{
    setUiStyle(accent, accent2, panel);
    ui.widgetCount = 0;

    const Vector2 delta = GetMouseDelta();
    if (delta.x != 0.0f || delta.y != 0.0f || IsMouseButtonPressed(MOUSE_BUTTON_LEFT))
    {
        ui.mouseUsed = true;
    }

    const bool pad = IsGamepadAvailable(0);
    ui.navUp = IsKeyPressed(KEY_UP) || (pad && IsGamepadButtonPressed(0, GAMEPAD_BUTTON_LEFT_FACE_UP));
    ui.navDown = IsKeyPressed(KEY_DOWN) || IsKeyPressed(KEY_TAB) ||
                 (pad && IsGamepadButtonPressed(0, GAMEPAD_BUTTON_LEFT_FACE_DOWN));
    ui.navLeft = IsKeyPressed(KEY_LEFT) || (pad && IsGamepadButtonPressed(0, GAMEPAD_BUTTON_LEFT_FACE_LEFT));
    ui.navRight = IsKeyPressed(KEY_RIGHT) || (pad && IsGamepadButtonPressed(0, GAMEPAD_BUTTON_LEFT_FACE_RIGHT));
    ui.activate = IsKeyPressed(KEY_ENTER) || IsKeyPressed(KEY_KP_ENTER) ||
                  (pad && IsGamepadButtonPressed(0, GAMEPAD_BUTTON_RIGHT_FACE_DOWN));
    ui.back = IsKeyPressed(KEY_ESCAPE) || (pad && IsGamepadButtonPressed(0, GAMEPAD_BUTTON_RIGHT_FACE_RIGHT));

    if (ui.previousWidgetCount > 0 && (ui.navUp || ui.navDown))
    {
        ui.mouseUsed = false;
        ui.focus = (ui.focus + (ui.navDown ? 1 : -1) + ui.previousWidgetCount) % ui.previousWidgetCount;
        playSfx(Sfx::MenuMove);
    }
}

void endUi()
{
    if (ui.widgetCount == 0)
    {
        return; // a drawing-only pass: keep the focus of the real menu
    }
    ui.previousWidgetCount = ui.widgetCount;
    if (ui.focus >= ui.widgetCount)
    {
        ui.focus = ui.widgetCount - 1;
    }
}

void resetFocus()
{
    ui.focus = 0;
    ui.previousWidgetCount = 0;
    ui.activeField = nullptr;
}

bool isFocused(int widgetIndex)
{
    return ui.focus == widgetIndex;
}

bool button(const char* label, Rectangle rect, bool enabled, const char* subtitle)
{
    bool hovered = false;
    const bool focused = claimWidget(rect, enabled, hovered);
    const float pulse = 0.5f + 0.5f * std::sin(ui.time * 6.0f);
    Rectangle drawn = rect;
    if (focused && enabled)
    {
        const float grow = px(3.0f + 2.0f * pulse);
        drawn = Rectangle{rect.x - grow, rect.y - grow * 0.5f, rect.width + grow * 2, rect.height + grow};
    }
    const Color fill = !enabled ? Color{40, 40, 50, 180}
                       : focused ? lerpColor(ui.accent, ui.accent2, 0.5f + 0.5f * std::sin(ui.time * 2.0f))
                                 : ui.panel;
    drawPanel(drawn, fill, focused ? WHITE : ui.accent, focused ? 2.0f : 1.0f);

    const float size = subtitle != nullptr ? drawn.height * 0.42f : drawn.height * 0.5f;
    // Long labels shrink to fit the button instead of spilling over its edges
    // (the subtitle keeps its place and size).
    float labelSize = size;
    const float labelWidth = measure(FontKind::Body, label, size).x;
    const float room = drawn.width - px(44); // room for a lock icon on either side
    if (labelWidth > room && labelWidth > 0.0f)
    {
        labelSize = size * std::fmax(0.6f, room / labelWidth);
    }
    const Color textColor = !enabled ? Color{120, 120, 130, 255} : (focused ? Color{20, 10, 30, 255} : WHITE);
    const float textY = subtitle != nullptr ? drawn.y + drawn.height * 0.1f : drawn.y + (drawn.height - size) * 0.5f;
    drawTextCentered(FontKind::Body, label, drawn.x + drawn.width * 0.5f, textY + (size - labelSize) * 0.5f, labelSize,
                     textColor);
    if (subtitle != nullptr)
    {
        drawTextCentered(FontKind::Body, subtitle, drawn.x + drawn.width * 0.5f, textY + size * 1.0f, size * 0.62f,
                         withAlpha(textColor, 0.8f));
    }

    const bool clicked = hovered && IsMouseButtonReleased(MOUSE_BUTTON_LEFT);
    const bool activated = enabled && (clicked || (focused && ui.activate));
    if (activated)
    {
        playSfx(Sfx::MenuSelect);
    }
    return activated;
}

bool bigButton(const char* label, Rectangle rect, Color color, bool enabled)
{
    const Color savedAccent = ui.accent;
    ui.accent = color;
    const bool result = button(label, rect, enabled);
    ui.accent = savedAccent;
    return result;
}

bool slider(const char* label, Rectangle rect, int& value, int minValue, int maxValue, const char* suffix)
{
    bool hovered = false;
    const bool focused = claimWidget(rect, true, hovered);
    drawPanel(rect, focused ? withAlpha(ui.accent, 0.35f) : ui.panel, focused ? WHITE : ui.accent);
    const float size = rect.height * 0.45f;
    drawText(FontKind::Body, label, rect.x + px(14), rect.y + (rect.height - size) * 0.5f, size, WHITE);

    const Rectangle track{rect.x + rect.width * 0.45f, rect.y + rect.height * 0.42f, rect.width * 0.4f,
                          rect.height * 0.16f};
    const float fraction = static_cast<float>(value - minValue) / static_cast<float>(maxValue - minValue);
    drawProgressBar(track, fraction, ui.accent2, Color{0, 0, 0, 150});
    DrawCircleV(Vector2{track.x + track.width * fraction, track.y + track.height * 0.5f}, rect.height * 0.2f, WHITE);
    const std::string text = std::to_string(value) + suffix;
    drawTextRight(FontKind::Body, text.c_str(), rect.x + rect.width - px(12), rect.y + (rect.height - size) * 0.5f,
                  size, ui.accent2);

    const int before = value;
    const Rectangle hit{track.x - px(10), rect.y, track.width + px(20), rect.height};
    if (IsMouseButtonDown(MOUSE_BUTTON_LEFT) && CheckCollisionPointRec(GetMousePosition(), hit))
    {
        const float t = (GetMousePosition().x - track.x) / track.width;
        value = minValue + static_cast<int>(std::round(std::fmax(0.0f, std::fmin(1.0f, t)) * (maxValue - minValue)));
    }
    if (focused)
    {
        const int step = (maxValue - minValue) > 20 ? 5 : 1;
        if (ui.navLeft)
        {
            value -= step;
        }
        if (ui.navRight)
        {
            value += step;
        }
    }
    value = value < minValue ? minValue : (value > maxValue ? maxValue : value);
    if (value != before)
    {
        playSfx(Sfx::Tick);
    }
    return value != before;
}

bool stepper(const char* label, Rectangle rect, int& value, int minValue, int maxValue, const char* const* names)
{
    bool hovered = false;
    const bool focused = claimWidget(rect, true, hovered);
    drawPanel(rect, focused ? withAlpha(ui.accent, 0.35f) : ui.panel, focused ? WHITE : ui.accent);
    const float size = rect.height * 0.45f;
    drawText(FontKind::Body, label, rect.x + px(14), rect.y + (rect.height - size) * 0.5f, size, WHITE);

    const float arrowWidth = rect.height * 0.9f;
    const Rectangle leftArrow{rect.x + rect.width * 0.5f, rect.y + rect.height * 0.1f, arrowWidth, rect.height * 0.8f};
    const Rectangle rightArrow{rect.x + rect.width - arrowWidth - px(8), leftArrow.y, arrowWidth, leftArrow.height};
    const std::string text = names != nullptr ? std::string(names[value - minValue]) : std::to_string(value);
    drawTextCentered(FontKind::Body, text.c_str(), (leftArrow.x + leftArrow.width + rightArrow.x) * 0.5f,
                     rect.y + (rect.height - size) * 0.5f, size, ui.accent2);
    const bool overLeft = CheckCollisionPointRec(GetMousePosition(), leftArrow);
    const bool overRight = CheckCollisionPointRec(GetMousePosition(), rightArrow);
    drawTextCentered(FontKind::Display, "<", leftArrow.x + arrowWidth * 0.5f, rect.y + (rect.height - size) * 0.5f, size,
                     overLeft ? WHITE : ui.accent);
    drawTextCentered(FontKind::Display, ">", rightArrow.x + arrowWidth * 0.5f, rect.y + (rect.height - size) * 0.5f,
                     size, overRight ? WHITE : ui.accent);

    const int before = value;
    if ((focused && ui.navLeft) || (overLeft && IsMouseButtonPressed(MOUSE_BUTTON_LEFT)))
    {
        value = value <= minValue ? maxValue : value - 1;
    }
    if ((focused && (ui.navRight || ui.activate)) || (overRight && IsMouseButtonPressed(MOUSE_BUTTON_LEFT)))
    {
        value = value >= maxValue ? minValue : value + 1;
    }
    if (value != before)
    {
        playSfx(Sfx::Tick);
    }
    return value != before;
}

bool toggle(const char* label, Rectangle rect, bool& value)
{
    bool hovered = false;
    const bool focused = claimWidget(rect, true, hovered);
    drawPanel(rect, focused ? withAlpha(ui.accent, 0.35f) : ui.panel, focused ? WHITE : ui.accent);
    const float size = rect.height * 0.45f;
    drawText(FontKind::Body, label, rect.x + px(14), rect.y + (rect.height - size) * 0.5f, size, WHITE);

    const Rectangle knobTrack{rect.x + rect.width - rect.height * 1.7f - px(10), rect.y + rect.height * 0.22f,
                              rect.height * 1.7f, rect.height * 0.56f};
    DrawRectangleRounded(knobTrack, 1.0f, 12, value ? ui.accent2 : Color{60, 60, 70, 255});
    const float knobX = value ? knobTrack.x + knobTrack.width - knobTrack.height * 0.5f
                              : knobTrack.x + knobTrack.height * 0.5f;
    DrawCircleV(Vector2{knobX, knobTrack.y + knobTrack.height * 0.5f}, knobTrack.height * 0.42f, WHITE);
    drawTextRight(FontKind::Body, value ? "ON" : "OFF", knobTrack.x - px(10), rect.y + (rect.height - size) * 0.5f,
                  size * 0.8f, value ? ui.accent2 : GRAY);

    const bool clicked = hovered && IsMouseButtonReleased(MOUSE_BUTTON_LEFT);
    if (clicked || (focused && (ui.activate || ui.navLeft || ui.navRight)))
    {
        value = !value;
        playSfx(Sfx::MenuSelect);
        return true;
    }
    return false;
}

bool textField(const char* label, Rectangle rect, std::string& text, std::size_t maxLength, bool digitsOnly,
               const char* placeholder)
{
    bool hovered = false;
    const bool focused = claimWidget(rect, true, hovered);
    drawPanel(rect, focused ? withAlpha(ui.accent, 0.25f) : ui.panel, focused ? WHITE : ui.accent);
    const float size = rect.height * 0.45f;
    drawText(FontKind::Body, label, rect.x + px(14), rect.y + (rect.height - size) * 0.5f, size, WHITE);

    const float fieldX = rect.x + rect.width * 0.38f;
    const Rectangle field{fieldX, rect.y + rect.height * 0.15f, rect.x + rect.width - fieldX - px(10), rect.height * 0.7f};
    DrawRectangleRounded(field, 0.3f, 6, Color{0, 0, 0, 140});
    bool changed = false;

    if (focused)
    {
        // Only allowed characters get in: this is the input validation for names and seeds.
        int codepoint = GetCharPressed();
        while (codepoint > 0)
        {
            const bool isDigit = codepoint >= '0' && codepoint <= '9';
            const bool isLetter = (codepoint >= 'a' && codepoint <= 'z') || (codepoint >= 'A' && codepoint <= 'Z');
            const bool isNameSymbol = codepoint == ' ' || codepoint == '-' || codepoint == '_';
            const bool allowed = digitsOnly ? isDigit : (isDigit || isLetter || isNameSymbol);
            if (allowed && text.size() < maxLength)
            {
                text.push_back(static_cast<char>(codepoint));
                changed = true;
                playSfx(Sfx::Tick, 1.2f, 0.6f);
            }
            codepoint = GetCharPressed();
        }
        if ((IsKeyPressed(KEY_BACKSPACE) || IsKeyPressedRepeat(KEY_BACKSPACE)) && !text.empty())
        {
            text.pop_back();
            changed = true;
        }
    }

    const bool empty = text.empty();
    const std::string shown = empty ? std::string(placeholder) : text;
    drawText(FontKind::Body, shown.c_str(), field.x + px(10), rect.y + (rect.height - size) * 0.5f, size,
             empty ? Color{140, 140, 160, 200} : ui.accent2);
    if (focused && std::fmod(ui.time, 1.0f) < 0.55f)
    {
        const float caretX = field.x + px(12) + (empty ? 0.0f : measure(FontKind::Body, text.c_str(), size).x);
        DrawRectangleRec(Rectangle{caretX, field.y + field.height * 0.18f, px(3), field.height * 0.64f}, ui.accent2);
    }
    return changed;
}

// ---------------------------------------------------------------------------
// Toasts
// ---------------------------------------------------------------------------

void showToast(const std::string& title, const std::string& text, Color color)
{
    toasts.push_back(Toast{title, text, color, 0.0f});
}

void updateAndDrawToasts(float dt)
{
    const float lifetime = 3.8f;
    float y = px(16);
    for (std::size_t i = 0; i < toasts.size();)
    {
        Toast& toast = toasts[i];
        toast.age += dt;
        if (toast.age > lifetime)
        {
            toasts.erase(toasts.begin() + static_cast<long>(i));
            continue;
        }
        const float slide = std::fmin(1.0f, toast.age * 5.0f) * std::fmin(1.0f, (lifetime - toast.age) * 4.0f);
        const float width = px(420);
        const float height = px(70);
        // Top-right corner, sliding in from the side, so it never covers a
        // screen title (e.g. ESCAPED! on the results).
        const Rectangle rect{static_cast<float>(ui.width) - width - px(18) + (1.0f - slide) * (width + px(30)), y, width,
                             height};
        drawPanel(rect, Color{10, 8, 20, static_cast<unsigned char>(230 * slide)}, toast.color, 2.0f);
        drawTrophy(Vector2{rect.x + px(38), rect.y + height * 0.5f}, px(42), toast.color);
        drawText(FontKind::Display, toast.title.c_str(), rect.x + px(72), rect.y + px(10), px(18), toast.color);
        drawText(FontKind::Body, toast.text.c_str(), rect.x + px(72), rect.y + px(34), px(24), WHITE);
        y += height + px(10);
        ++i;
    }
}

float drawWrappedText(FontKind kind, const char* text, float x, float y, float width, float size, Color color)
{
    std::string line;
    std::string word;
    float cursorY = y;
    const std::string all = std::string(text) + " ";
    for (const char symbol : all)
    {
        if (symbol != ' ' && symbol != '\n')
        {
            word.push_back(symbol);
            continue;
        }
        const std::string candidate = line.empty() ? word : line + " " + word;
        if (!line.empty() && measure(kind, candidate.c_str(), size).x > width)
        {
            drawText(kind, line.c_str(), x, cursorY, size, color);
            cursorY += size * 1.1f;
            line = word;
        }
        else
        {
            line = candidate;
        }
        word.clear();
        if (symbol == '\n')
        {
            drawText(kind, line.c_str(), x, cursorY, size, color);
            cursorY += size * 1.1f;
            line.clear();
        }
    }
    if (!line.empty())
    {
        drawText(kind, line.c_str(), x, cursorY, size, color);
        cursorY += size * 1.1f;
    }
    return cursorY - y;
}
