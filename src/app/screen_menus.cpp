// screen_menus.cpp
// Menu screens: the animated 3D background, splash, profiles, main menu,
// achievements, leaderboards, settings and "how to play".

#include "app.h"

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <filesystem>

namespace
{

// ---------------------------------------------------------------------------
// Animated 3D maze behind the menus
// ---------------------------------------------------------------------------

GameState demoState;
ThemeId demoTheme = ThemeId::Neon;
float demoTimer = 0.0f;
bool demoBuilt = false;
const float DEMO_SECONDS_PER_THEME = 14.0f;

void buildDemo()
{
    RunConfig config;
    config.size = 13;
    config.seed = 1000u + static_cast<unsigned int>(demoTheme) * 77u;
    config.gemCount = 12;
    config.powerUps = true;
    config.traps = true;
    config.teleporterPairs = 1;
    config.minotaurCount = 0;
    startRun(demoState, config);
    toggleHint(demoState);
    buildWorld(demoState, demoTheme);
    demoBuilt = true;
}

Theme& currentMenuTheme()
{
    return getThemeData(demoTheme);
}

void beginMenuUi()
{
    const Theme& theme = currentMenuTheme();
    beginUi(theme.accent, theme.accent2, theme.panel);
}

// ---------------------------------------------------------------------------
// Profiles screen state
// ---------------------------------------------------------------------------

struct ProfileCard
{
    Profile profile;
};
std::vector<ProfileCard> profileCards;
std::string newProfileName;
std::string profileError;
int confirmDelete = -1;

void scanProfiles()
{
    profileCards.clear();
    try
    {
        for (const auto& entry : std::filesystem::directory_iterator(app.saveDir))
        {
            if (entry.path().extension() != ".profile")
            {
                continue;
            }
            std::string text;
            Profile profile;
            if (readTextFile(entry.path().string(), text) && deserializeProfile(text, profile))
            {
                profileCards.push_back(ProfileCard{profile});
            }
        }
    }
    catch (const std::exception&)
    {
        // An unreadable folder just means no profiles yet.
    }
}

void selectProfile(const Profile& profile)
{
    app.profile = profile;
    app.hasProfile = true;
    app.freePlayTheme = profile.settings.theme;
    app.trailTheme = profile.settings.theme;
    applySettings();
    saveProfile();
    goTo(ScreenId::MainMenu);
}

bool tryLoadLastProfile()
{
    std::string name;
    if (!readTextFile(app.saveDir + "/last_profile.txt", name))
    {
        return false;
    }
    name = trim(name);
    std::string text;
    Profile profile;
    if (!isValidPlayerName(name) || !readTextFile(app.saveDir + "/" + profileFileName(name), text) ||
        !deserializeProfile(text, profile))
    {
        return false;
    }
    app.profile = profile;
    app.hasProfile = true;
    app.freePlayTheme = profile.settings.theme;
    app.trailTheme = profile.settings.theme;
    applySettings();
    return true;
}

// ---------------------------------------------------------------------------
// Other screen state
// ---------------------------------------------------------------------------

int leaderboardTab = 0;
int howToPage = 0;
float screenAge = 0.0f;

void drawStatLine(const char* label, const std::string& value, float x, float y, float width, Color color)
{
    drawText(FontKind::Body, label, x, y, px(24), Color{200, 200, 220, 255});
    drawTextRight(FontKind::Body, value.c_str(), x + width, y, px(24), color);
}

} // namespace

// ---------------------------------------------------------------------------
// Menu background
// ---------------------------------------------------------------------------

void initMenuBackground()
{
    demoTheme = ThemeId::Neon;
    demoTimer = 0.0f;
    buildDemo();
}

void unloadMenuBackground()
{
    endGame(demoState);
}

void invalidateMenuBackground()
{
    demoBuilt = false;
}

ThemeId menuTheme()
{
    return demoTheme;
}

