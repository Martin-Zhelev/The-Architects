// screen_trail.cpp
// Number Trail - the "follow the smallest numbers" puzzle from the assignment,
// upgraded: glowing tiles, smooth scrolling, combos, mouse support, particles
// and its own leaderboard.

#include "app.h"

#include <cmath>
#include <cstdlib>

namespace
{

NumberTrail trail;
ThemeId trailThemeId = ThemeId::Neon;
float elapsed = 0.0f;
float sinceLastStep = 0.0f;
float camRow = 0.0f;
float camCol = 0.0f;
float shakeAmount = 0.0f;
float flashAmount = 0.0f;
Color flashColor = WHITE;
float comboPop = 0.0f;
bool trailPaused = false;
bool pauseJustOpened = false;
bool trailDone = false;
float doneTimer = 0.0f;
bool trailRecorded = false;
int trailScore = 0;
std::string setupError;
std::string trailMessage;
float trailMessageTimer = 0.0f;
std::vector<Vector2> pathHistory; // (col, row) of every visited square, in order

const Theme& trailTheme()
{
    return getThemeData(trailThemeId);
}

void resetTrail()
{
    unsigned int seed = 0;
    if (trim(app.trailSeed).empty() || !parseUnsigned(app.trailSeed, seed))
    {
        seed = generateRandomSeed();
    }
    createNumberTrail(trail, app.trailSize, seed);
    elapsed = 0.0f;
    sinceLastStep = 99.0f;
    camRow = 0.0f;
    camCol = 0.0f;
    trailPaused = false;
    trailDone = false;
    doneTimer = 0.0f;
    trailRecorded = false;
    trailScore = 0;
    comboPop = 0.0f;
    pathHistory.clear();
    pathHistory.push_back(Vector2{0, 0});
    trailMessage = "Step onto the smallest glowing number!";
    trailMessageTimer = 3.0f;
}

void recordTrailResult()
{
    if (trailRecorded)
    {
        return;
    }
    trailRecorded = true;
    trailScore = calculateTrailScore(trail, static_cast<long long>(elapsed));
    recordTrail(app.profile.stats, trail.finished, trail.mistakes, trail.size, trail.bestCombo);
    app.profile.stats.themesPlayedMask |= 1 << static_cast<int>(trailThemeId);
    if (trail.finished)
    {
        ScoreEntry entry;
        entry.name = app.profile.name;
        entry.mazeSize = trail.size;
        entry.moves = trail.moves;
        entry.seconds = static_cast<long long>(elapsed);
        entry.score = trailScore;
        if (addToLeaderboard(app.boards.boards[static_cast<int>(BoardId::NumberTrail)], entry))
        {
            saveLeaderboards();
        }
    }
    saveProfile();
    announceAchievements();
}

void takeStep(TrailStep step)
{
    if (trailDone)
    {
        return;
    }
    const TrailResult result = makeTrailStep(trail, step);
    registerTrailCombo(trail, result, sinceLastStep);
    const Theme& t = trailTheme();
    const Vector2 screenCenter{static_cast<float>(ui.width) * 0.5f, static_cast<float>(ui.height) * 0.5f};

    switch (result)
    {
    case TrailResult::Correct:
    case TrailResult::Finished:
        sinceLastStep = 0.0f;
        pathHistory.push_back(Vector2{static_cast<float>(trail.col), static_cast<float>(trail.row)});
        playSfx(Sfx::TrailCorrect, 1.0f + 0.04f * static_cast<float>(std::min(trail.combo, 15)));
        spawnBurst2D(screenCenter, t.accent2, 18 + trail.combo * 2, 260.0f, 5.0f);
        if (trail.combo >= 3)
        {
            comboPop = 1.0f;
        }
        if (result == TrailResult::Finished)
        {
            playSfx(Sfx::Victory);
            spawnConfetti2D(ui.width, 260);
            flashColor = WHITE;
            flashAmount = 0.7f;
            trailDone = true;
        }
        break;
    case TrailResult::Wrong:
    case TrailResult::GameOver:
        playSfx(Sfx::TrailWrong);
        shakeAmount = app.profile.settings.reduceMotion ? 0.0f : 1.0f;
        flashColor = Color{255, 0, 40, 255};
        flashAmount = 0.5f;
        trailMessage = "Not the smallest number!  Mistakes: " + std::to_string(trail.mistakes) + " / " +
                       std::to_string(TRAIL_MAX_MISTAKES);
        trailMessageTimer = 2.0f;
        if (result == TrailResult::GameOver)
        {
            playSfx(Sfx::Defeat);
            trailDone = true;
        }
        break;
    case TrailResult::OutOfBounds:
        playSfx(Sfx::Bump);
        trailMessage = "You can't leave the grid that way.";
        trailMessageTimer = 1.5f;
        break;
    }
}

// Which step (if any) leads from the player to (row, col).
bool stepToCell(int row, int col, TrailStep& step)
{
    const TrailStep steps[3] = {TrailStep::Right, TrailStep::Down, TrailStep::Diagonal};
    for (const TrailStep s : steps)
    {
        int dr = 0;
        int dc = 0;
        getTrailStepOffset(s, dr, dc);
        if (trail.row + dr == row && trail.col + dc == col)
        {
            step = s;
            return true;
        }
    }
    return false;
}

void drawBoard(float cellSize, Vector2 origin)
{
    const Theme& t = trailTheme();
    const int digits = countDigits(trail.maxValue);
    const float fontSize = cellSize * (digits >= 4 ? 0.3f : 0.4f);

    // Visible range around the camera
    const int visibleCols = static_cast<int>(static_cast<float>(ui.width) / cellSize) + 2;
    const int visibleRows = static_cast<int>(static_cast<float>(ui.height) / cellSize) + 2;
    const int firstRow = std::max(0, static_cast<int>(camRow) - visibleRows / 2 - 1);
    const int firstCol = std::max(0, static_cast<int>(camCol) - visibleCols / 2 - 1);
    const int lastRow = std::min(trail.size - 1, firstRow + visibleRows + 2);
    const int lastCol = std::min(trail.size - 1, firstCol + visibleCols + 2);

    auto cellPos = [&](float row, float col) {
        return Vector2{origin.x + (col - camCol) * cellSize, origin.y + (row - camRow) * cellSize};
    };

    // Path line under the tiles
    for (std::size_t i = 1; i < pathHistory.size(); ++i)
    {
        const Vector2 a = cellPos(pathHistory[i - 1].y, pathHistory[i - 1].x);
        const Vector2 b = cellPos(pathHistory[i].y, pathHistory[i].x);
        DrawLineEx(a, b, cellSize * 0.18f, withAlpha(t.accent, 0.6f));
    }

    TrailStep best = TrailStep::Right;
    const bool hasBest = findBestTrailStep(trail, best);
    (void)hasBest;

    for (int row = firstRow; row <= lastRow; ++row)
    {
        for (int col = firstCol; col <= lastCol; ++col)
        {
            const Vector2 center = cellPos(static_cast<float>(row), static_cast<float>(col));
            const Rectangle rect{center.x - cellSize * 0.44f, center.y - cellSize * 0.44f, cellSize * 0.88f,
                                 cellSize * 0.88f};
            const bool isPlayer = row == trail.row && col == trail.col;
            const bool reachable = !trailDone && isReachableFromPlayer(trail, row, col);
            const bool isExit = row == trail.size - 1 && col == trail.size - 1;
            const bool visited = trail.visited[row][col];
            const float value = static_cast<float>(trail.values[row][col]) / static_cast<float>(trail.maxValue);

            Color fill = lerpColor(Color{40, 34, 70, 235}, Color{14, 12, 26, 235}, value);
            if (visited)
            {
                fill = withAlpha(t.accent, 0.75f);
            }
            if (isExit)
            {
                fill = lerpColor(Color{255, 200, 60, 255}, Color{255, 120, 40, 255}, 0.5f + 0.5f * std::sin(app.time * 3.0f));
            }
            DrawRectangleRounded(rect, 0.25f, 6, fill);

            if (reachable)
            {
                const float pulse = 0.5f + 0.5f * std::sin(app.time * 6.0f + static_cast<float>(row + col));
                const Rectangle glow{rect.x - cellSize * 0.04f, rect.y - cellSize * 0.04f, rect.width + cellSize * 0.08f,
                                     rect.height + cellSize * 0.08f};
                DrawRectangleRoundedLinesEx(glow, 0.25f, 6, cellSize * 0.06f, withAlpha(t.accent2, 0.6f + 0.4f * pulse));
                DrawRectangleRounded(rect, 0.25f, 6, withAlpha(t.accent2, 0.15f + 0.1f * pulse));
                const bool hovered = CheckCollisionPointRec(GetMousePosition(), rect);
                if (hovered)
                {
                    DrawRectangleRounded(rect, 0.25f, 6, withAlpha(WHITE, 0.2f));
                }
            }
            if (isPlayer)
            {
                const float pulse = 0.5f + 0.5f * std::sin(app.time * 5.0f);
                DrawRectangleRounded(rect, 0.25f, 6, t.accent3);
                DrawRectangleRoundedLinesEx(rect, 0.25f, 6, cellSize * (0.05f + 0.03f * pulse), WHITE);
            }

            const std::string number = std::to_string(trail.values[row][col]);
            const Color textColor = isPlayer || isExit ? Color{20, 10, 30, 255}
                                    : reachable       ? WHITE
                                    : visited         ? Color{20, 10, 30, 255}
                                                      : Color{200, 200, 220, 200};
            const float size = reachable ? fontSize * 1.15f : fontSize;
            drawTextCentered(FontKind::Display, number.c_str(), center.x, center.y - size * 0.5f, size, textColor);
        }
    }
}

} // namespace

