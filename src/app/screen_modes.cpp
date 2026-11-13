// screen_modes.cpp
// Choosing what to play: the campaign map, the challenge cards and the
// free play setup form.

#include "app.h"

#include <cmath>

namespace
{

std::string freePlayError;
const int TIME_LIMITS[5] = {0, 60, 120, 180, 300};
const char* const TIME_NAMES[5] = {"Off", "1 min", "2 min", "3 min", "5 min"};

void beginThemedUi(ThemeId theme)
{
    const Theme& t = getThemeData(theme);
    beginUi(t.accent, t.accent2, t.panel);
}

const char* const DIFFICULTY_NAMES[DIFFICULTY_COUNT] = {"Easy", "Normal", "Hard", "Expert"};

// One difficulty for every mode; harder = less time and fewer steps for
// 3 stars, faster Minotaurs, more points.
bool difficultyStepper(Rectangle rect)
{
    const bool changed =
        stepper("Difficulty", rect, app.profile.settings.difficulty, 0, DIFFICULTY_COUNT - 1, DIFFICULTY_NAMES);
    if (changed)
    {
        saveProfile();
    }
    return changed;
}

void launchRun(RunMode mode, int index, ThemeId theme, const RunConfig& config)
{
    app.pendingRun.mode = mode;
    app.pendingRun.index = index;
    app.pendingRun.theme = theme;
    app.pendingRun.config = config;
    app.pendingRun.config.difficulty = app.profile.settings.difficulty; // chosen in every mode screen
    app.pendingRun.testPlay = false;
    app.resumeRequested = false;
    goTo(ScreenId::Game);
}

// Small icons that list what a run contains.
void drawFeatureIcons(const RunConfig& config, float x, float y, float size)
{
    const float step = size * 1.25f;
    if (config.gemCount > 0)
    {
        drawItemIcon(Item::Gem, Vector2{x, y}, size, ui.time);
        x += step;
    }
    if (config.powerUps)
    {
        drawItemIcon(Item::Boots, Vector2{x, y}, size, ui.time);
        x += step;
    }
    if (config.traps)
    {
        drawItemIcon(Item::Spikes, Vector2{x, y}, size, ui.time);
        x += step;
    }
    if (config.teleporterPairs > 0)
    {
        drawItemIcon(Item::Teleporter, Vector2{x, y}, size, ui.time);
        x += step;
    }
    if (config.fogOfWar)
    {
        DrawCircleV(Vector2{x, y}, size * 0.4f, Color{90, 100, 130, 255});
        DrawCircleV(Vector2{x + size * 0.2f, y - size * 0.1f}, size * 0.3f, Color{140, 150, 180, 255});
        x += step;
    }
    for (int i = 0; i < config.minotaurCount; ++i)
    {
        drawMinotaurIcon(Vector2{x, y}, size, Color{130, 70, 45, 255});
        x += step * 0.8f;
    }
    if (config.timeLimitSeconds > 0)
    {
        drawItemIcon(Item::Hourglass, Vector2{x, y}, size, ui.time);
    }
}

std::string describeConfig(const RunConfig& config)
{
    std::string text = std::to_string(config.size) + "x" + std::to_string(config.size);
    text += "  |  " + std::to_string(config.lives) + (config.lives == 1 ? " life" : " lives");
    if (config.timeLimitSeconds > 0)
    {
        text += "  |  " + std::to_string(config.timeLimitSeconds) + "s limit";
    }
    if (!config.hintsAllowed)
    {
        text += "  |  no hints";
    }
    if (config.requireAllGems)
    {
        text += "  |  collect all gems";
    }
    return text;
}

} // namespace

// ---------------------------------------------------------------------------
// Campaign
// ---------------------------------------------------------------------------

void enterCampaign()
{
    const int unlocked = countUnlockedLevels(app.profile);
    if (app.selectedLevel >= unlocked)
    {
        app.selectedLevel = unlocked - 1;
    }
}