void drawMenuBackground(float dt)
{
    demoTimer += dt;
    if (demoTimer > DEMO_SECONDS_PER_THEME)
    {
        demoTimer = 0.0f;
        demoTheme = static_cast<ThemeId>((static_cast<int>(demoTheme) + 1) % THEME_COUNT);
        demoBuilt = false;
        clearParticles();
    }
    if (!demoBuilt)
    {
        buildDemo();
        playMusic(MusicTrack::Menu);
    }

    const Theme& theme = currentMenuTheme();
    const float center = static_cast<float>(demoState.maze.tileCount) * 0.5f;
    const float angle = app.time * 0.07f;
    WorldView view;
    view.camera.position = Vector3{center + std::cos(angle) * 17.0f, 13.0f + 2.0f * std::sin(app.time * 0.2f),
                                   center + std::sin(angle) * 17.0f};
    view.camera.target = Vector3{center, 0.0f, center};
    view.camera.up = Vector3{0, 1, 0};
    view.camera.fovy = 50.0f;
    view.camera.projection = CAMERA_PERSPECTIVE;
    view.playerPosition = Vector3{center, 1.0f, center};
    view.topDown = true;
    view.time = app.time;

    demoState.elapsed += static_cast<double>(dt);
    updateParticles(dt);
    spawnAmbient(theme.ambientStyle, Vector3{center, 0, center}, dt, 12.0f);

    beginScene();
    std::vector<OpeningDoor> none;
    drawWorld(demoState, demoTheme, view, none);
    endScene();

    PostEffects effects;
    effects.bloomStrength = theme.bloomStrength;
    effects.bloomThreshold = theme.bloomThreshold;
    effects.saturation = theme.saturation;
    effects.crt = app.profile.settings.crtEffect;
    effects.time = app.time;
    // Fade to black around theme changes.
    const float edge = std::fmin(demoTimer, DEMO_SECONDS_PER_THEME - demoTimer);
    if (edge < 0.6f)
    {
        effects.flashColor = Color{0, 0, 0, static_cast<unsigned char>(255.0f * (1.0f - edge / 0.6f))};
    }
    drawSceneWithEffects(effects, app.profile.settings.bloom);

    // Darken so the menu text stays readable.
    DrawRectangleGradientH(0, 0, GetScreenWidth(), GetScreenHeight(), Color{0, 0, 0, 170}, Color{0, 0, 0, 60});
    updateParticles2D(dt);
    drawParticles2D();
}

// ---------------------------------------------------------------------------
// Splash
// ---------------------------------------------------------------------------

void enterSplash()
{
    screenAge = 0.0f;
    playMusic(MusicTrack::Menu);
}

void updateSplash(float dt)
{
    screenAge += dt;
    drawMenuBackground(dt);
    beginMenuUi();
    const float w = static_cast<float>(ui.width);
    const float h = static_cast<float>(ui.height);
    const float intro = std::fmin(1.0f, screenAge * 1.5f);

    drawRainbowTitle("LABYRINTH", w * 0.5f, h * 0.26f - (1.0f - intro) * px(80), px(120), app.time);
    drawGlowText(FontKind::Body, "A  3 D   M A Z E   A D V E N T U R E", w * 0.5f, h * 0.26f + px(140), px(30),
                 WHITE, currentMenuTheme().accent2);

    // Orbiting icons
    const Item icons[6] = {Item::Gem, Item::Torch, Item::Heart, Item::Compass, Item::Hourglass, Item::Teleporter};
    for (int i = 0; i < 6; ++i)
    {
        const float a = app.time * 0.6f + static_cast<float>(i) * PI / 3.0f;
        drawItemIcon(icons[i], Vector2{w * 0.5f + std::cos(a) * px(330), h * 0.62f + std::sin(a) * px(50)}, px(44),
                     app.time);
    }
    drawMinotaurIcon(Vector2{w * 0.5f, h * 0.62f}, px(70), Color{130, 70, 45, 255});

    const float blink = 0.5f + 0.5f * std::sin(app.time * 4.0f);
    drawTextCentered(FontKind::Display, "PRESS ANY KEY", w * 0.5f, h * 0.8f, px(30), withAlpha(WHITE, 0.4f + 0.6f * blink));
    drawTextCentered(FontKind::Body, "Arrow keys / WASD  -  Mouse  -  Gamepad", w * 0.5f, h * 0.8f + px(44), px(22),
                     Color{200, 200, 220, 200});
    drawTextCentered(FontKind::Body, "C++ & raylib  |  Team project 2026", w * 0.5f, h - px(36), px(20),
                     Color{180, 180, 200, 160});

    if (std::rand() % 6 == 0)
    {
        spawnBurst2D(Vector2{static_cast<float>(std::rand() % ui.width), static_cast<float>(std::rand() % ui.height)},
                     rainbow(app.time * 0.3f), 6, 120.0f, 4.0f);
    }

    const bool pressed = GetKeyPressed() != 0 || IsMouseButtonPressed(MOUSE_BUTTON_LEFT) ||
                         (IsGamepadAvailable(0) && GetGamepadButtonPressed() != GAMEPAD_BUTTON_UNKNOWN);
    if (pressed && screenAge > 0.5f)
    {
        playSfx(Sfx::MenuSelect);
        if (app.hasProfile || tryLoadLastProfile())
        {
            goTo(ScreenId::MainMenu);
        }
        else
        {
            goTo(ScreenId::Profiles);
        }
    }
    endUi();
}