// ---------------------------------------------------------------------------
// Setup screen
// ---------------------------------------------------------------------------

void enterTrailSetup()
{
    setupError.clear();
}

void updateTrailSetup(float dt)
{
    drawMenuBackground(dt);
    const ThemeId themeId = static_cast<ThemeId>(app.trailTheme);
    const Theme& t = getThemeData(themeId);
    beginUi(t.accent, t.accent2, t.panel);
    if (drawScreenHeader("NUMBER TRAIL", "Always step onto the smallest number you can reach"))
    {
        goTo(ScreenId::MainMenu);
    }
    const float w = static_cast<float>(ui.width);
    const float x = w * 0.5f - px(300);
    float y = px(150);
    slider("Grid size", Rectangle{x, y, px(600), px(56)}, app.trailSize, MIN_MAZE_SIZE, MAX_MAZE_SIZE, "");
    y += px(70);
    textField("Seed", Rectangle{x, y, px(600), px(56)}, app.trailSeed, 10, true, "random");
    y += px(70);
    const char* names[THEME_COUNT];
    for (int i = 0; i < THEME_COUNT; ++i)
    {
        names[i] = getThemeData(static_cast<ThemeId>(i)).name;
    }
    stepper("World", Rectangle{x, y, px(600), px(56)}, app.trailTheme, 0, THEME_COUNT - 1, names);
    y += px(90);

    drawWrappedText(FontKind::Body,
                    "Move RIGHT, DOWN or DIAGONALLY down-right onto the smallest of the highlighted numbers. Quick correct "
                    "steps build a combo for bonus points. 3 mistakes and the trail is lost.",
                    x, y, px(600), px(24), Color{220, 220, 240, 230});
    y += px(110);

    const Leaderboard& board = app.boards.boards[static_cast<int>(BoardId::NumberTrail)];
    if (board.count > 0)
    {
        drawTextCentered(FontKind::Body,
                         ("Record: " + board.entries[0].name + "  " + std::to_string(board.entries[0].score)).c_str(),
                         w * 0.5f, y - px(10), px(24), t.accent3);
    }
    if (bigButton("START", Rectangle{w * 0.5f - px(160), y + px(30), px(320), px(62)}, t.accent3))
    {
        unsigned int seed = 0;
        if (!trim(app.trailSeed).empty() && !parseUnsigned(app.trailSeed, seed))
        {
            setupError = "The seed must be a whole number from 0 to 4294967295.";
            playSfx(Sfx::TrailWrong);
        }
        else
        {
            goTo(ScreenId::Trail);
        }
    }
    if (!setupError.empty())
    {
        drawTextCentered(FontKind::Body, setupError.c_str(), w * 0.5f, y + px(104), px(24), Color{255, 90, 90, 255});
    }
    pauseJustOpened = false;
    endUi();
}

