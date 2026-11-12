// particles.cpp
// Particle effects: sparkles, confetti, embers, snow, sprinkles (3D) and
// bursts / confetti for the menus (2D). Fixed-size pools, no allocations
// while playing.

#include "app.h"

#include <cmath>
#include <cstdlib>

namespace
{

struct Particle3D
{
    Vector3 position;
    Vector3 velocity;
    Color color;
    float life;
    float maxLife;
    float size;
    float gravity;
    float drag;
    bool alive;
};

struct Particle2D
{
    Vector2 position;
    Vector2 velocity;
    Color color;
    float life;
    float maxLife;
    float size;
    float gravity;
    float spin;
    float angle;
    bool square;
    bool alive;
};

const int MAX_3D = 3000;
const int MAX_2D = 1500;
Particle3D particles3D[MAX_3D];
Particle2D particles2D[MAX_2D];
int next3D = 0;
int next2D = 0;
Texture2D glowTexture;
float ambientAccumulator = 0.0f;

float randomFloat(float low, float high)
{
    return low + (high - low) * (static_cast<float>(std::rand()) / static_cast<float>(RAND_MAX));
}

Vector3 randomDirection()
{
    const float theta = randomFloat(0.0f, 2.0f * PI);
    const float z = randomFloat(-1.0f, 1.0f);
    const float r = std::sqrt(1.0f - z * z);
    return Vector3{r * std::cos(theta), z, r * std::sin(theta)};
}

Particle3D& allocate3D()
{
    Particle3D& p = particles3D[next3D];
    next3D = (next3D + 1) % MAX_3D;
    p = Particle3D{};
    p.alive = true;
    p.drag = 1.0f;
    return p;
}

Particle2D& allocate2D()
{
    Particle2D& p = particles2D[next2D];
    next2D = (next2D + 1) % MAX_2D;
    p = Particle2D{};
    p.alive = true;
    return p;
}

const Color CONFETTI_COLORS[8] = {{255, 60, 120, 255}, {255, 210, 50, 255}, {60, 230, 255, 255},
                                  {120, 255, 120, 255}, {200, 100, 255, 255}, {255, 140, 40, 255},
                                  {255, 255, 255, 255}, {255, 100, 220, 255}};

} // namespace

void initParticles()
{
    // A soft round dot used for every particle, generated in code.
    const int size = 64;
    Image image = GenImageColor(size, size, BLANK);
    Color* pixels = static_cast<Color*>(image.data);
    for (int y = 0; y < size; ++y)
    {
        for (int x = 0; x < size; ++x)
        {
            const float dx = (static_cast<float>(x) + 0.5f) / size * 2.0f - 1.0f;
            const float dy = (static_cast<float>(y) + 0.5f) / size * 2.0f - 1.0f;
            float d = 1.0f - std::sqrt(dx * dx + dy * dy);
            d = d < 0.0f ? 0.0f : d;
            const float alpha = d * d * (0.6f + 0.4f * d);
            pixels[y * size + x] = Color{255, 255, 255, static_cast<unsigned char>(alpha * 255.0f)};
        }
    }
    glowTexture = LoadTextureFromImage(image);
    UnloadImage(image);
    SetTextureFilter(glowTexture, TEXTURE_FILTER_BILINEAR);
    clearParticles();
    clearParticles2D();
}

void unloadParticles()
{
    UnloadTexture(glowTexture);
}

Texture2D getGlowTexture()
{
    return glowTexture;
}

void clearParticles()
{
    for (Particle3D& p : particles3D)
    {
        p.alive = false;
    }
}

void updateParticles(float dt)
{
    for (Particle3D& p : particles3D)
    {
        if (!p.alive)
        {
            continue;
        }
        p.life -= dt;
        if (p.life <= 0.0f)
        {
            p.alive = false;
            continue;
        }
        p.velocity.y -= p.gravity * dt;
        const float dragFactor = std::pow(p.drag, dt);
        p.velocity = Vector3Scale(p.velocity, dragFactor);
        p.position = Vector3Add(p.position, Vector3Scale(p.velocity, dt));
        if (p.position.y < 0.02f && p.gravity > 0.0f)
        {
            p.position.y = 0.02f;
            p.velocity.y *= -0.4f;
            p.velocity.x *= 0.7f;
            p.velocity.z *= 0.7f;
        }
    }
}