// ---------------------------------------------------------------------------
// Profiles
// ---------------------------------------------------------------------------

void enterProfiles()
{
    scanProfiles();
    newProfileName.clear();
    profileError.clear();
    confirmDelete = -1;
}

void updateProfiles(float dt)
{
    drawMenuBackground(dt);
    beginMenuUi();
    const float w = static_cast<float>(ui.width);
    if (drawScreenHeader("PLAYER PROFILES", "Progress, stars, achievements and settings are saved per player") &&
        app.hasProfile)
    {
        goTo(ScreenId::MainMenu);
    }

    const float listX = w * 0.5f - px(470);
    float y = px(130);
    if (profileCards.empty())
    {
        drawTextCentered(FontKind::Body, "No profiles yet - create one below!", w * 0.5f, y + px(20), px(30), WHITE);
    }
    for (int i = 0; i < static_cast<int>(profileCards.size()) && i < 6; ++i)
    {
        const Profile& p = profileCards[static_cast<std::size_t>(i)].profile;
        const std::string subtitle = std::to_string(countTotalStars(p)) + " stars  |  " +
                                     std::to_string(p.stats.mazesCompleted) + " mazes escaped";
        if (button(p.name.c_str(), Rectangle{listX, y, px(760), px(62)}, true, subtitle.c_str()))
        {
            selectProfile(p);
        }
        const bool confirming = confirmDelete == i;
        if (button(confirming ? "SURE?" : "DELETE", Rectangle{listX + px(780), y + px(8), px(160), px(46)}))
        {
            if (confirming)
            {
                std::remove((app.saveDir + "/" + profileFileName(p.name)).c_str());
                if (app.hasProfile && app.profile.name == p.name)
                {
                    app.hasProfile = false;
                }
                scanProfiles();
                confirmDelete = -1;
                break;
            }
            confirmDelete = i;
        }
        y += px(74);
    }

    // New profile
    const float formY = static_cast<float>(ui.height) - px(170);
    drawTextCentered(FontKind::Display, "NEW PROFILE", w * 0.5f, formY - px(44), px(24), ui.accent2);
    textField("Name", Rectangle{listX, formY, px(640), px(58)}, newProfileName, MAX_PLAYER_NAME_LENGTH, false,
              "1-12 letters, digits, - _");
    const bool create = button("CREATE", Rectangle{listX + px(660), formY, px(280), px(58)});
    if (create)
    {
        const std::string name = trim(newProfileName);
        bool exists = false;
        for (const ProfileCard& card : profileCards)
        {
            exists = exists || card.profile.name == name;
        }
        if (!isValidPlayerName(name))
        {
            profileError = "Names need 1-12 letters or digits (spaces, - and _ allowed).";
            playSfx(Sfx::TrailWrong);
        }
        else if (exists)
        {
            profileError = "That profile already exists.";
            playSfx(Sfx::TrailWrong);
        }
        else
        {
            Profile profile;
            profile.name = name;
            selectProfile(profile);
        }
    }
    if (!profileError.empty())
    {
        drawTextCentered(FontKind::Body, profileError.c_str(), w * 0.5f, formY + px(70), px(24), Color{255, 90, 90, 255});
    }
    endUi();
}

// ---------------------------------------------------------------------------
// Main menu
// ---------------------------------------------------------------------------

void enterMainMenu()
{
    screenAge = 0.0f;
    playMusic(MusicTrack::Menu);
    announceAchievements();
}