// ---------------------------------------------------------------------------
// Playing
// ---------------------------------------------------------------------------

void enterTrail()
{
    trailThemeId = static_cast<ThemeId>(app.trailTheme);
    resetTrail();
    playMusic(MusicTrack::Trail);
}

void leaveTrail()
{
    destroyNumberTrail(trail);
}

void updateTrail(float dt)
{
    const Theme& t = trailTheme();
    const float w = static_cast<float>(ui.width);
    const float h = static_cast<float>(ui.height);
    beginUi(t.accent, t.accent2, t.panel);
    const float cellSize = std::fmax(px(58), std::fmin(px(92), std::fmin(w, h) / 8.5f));
    const Vector2 origin{w * 0.5f, h * 0.53f};

    // ---- Input ----
    if (!trailPaused && !trailDone)
    {
        elapsed += dt;
        sinceLastStep += dt;
        if (IsKeyPressed(KEY_RIGHT) || IsKeyPressed(KEY_D))
        {
            takeStep(TrailStep::Right);
        }
        else if (IsKeyPressed(KEY_DOWN) || IsKeyPressed(KEY_S))
        {
            takeStep(TrailStep::Down);
        }
        else if (IsKeyPressed(KEY_E) || IsKeyPressed(KEY_THREE) || IsKeyPressed(KEY_KP_3) ||
                 IsKeyPressed(KEY_PAGE_DOWN))
        {
            takeStep(TrailStep::Diagonal);
        }
        else if (IsKeyPressed(KEY_UP) || IsKeyPressed(KEY_LEFT) || IsKeyPressed(KEY_W) || IsKeyPressed(KEY_A))
        {
            trailMessage = "Only RIGHT, DOWN or DIAGONAL (E) are allowed.";
            trailMessageTimer = 1.8f;
            playSfx(Sfx::Bump);
        }
        if (IsMouseButtonPressed(MOUSE_BUTTON_LEFT))
        {
            const Vector2 mouse = GetMousePosition();
            const int col = static_cast<int>(std::floor((mouse.x - origin.x) / cellSize + camCol + 0.5f));
            const int row = static_cast<int>(std::floor((mouse.y - origin.y) / cellSize + camRow + 0.5f));
            TrailStep step = TrailStep::Right;
            if (stepToCell(row, col, step))
            {
                takeStep(step);
            }
        }
        if (IsGamepadAvailable(0))
        {
            if (IsGamepadButtonPressed(0, GAMEPAD_BUTTON_LEFT_FACE_RIGHT))
            {
                takeStep(TrailStep::Right);
            }
            if (IsGamepadButtonPressed(0, GAMEPAD_BUTTON_LEFT_FACE_DOWN))
            {
                takeStep(TrailStep::Down);
            }
            if (IsGamepadButtonPressed(0, GAMEPAD_BUTTON_RIGHT_FACE_DOWN))
            {
                takeStep(TrailStep::Diagonal);
            }
        }
        if (IsKeyPressed(KEY_ESCAPE) || IsKeyPressed(KEY_P))
        {
            trailPaused = true;
            pauseJustOpened = true;
            resetFocus();
        }
    }
    if (trailDone)
    {
        doneTimer += dt;
        if (doneTimer > 0.8f)
        {
            recordTrailResult();
        }
    }

    // ---- Animation ----
    // The camera follows the player but never shows empty space beyond the grid.
    auto frame = [&](float focus, float screen, float originOffset) {
        const float size = static_cast<float>(trail.size);
        const float before = originOffset / cellSize - 0.5f;          // cells visible before the focus
        const float after = (screen - originOffset) / cellSize - 0.5f; // cells visible after it
        if (before + after + 1.0f >= size)
        {
            return (size - 1.0f) * 0.5f + (before - after) * 0.5f; // the whole grid fits: centre it
        }
        return std::fmax(before, std::fmin(size - 1.0f - after, focus));
    };
    const float wantRow = frame(static_cast<float>(trail.row), h - px(110), origin.y - px(110));
    const float wantCol = frame(static_cast<float>(trail.col), w, origin.x);
    camRow += (wantRow - camRow) * std::fmin(1.0f, dt * 8.0f);
    camCol += (wantCol - camCol) * std::fmin(1.0f, dt * 8.0f);
    shakeAmount = std::fmax(0.0f, shakeAmount - dt * 3.0f);
    flashAmount = std::fmax(0.0f, flashAmount - dt * 1.5f);
    comboPop = std::fmax(0.0f, comboPop - dt * 2.5f);
    trailMessageTimer = std::fmax(0.0f, trailMessageTimer - dt);
    updateParticles2D(dt);

    const Vector2 shaken{origin.x + (shakeAmount > 0 ? std::sin(app.time * 70.0f) * shakeAmount * px(10) : 0.0f),
                         origin.y};

    // ---- Scene (gets bloom and CRT) ----
    beginScene();
    drawSky(t, trailThemeId, ui.width, ui.height, app.time, app.time * 0.05f);
    DrawRectangle(0, 0, ui.width, ui.height, Color{0, 0, 0, 110});
    drawBoard(cellSize, shaken);
    drawParticles2D();
    endScene();
    PostEffects effects;
    effects.bloomStrength = t.bloomStrength;
    effects.bloomThreshold = t.bloomThreshold;
    effects.saturation = t.saturation;
    effects.crt = app.profile.settings.crtEffect;
    effects.time = app.time;
    effects.flashColor = withAlpha(flashColor, flashAmount);
    drawSceneWithEffects(effects, app.profile.settings.bloom);

    // ---- HUD ----
    const Rectangle bar{px(16), px(16), w - px(32), px(80)};
    drawPanel(bar, withAlpha(t.panel, 0.9f), t.accent);
    drawText(FontKind::Display, "NUMBER TRAIL", bar.x + px(20), bar.y + px(12), px(24), t.accent3);
    const std::string info = std::to_string(trail.size) + "x" + std::to_string(trail.size) + "   Row " +
                             std::to_string(trail.row + 1) + "/" + std::to_string(trail.size) + "  Col " +
                             std::to_string(trail.col + 1) + "/" + std::to_string(trail.size);
    drawText(FontKind::Body, info.c_str(), bar.x + px(20), bar.y + px(44), px(24), Color{210, 210, 230, 255});
    for (int i = 0; i < TRAIL_MAX_MISTAKES; ++i)
    {
        drawHeart(Vector2{w * 0.5f - px(40) + static_cast<float>(i) * px(40), bar.y + px(40)}, px(30),
                  i < TRAIL_MAX_MISTAKES - trail.mistakes ? Color{255, 60, 90, 255} : Color{60, 50, 60, 255});
    }
    const int seconds = static_cast<int>(elapsed);
    const std::string clock = std::to_string(seconds / 60) + ":" + (seconds % 60 < 10 ? "0" : "") +
                              std::to_string(seconds % 60);
    drawTextRight(FontKind::Display, clock.c_str(), bar.x + bar.width - px(20), bar.y + px(10), px(30), WHITE);
    drawTextRight(FontKind::Body, ("Bonus " + std::to_string(trail.bonusPoints)).c_str(), bar.x + bar.width - px(20),
                  bar.y + px(46), px(22), t.accent2);

    if (trail.combo >= 2)
    {
        const float scale = 1.0f + comboPop * 0.6f;
        const std::string combo = "x" + std::to_string(trail.combo) + " COMBO";
        drawGlowText(FontKind::Display, combo.c_str(), w - px(170), px(120), px(32) * scale,
                     rainbow(app.time * 0.5f), t.accent);
        const float window = std::fmax(0.0f, 1.0f - sinceLastStep / TRAIL_COMBO_WINDOW);
        drawProgressBar(Rectangle{w - px(290), px(170), px(240), px(10)}, window, t.accent2, Color{0, 0, 0, 150});
    }
    if (trailMessageTimer > 0.0f)
    {
        const float alpha = std::fmin(1.0f, trailMessageTimer * 2.0f);
        drawTextCentered(FontKind::Body, trailMessage.c_str(), w * 0.5f, h - px(84), px(28),
                         withAlpha(trail.mistakes > 0 && trailMessage.find("Mistakes") != std::string::npos
                                       ? Color{255, 100, 100, 255}
                                       : t.accent2,
                                   alpha));
    }
    drawTextCentered(FontKind::Body, "Right / Down arrows   E, 3 or PgDn = diagonal   or click a glowing square   ESC pause",
                     w * 0.5f, h - px(40), px(20), Color{220, 220, 240, 170});

    // ---- Pause ----
    if (trailPaused)
    {
        DrawRectangle(0, 0, ui.width, ui.height, Color{0, 0, 0, 170});
        const Rectangle panel{w * 0.5f - px(220), h * 0.5f - px(170), px(440), px(340)};
        drawPanel(panel, withAlpha(t.panel, 0.97f), t.accent, 2.0f);
        drawGlowText(FontKind::Display, "PAUSED", w * 0.5f, panel.y + px(24), px(40), WHITE, t.accent);
        if (button("RESUME", Rectangle{panel.x + px(50), panel.y + px(100), px(340), px(52)}) || (ui.back && !pauseJustOpened))
        {
            trailPaused = false;
        }
        if (button("RESTART", Rectangle{panel.x + px(50), panel.y + px(166), px(340), px(52)}))
        {
            resetTrail();
        }
        if (button("QUIT TO MENU", Rectangle{panel.x + px(50), panel.y + px(232), px(340), px(52)}))
        {
            goTo(ScreenId::MainMenu);
        }
    }

    // ---- Results ----
    if (trailDone && doneTimer > 1.0f)
    {
        const float age = doneTimer - 1.0f;
        DrawRectangle(0, 0, ui.width, ui.height, Color{0, 0, 0, static_cast<unsigned char>(std::fmin(170.0f, age * 300.0f))});
        const Rectangle panel{w * 0.5f - px(300), h * 0.5f - px(240), px(600), px(480)};
        drawPanel(panel, withAlpha(t.panel, 0.97f), trail.finished ? t.accent3 : Color{255, 70, 60, 255}, 2.0f);
        if (trail.finished)
        {
            drawRainbowTitle("TRAIL COMPLETE!", w * 0.5f, panel.y + px(24), px(44), app.time);
        }
        else
        {
            drawGlowText(FontKind::Display, "TRAIL LOST", w * 0.5f, panel.y + px(24), px(48), Color{255, 80, 70, 255},
                         Color{255, 0, 0, 255});
        }
        float y = panel.y + px(110);
        auto line = [&](const char* label, const std::string& value) {
            drawText(FontKind::Body, label, panel.x + px(60), y, px(26), Color{200, 200, 220, 255});
            drawTextRight(FontKind::Display, value.c_str(), panel.x + panel.width - px(60), y + px(2), px(22), WHITE);
            y += px(38);
        };
        line("Grid", std::to_string(trail.size) + "x" + std::to_string(trail.size));
        line("Reached", "Row " + std::to_string(trail.row + 1) + "/" + std::to_string(trail.size) + "  Col " +
                            std::to_string(trail.col + 1) + "/" + std::to_string(trail.size));
        line("Steps", std::to_string(trail.moves));
        line("Mistakes", std::to_string(trail.mistakes));
        line("Best combo", "x" + std::to_string(trail.bestCombo));
        line("Time", clock);
        if (trail.finished)
        {
            drawGlowText(FontKind::Display, ("SCORE  " + std::to_string(trailScore)).c_str(), w * 0.5f, y + px(6), px(36),
                         t.accent3, t.accent);
        }
        if (age > 0.6f)
        {
            if (button("PLAY AGAIN", Rectangle{panel.x + px(40), panel.y + panel.height - px(76), px(250), px(54)}))
            {
                resetTrail();
            }
            if (button("MENU", Rectangle{panel.x + panel.width - px(290), panel.y + panel.height - px(76), px(250), px(54)}))
            {
                goTo(ScreenId::TrailSetup);
            }
        }
    }
    pauseJustOpened = false;
    endUi();
}