void drawParticles3D(Camera3D camera)
{
    BeginBlendMode(BLEND_ADDITIVE);
    for (const Particle3D& p : particles3D)
    {
        if (!p.alive)
        {
            continue;
        }
        const float t = p.life / p.maxLife;
        const float fadeIn = (1.0f - t) < 0.1f ? (1.0f - t) * 10.0f : 1.0f;
        const float alpha = t * fadeIn;
        DrawBillboard(camera, glowTexture, p.position, p.size * (0.6f + 0.4f * t), withAlpha(p.color, alpha));
    }
    EndBlendMode();
}

void spawnBurst(Vector3 position, Color color, int count, float speed, float size)
{
    for (int i = 0; i < count; ++i)
    {
        Particle3D& p = allocate3D();
        p.position = position;
        p.velocity = Vector3Scale(randomDirection(), randomFloat(0.3f, 1.0f) * speed);
        p.velocity.y = std::fabs(p.velocity.y) + speed * 0.3f;
        p.color = color;
        p.maxLife = p.life = randomFloat(0.5f, 1.1f);
        p.size = size * randomFloat(0.6f, 1.4f);
        p.gravity = 2.5f;
        p.drag = 0.4f;
    }
}

void spawnConfetti3D(Vector3 position, int count)
{
    for (int i = 0; i < count; ++i)
    {
        Particle3D& p = allocate3D();
        p.position = Vector3Add(position, Vector3{randomFloat(-0.3f, 0.3f), 0.2f, randomFloat(-0.3f, 0.3f)});
        p.velocity = Vector3{randomFloat(-2.5f, 2.5f), randomFloat(3.0f, 7.0f), randomFloat(-2.5f, 2.5f)};
        p.color = CONFETTI_COLORS[i % 8];
        p.maxLife = p.life = randomFloat(1.5f, 3.0f);
        p.size = randomFloat(0.08f, 0.16f);
        p.gravity = 4.0f;
        p.drag = 0.6f;
    }
}

void spawnRing(Vector3 position, Color color, int count, float radius)
{
    for (int i = 0; i < count; ++i)
    {
        const float angle = 2.0f * PI * static_cast<float>(i) / static_cast<float>(count);
        Particle3D& p = allocate3D();
        p.position = Vector3Add(position, Vector3{std::cos(angle) * radius * 0.2f, 0.0f, std::sin(angle) * radius * 0.2f});
        p.velocity = Vector3{std::cos(angle) * radius * 2.0f, randomFloat(0.5f, 2.0f), std::sin(angle) * radius * 2.0f};
        p.color = color;
        p.maxLife = p.life = randomFloat(0.6f, 1.0f);
        p.size = 0.14f;
        p.drag = 0.2f;
    }
}

void spawnAmbient(AmbientStyle style, Vector3 around, float dt, float radius)
{
    const float rate = (style == AmbientStyle::Snow) ? 90.0f : 45.0f;
    ambientAccumulator += dt * rate;
    while (ambientAccumulator >= 1.0f)
    {
        ambientAccumulator -= 1.0f;
        Particle3D& p = allocate3D();
        p.position = Vector3{around.x + randomFloat(-radius, radius), randomFloat(0.0f, 2.2f),
                             around.z + randomFloat(-radius, radius)};
        p.maxLife = p.life = randomFloat(2.5f, 5.0f);
        p.size = 0.05f;
        switch (style)
        {
        case AmbientStyle::NeonDust:
            p.color = (std::rand() % 2 == 0) ? Color{255, 60, 220, 255} : Color{60, 230, 255, 255};
            p.velocity = Vector3{randomFloat(-0.1f, 0.1f), randomFloat(0.05f, 0.25f), randomFloat(-0.1f, 0.1f)};
            break;
        case AmbientStyle::DungeonMotes:
            p.color = Color{255, 200, 140, 160};
            p.velocity = Vector3{randomFloat(-0.08f, 0.08f), randomFloat(-0.05f, 0.05f), randomFloat(-0.08f, 0.08f)};
            p.size = 0.035f;
            break;
        case AmbientStyle::Embers:
            p.color = (std::rand() % 3 == 0) ? Color{255, 220, 80, 255} : Color{255, 100, 20, 255};
            p.position.y = randomFloat(0.0f, 0.5f);
            p.velocity = Vector3{randomFloat(-0.2f, 0.2f), randomFloat(0.4f, 1.0f), randomFloat(-0.2f, 0.2f)};
            p.size = 0.06f;
            break;
        case AmbientStyle::Snow:
            p.color = Color{230, 245, 255, 200};
            p.position.y = randomFloat(1.5f, 3.0f);
            p.velocity = Vector3{randomFloat(-0.3f, 0.3f), randomFloat(-0.7f, -0.35f), randomFloat(-0.3f, 0.3f)};
            p.size = 0.06f;
            break;
        case AmbientStyle::Sprinkles:
            p.color = CONFETTI_COLORS[std::rand() % 8];
            p.position.y = randomFloat(1.5f, 3.0f);
            p.velocity = Vector3{randomFloat(-0.2f, 0.2f), randomFloat(-0.5f, -0.2f), randomFloat(-0.2f, 0.2f)};
            p.size = 0.05f;
            break;
        }
    }
}