void updateCampaign(float dt)
{
    drawMenuBackground(dt);
    const LevelDef& selected = getLevel(app.selectedLevel);
    beginThemedUi(selected.theme);
    const std::string subtitle = std::to_string(countTotalStars(app.profile)) + " / " +
                                 std::to_string(LEVEL_COUNT * 3) + " stars collected";
    if (drawScreenHeader("CAMPAIGN", subtitle.c_str()))
    {
        goTo(ScreenId::MainMenu);
    }

    const float w = static_cast<float>(ui.width);
    const float worldW = px(224);
    const float gap = px(14);
    const float startX = w * 0.5f - (worldW * WORLD_COUNT + gap * (WORLD_COUNT - 1)) * 0.5f;
    const float top = px(122);

    for (int world = 0; world < WORLD_COUNT; ++world)
    {
        const ThemeId theme = getLevel(world * LEVELS_PER_WORLD).theme;
        const Theme& t = getThemeData(theme);
        const float x = startX + static_cast<float>(world) * (worldW + gap);
        const Rectangle panel{x, top, worldW, px(330)};
        drawPanel(panel, withAlpha(t.panel, 0.9f), t.accent);
        // Rounded like the card around it (inner radius = card radius - inset).
        drawRoundedGradientV(Rectangle{x + px(6), top + px(6), worldW - px(12), px(60)}, px(6), withAlpha(t.skyTop, 0.9f),
                             withAlpha(t.skyBottom, 0.6f));
        drawTextCentered(FontKind::Body, ("WORLD " + std::to_string(world + 1)).c_str(), x + worldW * 0.5f, top + px(10),
                         px(18), withAlpha(WHITE, 0.8f));
        drawTextCentered(FontKind::Display, getWorldName(world), x + worldW * 0.5f, top + px(32), px(17), t.accent3);

        for (int i = 0; i < LEVELS_PER_WORLD; ++i)
        {
            const int level = world * LEVELS_PER_WORLD + i;
            const bool unlocked = isLevelUnlocked(app.profile, level);
            const Rectangle node{x + px(22), top + px(84) + static_cast<float>(i) * px(80), worldW - px(44), px(62)};
            if (i > 0)
            {
                DrawLineEx(Vector2{x + worldW * 0.5f, node.y - px(18)}, Vector2{x + worldW * 0.5f, node.y},
                           px(4), unlocked ? t.accent : Color{80, 80, 90, 255});
            }
            const Color saved = ui.accent;
            ui.accent = level == app.selectedLevel ? t.accent3 : t.accent;
            // Long names (e.g. "15. The Minotaur's Feast") are shrunk to fit by button().
            const std::string label = std::to_string(level + 1) + ". " + getLevel(level).name;
            if (button(label.c_str(), node, unlocked, " "))
            {
                if (app.selectedLevel == level)
                {
                    launchRun(RunMode::Campaign, level, getLevel(level).theme, getLevel(level).config);
                }
                app.selectedLevel = level;
            }
            ui.accent = saved;
            for (int s = 0; s < 3; ++s)
            {
                const bool earned = s < app.profile.levelStars[level];
                drawStar(Vector2{node.x + node.width * 0.5f + static_cast<float>(s - 1) * px(24), node.y + px(45)},
                         px(9), earned ? Color{255, 215, 60, 255} : Color{70, 70, 85, 255});
            }
            if (!unlocked)
            {
                drawLock(Vector2{node.x + node.width - px(14), node.y + px(14)}, px(16), Color{160, 160, 180, 255});
            }
        }
    }

    // Selected level details
    const Theme& t = getThemeData(selected.theme);
    const Rectangle info{startX, top + px(350), worldW * WORLD_COUNT + gap * (WORLD_COUNT - 1), px(170)};
    drawPanel(info, withAlpha(t.panel, 0.95f), t.accent3);
    drawText(FontKind::Display, selected.name, info.x + px(28), info.y + px(18), px(32), t.accent3);
    drawText(FontKind::Body, selected.description, info.x + px(28), info.y + px(62), px(26), WHITE);
    drawText(FontKind::Body, describeConfig(selected.config).c_str(), info.x + px(28), info.y + px(100), px(22),
             Color{200, 200, 220, 255});
    drawFeatureIcons(selected.config, info.x + px(42), info.y + px(144), px(28));
    const int best = app.profile.levelBestScore[app.selectedLevel];
    if (best > 0)
    {
        drawTextRight(FontKind::Body, ("Best score: " + std::to_string(best)).c_str(), info.x + info.width - px(28),
                      info.y + px(22), px(24), t.accent2);
    }
    difficultyStepper(Rectangle{info.x + info.width - px(600), info.y + info.height - px(78), px(330), px(58)});
    if (bigButton("PLAY", Rectangle{info.x + info.width - px(250), info.y + info.height - px(78), px(222), px(58)},
                  t.accent3, isLevelUnlocked(app.profile, app.selectedLevel)))
    {
        launchRun(RunMode::Campaign, app.selectedLevel, selected.theme, selected.config);
    }
    endUi();
}