void updateMainMenu(float dt)
{
    screenAge += dt;
    drawMenuBackground(dt);
    beginMenuUi();
    const float w = static_cast<float>(ui.width);
    const float h = static_cast<float>(ui.height);

    drawRainbowTitle("LABYRINTH", px(320), px(40), px(76), app.time);

    struct Entry
    {
        const char* label;
        ScreenId screen;
    };
    const bool canContinue = hasSavedRun();
    float y = px(146);
    const float x = px(110);
    const float bw = px(420);
    const float bh = px(42);
    const float gap = canContinue ? px(48) : px(52);

    if (canContinue && button("CONTINUE", Rectangle{x, y, bw, bh}))
    {
        app.resumeRequested = true;
        goTo(ScreenId::Game);
    }
    if (canContinue)
    {
        y += gap;
    }
    const Entry entries[] = {{"CAMPAIGN", ScreenId::Campaign},       {"CHALLENGES", ScreenId::Challenges},
                             {"FREE PLAY", ScreenId::FreePlay},     {"MAP MAKER", ScreenId::Maps},
                             {"NUMBER TRAIL", ScreenId::TrailSetup},
                             {"ACHIEVEMENTS", ScreenId::Achievements}, {"LEADERBOARDS", ScreenId::Leaderboards},
                             {"SETTINGS", ScreenId::Settings},      {"HOW TO PLAY", ScreenId::HowTo}};
    for (const Entry& entry : entries)
    {
        if (button(entry.label, Rectangle{x, y, bw, bh}))
        {
            app.settingsReturn = ScreenId::MainMenu;
            goTo(entry.screen);
        }
        y += gap;
    }
    if (button("QUIT", Rectangle{x, y, bw, bh}))
    {
        app.quit = true;
    }

    // Player card
    const Rectangle card{w - px(470), px(150), px(400), px(330)};
    drawPanel(card, withAlpha(ui.panel, 0.92f), ui.accent2);
    drawTextCentered(FontKind::Body, "PLAYER", card.x + card.width * 0.5f, card.y + px(16), px(22), ui.accent2);
    drawGlowText(FontKind::Display, app.profile.name.c_str(), card.x + card.width * 0.5f, card.y + px(44), px(36), WHITE,
                 ui.accent);
    const float sx = card.x + px(30);
    const float sw = card.width - px(60);
    drawStatLine("Campaign stars", std::to_string(countTotalStars(app.profile)) + " / " + std::to_string(LEVEL_COUNT * 3),
                 sx, card.y + px(110), sw, Color{255, 215, 60, 255});
    int achieved = 0;
    for (const bool a : app.profile.achievements)
    {
        achieved += a ? 1 : 0;
    }
    drawStatLine("Achievements", std::to_string(achieved) + " / " + std::to_string(ACHIEVEMENT_COUNT), sx,
                 card.y + px(146), sw, ui.accent2);
    drawStatLine("Mazes escaped", std::to_string(app.profile.stats.mazesCompleted), sx, card.y + px(182), sw, WHITE);
    drawStatLine("Gems collected", std::to_string(app.profile.stats.gemsCollected), sx, card.y + px(218), sw, WHITE);
    if (button("SWITCH PROFILE", Rectangle{card.x + px(40), card.y + card.height - px(70), card.width - px(80), px(48)}))
    {
        goTo(ScreenId::Profiles);
    }

    // World name ticker
    const Theme& theme = currentMenuTheme();
    drawTextRight(FontKind::Display, theme.name, w - px(70), h - px(90), px(30), theme.accent);
    drawTextRight(FontKind::Body, theme.tagline, w - px(70), h - px(52), px(22), Color{220, 220, 240, 200});
    endUi();
}

// ---------------------------------------------------------------------------
// Achievements
// ---------------------------------------------------------------------------

void enterAchievementsScreen()
{
    announceAchievements();
}

void updateAchievementsScreen(float dt)
{
    drawMenuBackground(dt);
    beginMenuUi();
    int achieved = 0;
    for (const bool a : app.profile.achievements)
    {
        achieved += a ? 1 : 0;
    }
    const std::string subtitle = std::to_string(achieved) + " of " + std::to_string(ACHIEVEMENT_COUNT) + " unlocked";
    if (drawScreenHeader("ACHIEVEMENTS", subtitle.c_str()))
    {
        goTo(ScreenId::MainMenu);
    }

    const int columns = 4;
    const float cardW = px(285);
    const float cardH = px(104);
    const float startX = static_cast<float>(ui.width) * 0.5f - (cardW * columns + px(12) * (columns - 1)) * 0.5f;
    const int stars = countTotalStars(app.profile);
    for (int i = 0; i < ACHIEVEMENT_COUNT; ++i)
    {
        const float x = startX + static_cast<float>(i % columns) * (cardW + px(12));
        const float y = px(124) + static_cast<float>(i / columns) * (cardH + px(10));
        const bool earned = app.profile.achievements[i];
        const AchievementInfo& info = getAchievementInfo(i);
        const Color gold{255, 215, 60, 255};
        drawPanel(Rectangle{x, y, cardW, cardH}, earned ? Color{50, 36, 10, 225} : Color{14, 14, 22, 215},
                  earned ? gold : Color{90, 90, 110, 255}, earned ? 1.5f : 0.4f);
        if (earned)
        {
            drawTrophy(Vector2{x + px(40), y + px(44)}, px(50), gold);
        }
        else
        {
            drawLock(Vector2{x + px(40), y + px(46)}, px(36), Color{110, 110, 130, 255});
        }
        drawText(FontKind::Body, info.name, x + px(78), y + px(8), px(25), earned ? gold : WHITE);
        drawWrappedText(FontKind::Body, info.description, x + px(78), y + px(36), cardW - px(88), px(18),
                        Color{200, 200, 215, 230});
        int current = 0;
        int target = 1;
        getAchievementProgress(i, app.profile.stats, stars, current, target);
        drawProgressBar(Rectangle{x + px(78), y + cardH - px(18), cardW - px(130), px(8)},
                        static_cast<float>(current) / static_cast<float>(target), earned ? gold : ui.accent2,
                        Color{0, 0, 0, 160});
        const std::string progress = std::to_string(current) + "/" + std::to_string(target);
        drawTextRight(FontKind::Body, progress.c_str(), x + cardW - px(10), y + cardH - px(25), px(17), WHITE);
    }
    endUi();
}