// ---------------------------------------------------------------------------
// 2D particles for menus and the Number Trail
// ---------------------------------------------------------------------------

void clearParticles2D()
{
    for (Particle2D& p : particles2D)
    {
        p.alive = false;
    }
}

void updateParticles2D(float dt)
{
    for (Particle2D& p : particles2D)
    {
        if (!p.alive)
        {
            continue;
        }
        p.life -= dt;
        if (p.life <= 0.0f)
        {
            p.alive = false;
            continue;
        }
        p.velocity.y += p.gravity * dt;
        p.velocity = Vector2Scale(p.velocity, std::pow(0.5f, dt));
        p.position = Vector2Add(p.position, Vector2Scale(p.velocity, dt));
        p.angle += p.spin * dt;
    }
}

void drawParticles2D()
{
    for (const Particle2D& p : particles2D)
    {
        if (!p.alive)
        {
            continue;
        }
        const float t = p.life / p.maxLife;
        if (p.square)
        {
            const float wobble = std::fabs(std::cos(p.angle * 0.5f));
            DrawRectanglePro(Rectangle{p.position.x, p.position.y, p.size, p.size * 0.5f * (0.3f + wobble)},
                             Vector2{p.size * 0.5f, p.size * 0.25f}, p.angle * RAD2DEG, withAlpha(p.color, t * 1.5f));
        }
        else
        {
            BeginBlendMode(BLEND_ADDITIVE);
            const float s = p.size * 4.0f * (0.5f + 0.5f * t);
            DrawTexturePro(glowTexture, Rectangle{0, 0, 64, 64},
                           Rectangle{p.position.x - s * 0.5f, p.position.y - s * 0.5f, s, s}, Vector2{0, 0}, 0.0f,
                           withAlpha(p.color, t));
            EndBlendMode();
        }
    }
}

void spawnBurst2D(Vector2 position, Color color, int count, float speed, float size)
{
    for (int i = 0; i < count; ++i)
    {
        Particle2D& p = allocate2D();
        const float angle = randomFloat(0.0f, 2.0f * PI);
        const float s = randomFloat(0.3f, 1.0f) * speed;
        p.position = position;
        p.velocity = Vector2{std::cos(angle) * s, std::sin(angle) * s};
        p.color = color;
        p.maxLife = p.life = randomFloat(0.4f, 0.9f);
        p.size = size * randomFloat(0.6f, 1.3f);
        p.gravity = speed * 0.8f;
    }
}

void spawnConfetti2D(int screenWidth, int count)
{
    for (int i = 0; i < count; ++i)
    {
        Particle2D& p = allocate2D();
        p.position = Vector2{randomFloat(0.0f, static_cast<float>(screenWidth)), randomFloat(-200.0f, -10.0f)};
        p.velocity = Vector2{randomFloat(-80.0f, 80.0f), randomFloat(50.0f, 250.0f)};
        p.color = CONFETTI_COLORS[i % 8];
        p.maxLife = p.life = randomFloat(3.0f, 5.0f);
        p.size = randomFloat(8.0f, 16.0f);
        p.gravity = 220.0f;
        p.spin = randomFloat(-8.0f, 8.0f);
        p.square = true;
    }
}