// ---------------------------------------------------------------------------
// Challenges
// ---------------------------------------------------------------------------

void enterChallenges()
{
}

void updateChallenges(float dt)
{
    drawMenuBackground(dt);
    const Theme& menu = getThemeData(menuTheme());
    beginUi(menu.accent, menu.accent2, menu.panel);
    if (drawScreenHeader("CHALLENGES", "Special rules, special leaderboards"))
    {
        goTo(ScreenId::MainMenu);
    }

    int year = 0;
    int month = 0;
    int day = 0;
    getToday(year, month, day);
    const int today = makeDateNumber(year, month, day);

    const float w = static_cast<float>(ui.width);
    const float cardW = px(380);
    const float cardH = px(232);
    const float startX = w * 0.5f - (cardW * 3 + px(20) * 2) * 0.5f;
    for (int i = 0; i < CHALLENGE_COUNT; ++i)
    {
        const ChallengeId id = static_cast<ChallengeId>(i);
        const ChallengeInfo& info = getChallengeInfo(id);
        const Theme& t = getThemeData(info.theme);
        const float x = startX + static_cast<float>(i % 3) * (cardW + px(20));
        const float y = px(124) + static_cast<float>(i / 3) * (cardH + px(20));
        const Rectangle card{x, y, cardW, cardH};

        const Color saved = ui.accent;
        ui.accent = t.accent;
        const bool play = button("", card, true);
        ui.accent = saved;
        const bool focused = isFocused(ui.widgetCount - 1);
        const Color textColor = focused ? Color{20, 10, 30, 255} : WHITE;
        drawText(FontKind::Display, info.name, x + px(22), y + px(18), px(26), focused ? textColor : t.accent3);
        drawWrappedText(FontKind::Body, info.description, x + px(22), y + px(60), cardW - px(44), px(23),
                        withAlpha(textColor, 0.9f));
        const RunConfig config = makeChallengeConfig(id, 1);
        drawFeatureIcons(config, x + px(36), y + cardH - px(72), px(26));

        std::string best = "Best: " + std::to_string(app.profile.challengeBest[i]);
        if (id == ChallengeId::Daily)
        {
            const std::string date = std::to_string(year) + "-" + (month < 10 ? "0" : "") + std::to_string(month) + "-" +
                                     (day < 10 ? "0" : "") + std::to_string(day);
            best = "Today " + date + "  |  " +
                   (app.profile.dailyBestDate == today ? "best " + std::to_string(app.profile.dailyBestScore)
                                                       : std::string("not played yet"));
        }
        drawText(FontKind::Body, best.c_str(), x + px(22), y + cardH - px(40), px(22), focused ? textColor : t.accent2);

        if (play)
        {
            const unsigned int seed = id == ChallengeId::Daily ? makeDailySeed(year, month, day) : generateRandomSeed();
            launchRun(RunMode::Challenge, i, info.theme, makeChallengeConfig(id, seed));
        }
    }
    difficultyStepper(Rectangle{w * 0.5f - px(220), px(124) + 2.0f * (cardH + px(20)), px(440), px(50)});
    endUi();
}

// ---------------------------------------------------------------------------
// Free play
// ---------------------------------------------------------------------------

void enterFreePlay()
{
    freePlayError.clear();
}