// ---------------------------------------------------------------------------
// Leaderboards
// ---------------------------------------------------------------------------

void enterLeaderboards()
{
}

void updateLeaderboards(float dt)
{
    drawMenuBackground(dt);
    beginMenuUi();
    if (drawScreenHeader("LEADERBOARDS", "Best scores of every player on this computer"))
    {
        goTo(ScreenId::MainMenu);
    }
    const float w = static_cast<float>(ui.width);
    const char* names[BOARD_COUNT];
    for (int i = 0; i < BOARD_COUNT; ++i)
    {
        names[i] = getBoardName(static_cast<BoardId>(i));
    }
    stepper("Board", Rectangle{w * 0.5f - px(330), px(122), px(660), px(54)}, leaderboardTab, 0, BOARD_COUNT - 1, names);

    const Rectangle table{w * 0.5f - px(400), px(196), px(800), px(420)};
    drawPanel(table, withAlpha(ui.panel, 0.92f), ui.accent);
    const float cols[5] = {table.x + px(40), table.x + px(130), table.x + px(420), table.x + px(560), table.x + px(680)};
    const char* headers[5] = {"#", "PLAYER", "SIZE", "TIME", "SCORE"};
    for (int i = 0; i < 5; ++i)
    {
        drawText(FontKind::Display, headers[i], cols[i], table.y + px(22), px(20), ui.accent2);
    }
    const Leaderboard& board = app.boards.boards[leaderboardTab];
    if (board.count == 0)
    {
        drawTextCentered(FontKind::Body, "No scores yet. Go set one!", table.x + table.width * 0.5f, table.y + px(180),
                         px(30), WHITE);
    }
    const Color medals[3] = {{255, 215, 60, 255}, {200, 210, 225, 255}, {215, 140, 80, 255}};
    for (int i = 0; i < board.count; ++i)
    {
        const ScoreEntry& e = board.entries[i];
        const float y = table.y + px(70) + static_cast<float>(i) * px(66);
        const float pulse = i == 0 ? 0.5f + 0.5f * std::sin(app.time * 3.0f) : 0.0f;
        DrawRectangleRounded(Rectangle{table.x + px(16), y - px(8), table.width - px(32), px(56)}, 0.3f, 6,
                             withAlpha(i < 3 ? medals[i] : WHITE, 0.08f + 0.06f * pulse));
        if (i < 3)
        {
            DrawCircleV(Vector2{cols[0] + px(10), y + px(20)}, px(18), medals[i]);
            drawTextCentered(FontKind::Display, std::to_string(i + 1).c_str(), cols[0] + px(10), y + px(8), px(22),
                             Color{30, 20, 10, 255});
        }
        else
        {
            drawText(FontKind::Display, std::to_string(i + 1).c_str(), cols[0], y + px(6), px(24), WHITE);
        }
        drawText(FontKind::Body, e.name.c_str(), cols[1], y + px(2), px(32), WHITE);
        const std::string size = std::to_string(e.mazeSize) + "x" + std::to_string(e.mazeSize);
        drawText(FontKind::Body, size.c_str(), cols[2], y + px(4), px(28), Color{210, 210, 230, 255});
        const std::string time = std::to_string(e.seconds / 60) + ":" + (e.seconds % 60 < 10 ? "0" : "") +
                                 std::to_string(e.seconds % 60);
        drawText(FontKind::Body, time.c_str(), cols[3], y + px(4), px(28), Color{210, 210, 230, 255});
        drawText(FontKind::Display, std::to_string(e.score).c_str(), cols[4], y + px(4), px(28),
                 i < 3 ? medals[i] : ui.accent2);
    }
    endUi();
}

// ---------------------------------------------------------------------------
// Settings
// ---------------------------------------------------------------------------

void enterSettings()
{
}

void updateSettings(float dt)
{
    drawMenuBackground(dt);
    beginMenuUi();
    AppSettings& s = app.profile.settings;
    if (drawScreenHeader("SETTINGS", "Saved with your profile"))
    {
        saveProfile();
        goTo(app.settingsReturn);
    }
    const float w = static_cast<float>(ui.width);
    const float leftX = w * 0.5f - px(530);
    const float rightX = w * 0.5f + px(20);
    const float rowW = px(510);
    const float rowH = px(54);
    float y = px(130);

    drawText(FontKind::Display, "AUDIO", leftX, y, px(22), ui.accent2);
    drawText(FontKind::Display, "GRAPHICS", rightX, y, px(22), ui.accent2);
    y += px(40);

    bool audioChanged = false;
    audioChanged |= slider("Master volume", Rectangle{leftX, y, rowW, rowH}, s.masterVolume, 0, 100, "%");
    // Shows the real window state, so the switch always matches the screen.
    bool fullscreen = IsWindowState(FLAG_BORDERLESS_WINDOWED_MODE);
    s.fullscreen = fullscreen;
    if (toggle("Fullscreen (F11 / Alt+Enter)", Rectangle{rightX, y, rowW, rowH}, fullscreen))
    {
        s.fullscreen = fullscreen;
        applySettings();
        saveProfile();
    }
    y += rowH + px(12);
    audioChanged |= slider("Music", Rectangle{leftX, y, rowW, rowH}, s.musicVolume, 0, 100, "%");
    toggle("Bloom glow", Rectangle{rightX, y, rowW, rowH}, s.bloom);
    y += rowH + px(12);
    audioChanged |= slider("Sound effects", Rectangle{leftX, y, rowW, rowH}, s.sfxVolume, 0, 100, "%");
    toggle("Retro CRT effect", Rectangle{rightX, y, rowW, rowH}, s.crtEffect);
    y += rowH + px(12);
    if (audioChanged)
    {
        setVolumes(s.masterVolume, s.musicVolume, s.sfxVolume);
    }

    toggle("Mute when in the background", Rectangle{leftX, y, rowW, rowH}, s.muteInBackground);
    toggle("Reduce motion & flashes", Rectangle{rightX, y, rowW, rowH}, s.reduceMotion);
    y += rowH + px(12);
    slider("Field of view", Rectangle{leftX, y, rowW, rowH}, s.fieldOfView, 60, 100, " deg");
    toggle("Show FPS (F3)", Rectangle{rightX, y, rowW, rowH}, s.showFps);
    y += rowH + px(12);
    static const char* const VIEW_NAMES[3] = {"Third person", "First person", "Top-down"};
    stepper("Starting view", Rectangle{leftX, y, rowW, rowH}, s.startView, 0, 2, VIEW_NAMES);
    const char* themeNames[THEME_COUNT];
    for (int i = 0; i < THEME_COUNT; ++i)
    {
        themeNames[i] = getThemeData(static_cast<ThemeId>(i)).name;
    }
    if (stepper("Free play world", Rectangle{rightX, y, rowW, rowH}, s.theme, 0, THEME_COUNT - 1, themeNames))
    {
        app.freePlayTheme = s.theme;
    }

    y += rowH + px(12);
    static const char* const DIFFICULTY_LABELS[DIFFICULTY_COUNT] = {"Easy", "Normal", "Hard", "Expert"};
    stepper("Difficulty", Rectangle{leftX, y, rowW, rowH}, s.difficulty, 0, DIFFICULTY_COUNT - 1, DIFFICULTY_LABELS);
    // Admin code: unlocks every level, star and achievement for this profile.
    if (app.profile.admin)
    {
        drawPanel(Rectangle{rightX, y, rowW, rowH}, withAlpha(Color{60, 45, 0, 255}, 0.9f), Color{255, 215, 60, 255});
        drawText(FontKind::Display, "ADMIN: EVERYTHING UNLOCKED  (F9 = warp to exit)", rightX + px(14), y + px(17), px(15),
                 Color{255, 215, 60, 255});
    }
    else
    {
        static std::string adminCode;
        textField("Admin code", Rectangle{rightX, y, rowW - px(120), rowH}, adminCode, 12, false, "code");
        if (button("UNLOCK", Rectangle{rightX + rowW - px(110), y, px(110), rowH}))
        {
            if (isAdminCode(adminCode))
            {
                unlockEverything(app.profile);
                saveProfile();
                showToast("ADMIN MODE", "Every level, star and achievement unlocked", Color{255, 215, 60, 255});
                playSfx(Sfx::Achievement);
            }
            else
            {
                showToast("WRONG CODE", "That is not the admin code", Color{255, 90, 90, 255});
                playSfx(Sfx::TrailWrong);
            }
            adminCode.clear();
        }
    }

    if (!isAudioAvailable())
    {
        drawTextCentered(FontKind::Body, "No audio device found - the game runs silently.", w * 0.5f,
                         static_cast<float>(ui.height) - px(60), px(22), Color{255, 160, 120, 255});
    }
    const std::string where = "Saved automatically on this device: " + app.saveDir;
    drawTextCentered(FontKind::Body, where.c_str(), w * 0.5f, static_cast<float>(ui.height) - px(30), px(16),
                     Color{180, 180, 200, 200});
    endUi();
}