void updateFreePlay(float dt)
{
    drawMenuBackground(dt);
    const ThemeId theme = static_cast<ThemeId>(app.freePlayTheme);
    beginThemedUi(theme);
    if (drawScreenHeader("FREE PLAY", "Build your own labyrinth"))
    {
        goTo(ScreenId::MainMenu);
    }
    RunConfig& c = app.freePlayConfig;
    const float w = static_cast<float>(ui.width);
    const float leftX = w * 0.5f - px(530);
    const float rightX = w * 0.5f + px(20);
    const float rowW = px(510);
    const float rowH = px(52);
    const float step = rowH + px(10);
    float y = px(124);

    slider("Maze size", Rectangle{leftX, y, rowW, rowH}, c.size, MIN_MAZE_SIZE, MAX_MAZE_SIZE, "");
    const char* themeNames[THEME_COUNT];
    for (int i = 0; i < THEME_COUNT; ++i)
    {
        themeNames[i] = getThemeData(static_cast<ThemeId>(i)).name;
    }
    stepper("World", Rectangle{rightX, y, rowW, rowH}, app.freePlayTheme, 0, THEME_COUNT - 1, themeNames);
    y += step;
    textField("Seed", Rectangle{leftX, y, rowW - px(140), rowH}, app.freePlaySeed, 10, true, "random");
    if (button("DICE", Rectangle{leftX + rowW - px(128), y, px(128), rowH}))
    {
        app.freePlaySeed = std::to_string(generateRandomSeed() % 1000000u);
    }
    stepper("Minotaurs", Rectangle{rightX, y, rowW, rowH}, c.minotaurCount, 0, 3);
    y += step;
    stepper("Lives", Rectangle{leftX, y, rowW, rowH}, c.lives, 1, 5);
    stepper("Teleporter pairs", Rectangle{rightX, y, rowW, rowH}, c.teleporterPairs, 0, 3);
    y += step;
    slider("Gems", Rectangle{leftX, y, rowW, rowH}, c.gemCount, 0, 30, "");
    int timeIndex = 0;
    for (int i = 0; i < 5; ++i)
    {
        timeIndex = TIME_LIMITS[i] == c.timeLimitSeconds ? i : timeIndex;
    }
    if (stepper("Time limit", Rectangle{rightX, y, rowW, rowH}, timeIndex, 0, 4, TIME_NAMES))
    {
        c.timeLimitSeconds = TIME_LIMITS[timeIndex];
    }
    y += step;
    toggle("Power-ups", Rectangle{leftX, y, rowW, rowH}, c.powerUps);
    toggle("Fog of war", Rectangle{rightX, y, rowW, rowH}, c.fogOfWar);
    y += step;
    toggle("Spike traps", Rectangle{leftX, y, rowW, rowH}, c.traps);
    toggle("Hints allowed", Rectangle{rightX, y, rowW, rowH}, c.hintsAllowed);
    y += step;
    toggle("Exit sealed until all gems found", Rectangle{leftX, y, rowW, rowH}, c.requireAllGems);
    difficultyStepper(Rectangle{rightX, y, rowW, rowH});
    y += step;

    drawFeatureIcons(c, leftX + px(20), y + px(37), px(30));

    if (bigButton("START", Rectangle{w * 0.5f - px(160), y + px(6), px(320), px(62)}, getThemeData(theme).accent3))
    {
        unsigned int seed = 0;
        if (trim(app.freePlaySeed).empty())
        {
            RunConfig config = clampConfig(c);
            config.seed = generateRandomSeed();
            launchRun(RunMode::FreePlay, 0, theme, config);
        }
        else if (!parseUnsigned(app.freePlaySeed, seed))
        {
            freePlayError = "The seed must be a whole number from 0 to 4294967295.";
            playSfx(Sfx::TrailWrong);
        }
        else if (c.requireAllGems && c.gemCount == 0)
        {
            freePlayError = "A sealed exit needs at least one gem.";
            playSfx(Sfx::TrailWrong);
        }
        else
        {
            RunConfig config = clampConfig(c);
            config.seed = seed;
            launchRun(RunMode::FreePlay, 0, theme, config);
        }
    }
    if (!freePlayError.empty())
    {
        drawTextCentered(FontKind::Body, freePlayError.c_str(), w * 0.5f, y + px(76), px(24), Color{255, 90, 90, 255});
    }
    endUi();
}