// ---------------------------------------------------------------------------
// How to play
// ---------------------------------------------------------------------------

void enterHowTo()
{
    howToPage = 0;
}

void updateHowTo(float dt)
{
    drawMenuBackground(dt);
    beginMenuUi();
    const char* pages[4] = {"THE GOAL", "ITEMS", "DANGERS & STARS", "NUMBER TRAIL"};
    if (drawScreenHeader("HOW TO PLAY", pages[howToPage]))
    {
        goTo(ScreenId::MainMenu);
    }
    const float w = static_cast<float>(ui.width);
    const Rectangle panel{w * 0.5f - px(560), px(124), px(1120), px(500)};
    drawPanel(panel, withAlpha(ui.panel, 0.93f), ui.accent);
    const float x = panel.x + px(40);
    float y = panel.y + px(30);
    const Color soft{215, 215, 235, 255};

    switch (howToPage)
    {
    case 0:
    {
        drawWrappedText(FontKind::Body,
                        "You start in the top-left corner of the labyrinth. The exit - a glowing beam of light - is in "
                        "the bottom-right corner. Reach it to escape!",
                        x, y, panel.width - px(80), px(30), WHITE);
        y += px(100);
        const char* controls[][2] = {
            {"Arrow keys / WASD", "First person: forward, back, turn.  Top-down: move in any direction."},
            {"Q / E", "Step sideways (first person)"},
            {"V or TAB", "Switch between first person and the top-down view"},
            {"T", "Text view - the maze drawn with characters, seen from above"},
            {"M", "Full map of everything you have discovered"},
            {"H", "Hint: shows the shortest route (costs 100 points)"},
            {"SPACE", "Use the hammer on the wall in front of you"},
            {"ESC / P", "Pause, save & quit"},
            {"F11 / F12", "Fullscreen / screenshot"},
        };
        for (const auto& control : controls)
        {
            drawText(FontKind::Display, control[0], x, y, px(20), ui.accent2);
            drawText(FontKind::Body, control[1], x + px(270), y - px(2), px(25), soft);
            y += px(38);
        }
        break;
    }
    case 1:
    {
        const Item items[8] = {Item::Gem,   Item::Torch,   Item::Boots,   Item::Hammer,
                               Item::Hourglass, Item::Heart, Item::Compass, Item::Teleporter};
        for (int i = 0; i < 8; ++i)
        {
            const float ix = x + static_cast<float>(i % 2) * px(530);
            const float iy = y + static_cast<float>(i / 2) * px(100);
            DrawCircleV(Vector2{ix + px(34), iy + px(34)}, px(36), withAlpha(itemColor(items[i]), 0.15f));
            drawItemIcon(items[i], Vector2{ix + px(34), iy + px(34)}, px(52), app.time);
            drawText(FontKind::Display, itemName(items[i]), ix + px(84), iy + px(4), px(22), itemColor(items[i]));
            drawWrappedText(FontKind::Body, itemDescription(items[i]), ix + px(84), iy + px(32), px(420), px(22), soft);
        }
        drawKeyIcon(Vector2{x + px(34), y + px(434)}, px(48), Color{255, 215, 60, 255});
        drawText(FontKind::Body, "Keys open the glowing special doors - shortcuts towards the exit. There are fewer keys than doors!",
                 x + px(84), y + px(420), px(23), soft);
        break;
    }
    case 2:
    {
        drawMinotaurIcon(Vector2{x + px(40), y + px(40)}, px(64), Color{130, 70, 45, 255});
        drawText(FontKind::Display, "THE MINOTAUR", x + px(90), y + px(6), px(24), Color{255, 90, 70, 255});
        drawWrappedText(FontKind::Body,
                        "Wanders the maze until it smells you, then hunts you down. Its eyes turn red when it is hunting. "
                        "If it catches you, you lose a life. Hourglasses freeze it.",
                        x + px(90), y + px(38), px(950), px(24), soft);
        y += px(120);
        drawItemIcon(Item::Spikes, Vector2{x + px(40), y + px(36)}, px(56), app.time);
        drawText(FontKind::Display, "SPIKE TRAPS", x + px(90), y + px(6), px(24), WHITE);
        drawWrappedText(FontKind::Body, "They rise and fall in a rhythm. Wait for them to go down, then cross.",
                        x + px(90), y + px(38), px(950), px(24), soft);
        y += px(100);
        drawText(FontKind::Display, "FOG OF WAR", x + px(90), y + px(6), px(24), Color{160, 200, 255, 255});
        drawWrappedText(FontKind::Body, "In dark levels you only see what is near you. Torches help.", x + px(90),
                        y + px(38), px(950), px(24), soft);
        y += px(100);
        for (int i = 0; i < 3; ++i)
        {
            drawStar(Vector2{x + px(20) + static_cast<float>(i) * px(34), y + px(30)}, px(16), Color{255, 215, 60, 255});
        }
        drawText(FontKind::Display, "STARS", x + px(130), y + px(6), px(24), Color{255, 215, 60, 255});
        drawWrappedText(FontKind::Body,
                        "1 star: escape.  2 stars: escape within the par time.  3 stars: within par, no hints, no lives "
                        "lost and every gem collected.",
                        x + px(130), y + px(38), px(910), px(24), soft);
        break;
    }
    default:
    {
        drawWrappedText(FontKind::Body,
                        "Every square holds a number. From your square you may move RIGHT, DOWN or DIAGONALLY "
                        "down-right - and you must always step onto the SMALLEST number you can reach. Three wrong steps "
                        "and the trail is lost.",
                        x, y, panel.width - px(80), px(28), WHITE);
        y += px(130);
        const int demo[3][3] = {{0, 7, 15}, {4, 2, 9}, {11, 6, 3}};
        for (int r = 0; r < 3; ++r)
        {
            for (int c = 0; c < 3; ++c)
            {
                const Rectangle cell{x + px(60) + static_cast<float>(c) * px(84), y + static_cast<float>(r) * px(84),
                                     px(76), px(76)};
                const bool onPath = (r == 0 && c == 0) || (r == 1 && c == 1) || (r == 2 && c == 2);
                DrawRectangleRounded(cell, 0.25f, 6, onPath ? withAlpha(ui.accent, 0.7f) : Color{30, 30, 45, 230});
                drawTextCentered(FontKind::Display, std::to_string(demo[r][c]).c_str(), cell.x + cell.width * 0.5f,
                                 cell.y + px(20), px(32), WHITE);
            }
        }
        drawWrappedText(FontKind::Body,
                        "From 0 you can reach 7, 4 or 2: the smallest is 2 (diagonal). From 2 you can reach 9, 6 or 3: "
                        "go to 3. Quick correct steps build a COMBO for bonus points. Use the arrow keys, E / 3 / Page "
                        "Down for diagonal - or click the square.",
                        x + px(360), y, px(680), px(26), soft);
        break;
    }
    }

    if (button("< PREVIOUS", Rectangle{panel.x, panel.y + panel.height + px(20), px(220), px(50)}, howToPage > 0))
    {
        --howToPage;
    }
    if (button("NEXT >", Rectangle{panel.x + panel.width - px(220), panel.y + panel.height + px(20), px(220), px(50)},
               howToPage < 3))
    {
        ++howToPage;
    }
    for (int i = 0; i < 4; ++i)
    {
        DrawCircleV(Vector2{w * 0.5f + static_cast<float>(i - 1) * px(28) - px(14), panel.y + panel.height + px(45)},
                    px(i == howToPage ? 8.0f : 5.0f), i == howToPage ? ui.accent2 : Color{120, 120, 140, 255});
    }
    endUi();
}
