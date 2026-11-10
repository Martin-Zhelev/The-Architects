// main.cpp
// Labyrinth - the graphical app.
// Opens the window, loads everything, runs the main loop and switches
// between screens with an animated transition. Also saves and loads
// profiles, leaderboards and the "Continue" game.
//
// Command line:
//   --tour <folder>   automatic screenshot tour of every screen (used by QA)

#include "app.h"
#include "embedded_icon.h"

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <ctime>
#include <filesystem>

App app;

namespace
{

enum class TransitionPhase
{
    None,
    Covering,
    Revealing
};

TransitionPhase phase = TransitionPhase::None;
float transitionTime = 0.0f;
const float TRANSITION_SECONDS = 0.28f;

void enterScreen(ScreenId screen)
{
    resetFocus();
    switch (screen)
    {
    case ScreenId::Splash:
        enterSplash();
        break;
    case ScreenId::Profiles:
        enterProfiles();
        break;
    case ScreenId::MainMenu:
        enterMainMenu();
        break;
    case ScreenId::Campaign:
        enterCampaign();
        break;
    case ScreenId::Challenges:
        enterChallenges();
        break;
    case ScreenId::FreePlay:
        enterFreePlay();
        break;
    case ScreenId::Game:
        enterGame();
        break;
    case ScreenId::TrailSetup:
        enterTrailSetup();
        break;
    case ScreenId::Trail:
        enterTrail();
        break;
    case ScreenId::Achievements:
        enterAchievementsScreen();
        break;
    case ScreenId::Leaderboards:
        enterLeaderboards();
        break;
    case ScreenId::Settings:
        enterSettings();
        break;
    case ScreenId::HowTo:
        enterHowTo();
        break;
    case ScreenId::Maps:
        enterMaps();
        break;
    case ScreenId::Editor:
        enterEditor();
        break;
    }
}

void leaveScreen(ScreenId screen)
{
    if (screen == ScreenId::Game)
    {
        leaveGame();
    }
    else if (screen == ScreenId::Trail)
    {
        leaveTrail();
    }
}

void updateScreen(ScreenId screen, float dt)
{
    switch (screen)
    {
    case ScreenId::Splash:
        updateSplash(dt);
        break;
    case ScreenId::Profiles:
        updateProfiles(dt);
        break;
    case ScreenId::MainMenu:
        updateMainMenu(dt);
        break;
    case ScreenId::Campaign:
        updateCampaign(dt);
        break;
    case ScreenId::Challenges:
        updateChallenges(dt);
        break;
    case ScreenId::FreePlay:
        updateFreePlay(dt);
        break;
    case ScreenId::Game:
        updateGame(dt);
        break;
    case ScreenId::TrailSetup:
        updateTrailSetup(dt);
        break;
    case ScreenId::Trail:
        updateTrail(dt);
        break;
    case ScreenId::Achievements:
        updateAchievementsScreen(dt);
        break;
    case ScreenId::Leaderboards:
        updateLeaderboards(dt);
        break;
    case ScreenId::Settings:
        updateSettings(dt);
        break;
    case ScreenId::HowTo:
        updateHowTo(dt);
        break;
    case ScreenId::Maps:
        updateMaps(dt);
        break;
    case ScreenId::Editor:
        updateEditor(dt);
        break;
    }
}

// Diagonal neon stripes sweep across the screen between pages.
void drawTransition(float amount)
{
    if (amount <= 0.0f)
    {
        return;
    }
    const int width = GetScreenWidth();
    const int height = GetScreenHeight();
    const float diagonal = static_cast<float>(width + height);
    const int stripes = 9;
    for (int i = 0; i < stripes; ++i)
    {
        const float delay = static_cast<float>(i) / static_cast<float>(stripes) * 0.45f;
        const float t = std::fmax(0.0f, std::fmin(1.0f, (amount - delay) / 0.55f));
        if (t <= 0.0f)
        {
            continue;
        }
        const float stripeWidth = diagonal / static_cast<float>(stripes) + 4.0f;
        const float x = static_cast<float>(i) * (diagonal / stripes) - static_cast<float>(height);
        const Color color = i % 2 == 0 ? lerpColor(ui.accent, BLACK, 0.55f) : Color{8, 6, 16, 255};
        const Vector2 a{x, 0};
        const Vector2 b{x + stripeWidth, 0};
        const Vector2 c{x + stripeWidth + static_cast<float>(height) * t, static_cast<float>(height) * t};
        const Vector2 d{x + static_cast<float>(height) * t, static_cast<float>(height) * t};
        DrawTriangle(a, d, c, color);
        DrawTriangle(a, c, b, color);
    }
    DrawRectangle(0, 0, width, height, withAlpha(BLACK, std::fmax(0.0f, amount - 0.6f) * 2.5f));
}

// Where this device keeps profiles, settings, saved games and maps for the
// person logged in to the computer:
//   Windows  %LOCALAPPDATA%\Labyrinth   (C:\Users\<you>\AppData\Local\Labyrinth)
//   macOS    ~/Library/Application Support/Labyrinth
//   Linux    ~/.local/share/labyrinth
// Saving next to the .exe is fragile: started from inside a zip, Windows runs
// it from a temporary folder that is wiped later, and some folders can't be
// written at all. A file "portable.txt" next to the program keeps everything
// next to it instead (e.g. for a USB stick).
std::string userDataFolder()
{
    const std::string appFolder = GetApplicationDirectory();
    if (std::filesystem::exists(appFolder + "portable.txt"))
    {
        return appFolder;
    }
#if defined(_WIN32)
    const char* base = std::getenv("LOCALAPPDATA");
    if (base == nullptr || *base == '\0')
    {
        base = std::getenv("APPDATA");
    }
    if (base != nullptr && *base != '\0')
    {
        return std::string(base) + "/Labyrinth/";
    }
#elif defined(__APPLE__)
    const char* home = std::getenv("HOME");
    if (home != nullptr && *home != '\0')
    {
        return std::string(home) + "/Library/Application Support/Labyrinth/";
    }
#else
    const char* xdg = std::getenv("XDG_DATA_HOME");
    if (xdg != nullptr && *xdg != '\0')
    {
        return std::string(xdg) + "/labyrinth/";
    }
    const char* home = std::getenv("HOME");
    if (home != nullptr && *home != '\0')
    {
        return std::string(home) + "/.local/share/labyrinth/";
    }
#endif
    return appFolder;
}

// True if the folder exists (or was created) and a file can be written in it.
bool isWritableFolder(const std::string& folder)
{
    try
    {
        std::filesystem::create_directories(folder);
    }
    catch (const std::exception&)
    {
        return false;
    }
    const std::string probe = folder + "/.write_test";
    if (!writeTextFile(probe, "ok"))
    {
        return false;
    }
    std::remove(probe.c_str());
    return true;
}

// Brings files that sit next to the program (the ready-made Admin profile,
// or saves from an older version) into the device folder, once. Never
// overwrites anything already there.
void copyMissingFiles(const std::string& from, const std::string& to)
{
    if (from == to)
    {
        return;
    }
    try
    {
        if (!std::filesystem::is_directory(from))
        {
            return;
        }
        for (const auto& entry : std::filesystem::directory_iterator(from))
        {
            const std::filesystem::path target = std::filesystem::path(to) / entry.path().filename();
            if (entry.is_regular_file() && !std::filesystem::exists(target))
            {
                std::filesystem::copy_file(entry.path(), target);
            }
        }
    }
    catch (const std::exception&)
    {
        // best effort: the game works without the old files
    }
}

void setupSaveFolder()
{
    const std::string appFolder = GetApplicationDirectory();
    std::string root = userDataFolder();
    if (!isWritableFolder(root + "saves") || !isWritableFolder(root + "maps"))
    {
        root = appFolder; // fall back to the old place
        if (!isWritableFolder(root + "saves"))
        {
            root = "./";
        }
    }
    app.dataDir = root;
    app.saveDir = root + "saves";
    copyMissingFiles(appFolder + "saves", app.saveDir);
    copyMissingFiles(appFolder + "maps", root + "maps");

    std::string text;
    if (readTextFile(app.saveDir + "/leaderboards.txt", text))
    {
        deserializeLeaderboards(text, app.boards);
    }
}

Image makeWindowIcon()
{
    // The same picture as the .exe icon (both made by tools/make_icon.py).
    Image image = LoadImageFromMemory(".png", ICON_PNG_DATA, ICON_PNG_SIZE);
    ImageFormat(&image, PIXELFORMAT_UNCOMPRESSED_R8G8B8A8);
    return image;
}

// ---- Automatic screenshot tour (for QA and documentation) ----

struct TourStep
{
    ScreenId screen;
    float wait;
    const char* name;
    int action; // 0 = none, 1 = start campaign level, 2 = start top-down, 3 = text view, 4 = trail, 5 = map
};

bool tourActive = false;
std::string tourFolder;
int tourIndex = -1;
float tourTimer = 0.0f;

} // namespace

// Lets the game screen react to tour actions (declared in screen_game.cpp).
void tourGameAction(int action);
int tourLevel = 0;

namespace
{

const TourStep TOUR[] = {
    {ScreenId::Splash, 1.4f, "01_splash", 0},
    {ScreenId::MainMenu, 1.4f, "02_main_menu", 0},
    {ScreenId::Campaign, 1.2f, "03_campaign", 0},
    {ScreenId::Challenges, 1.2f, "04_challenges", 0},
    {ScreenId::FreePlay, 1.2f, "05_free_play", 0},
    {ScreenId::Game, 2.0f, "06_game_neon_third_person", 1},
    {ScreenId::Game, 1.2f, "06b_game_neon_first_person", 8},
    {ScreenId::Game, 1.2f, "07_game_neon_top_down", 2},
    {ScreenId::Game, 1.5f, "07b_game_items", 7},
    {ScreenId::Game, 1.2f, "08_game_text_view", 3},
    {ScreenId::Game, 1.2f, "09_game_map", 5},
    {ScreenId::Game, 2.0f, "10_game_dungeon", 11},
    {ScreenId::Game, 1.2f, "11_game_dungeon_top", 2},
    {ScreenId::Game, 1.5f, "11b_minotaur", 6},
    {ScreenId::Game, 2.0f, "12_game_lava", 12},
    {ScreenId::Game, 1.2f, "13_game_lava_top", 2},
    {ScreenId::Game, 2.0f, "14_game_ice", 13},
    {ScreenId::Game, 2.0f, "15_game_candy", 14},
    {ScreenId::Game, 1.2f, "16_game_candy_top", 2},
    {ScreenId::Game, 2.5f, "17_victory", 20},
    {ScreenId::Game, 2.0f, "18_pause", 21},
    {ScreenId::TrailSetup, 1.2f, "19_trail_setup", 0},
    {ScreenId::Trail, 1.5f, "20_trail", 4},
    {ScreenId::Achievements, 1.2f, "21_achievements", 0},
    {ScreenId::Leaderboards, 1.2f, "22_leaderboards", 0},
    {ScreenId::Settings, 1.2f, "23_settings", 0},
    {ScreenId::HowTo, 1.2f, "24_how_to_play", 0},
    {ScreenId::Profiles, 1.2f, "25_profiles", 0},
    {ScreenId::Maps, 1.2f, "26_map_list", 30},
    {ScreenId::Editor, 1.5f, "27_map_editor", 31},
};
const int TOUR_LENGTH = static_cast<int>(sizeof(TOUR) / sizeof(TOUR[0]));

void runTour(float dt)
{
    if (!tourActive)
    {
        return;
    }
    tourTimer -= dt;
    if (tourTimer > 0.0f || phase != TransitionPhase::None)
    {
        return;
    }
    if (tourIndex >= 0)
    {
        const std::string file = tourFolder + "/" + TOUR[tourIndex].name + ".png";
        Image shot = LoadImageFromScreen();
        ExportImage(shot, file.c_str());
        UnloadImage(shot);
    }
    ++tourIndex;
    if (tourIndex >= TOUR_LENGTH)
    {
        app.quit = true;
        return;
    }
    const TourStep& step = TOUR[tourIndex];
    tourTimer = step.wait;
    const int action = step.action;
    if (action == 1 || (action >= 11 && action <= 14))
    {
        const int showcaseLevels[4] = {5, 7, 9, 13};
        tourLevel = action == 1 ? 2 : showcaseLevels[action - 11];
        app.pendingRun.mode = RunMode::Campaign;
        app.pendingRun.index = tourLevel;
        app.pendingRun.theme = getLevel(tourLevel).theme;
        app.pendingRun.config = getLevel(tourLevel).config;
    }
    if (step.screen != app.screen || action == 1 || (action >= 11 && action <= 14))
    {
        if (step.screen == app.screen)
        {
            leaveScreen(app.screen);
            enterScreen(app.screen);
        }
        else
        {
            goTo(step.screen);
        }
    }
    if (action == 30 || action == 31)
    {
        // An example map for the map maker screens.
        CustomMap map;
        createGeneratedMap(map, 14, 2026);
        map.name = "Tour Example";
        map.author = "Tour";
        map.theme = 2;
        setMapItem(map, 2, 3, Item::Gem);
        setMapItem(map, 5, 7, Item::Gem);
        setMapItem(map, 8, 2, Item::Spikes);
        setMapItem(map, 3, 10, Item::Teleporter);
        setMapItem(map, 11, 4, Item::Teleporter);
        setMapItem(map, 6, 12, Item::Teleporter);
        setMapItem(map, 12, 9, Item::Teleporter);
        setMapItem(map, 9, 9, Item::Boots);
        toggleMapMinotaur(map, 10, 11);
        app.editorMap = map;
        app.editorFile.clear();
        if (action == 30)
        {
            CustomMap verified = map;
            markMapVerified(verified, 88, 40);
            saveCustomMap(verified);
            app.editorFile.clear();
        }
    }
    if (action == 4)
    {
        app.trailSize = 14;
    }
    tourGameAction(action);
}

} // namespace

// ---------------------------------------------------------------------------
// Shared helpers
// ---------------------------------------------------------------------------

void goTo(ScreenId screen)
{
    if (phase != TransitionPhase::None)
    {
        return;
    }
    app.nextScreen = screen;
    phase = TransitionPhase::Covering;
    transitionTime = 0.0f;
    playSfx(Sfx::Whoosh, 1.0f, 0.6f);
}

void saveProfile()
{
    if (!app.hasProfile)
    {
        return;
    }
    const std::string text = serializeProfile(app.profile);
    const bool saved = writeTextFile(app.saveDir + "/" + profileFileName(app.profile.name), text) &&
                       writeTextFile(app.saveDir + "/last_profile.txt", app.profile.name);
    if (saved)
    {
        app.lastSavedProfile = text;
    }
    else if (app.lastSavedProfile != "!failed")
    {
        app.lastSavedProfile = "!failed"; // warn once, keep trying
        showToast("COULD NOT SAVE", "Your profile could not be written to " + app.saveDir, Color{255, 90, 80, 255});
    }
}

// Settings and progress are written as soon as they change (checked once a
// second), so nothing is lost if the game is closed unexpectedly.
void autosaveProfile(float dt)
{
    static float timer = 0.0f;
    timer += dt;
    if (timer < 1.0f || !app.hasProfile || app.tourMode)
    {
        return;
    }
    timer = 0.0f;
    if (serializeProfile(app.profile) != app.lastSavedProfile)
    {
        saveProfile();
    }
}

void saveLeaderboards()
{
    writeTextFile(app.saveDir + "/leaderboards.txt", serializeLeaderboards(app.boards));
}

std::string runSavePath()
{
    std::string name = profileFileName(app.profile.name);
    name = name.substr(0, name.size() - 8); // remove ".profile"
    return app.saveDir + "/" + name + ".run";
}

bool hasSavedRun()
{
    return app.hasProfile && FileExists(runSavePath().c_str());
}

void applySettings()
{
    const AppSettings& s = app.profile.settings;
    setVolumes(s.masterVolume, s.musicVolume, s.sfxVolume);
    // The game uses borderless fullscreen, so ask raylib about THAT mode
    // (IsWindowFullscreen() only reports exclusive fullscreen, which made
    // the OFF switch think there was nothing to undo).
    const bool borderless = IsWindowState(FLAG_BORDERLESS_WINDOWED_MODE);
    if (IsWindowFullscreen())
    {
        ToggleFullscreen(); // never stay stuck in exclusive fullscreen
    }
    if (s.fullscreen != borderless)
    {
        ToggleBorderlessWindowed();
    }
    if (!s.fullscreen && (GetScreenWidth() < 640 || GetScreenHeight() < 360))
    {
        SetWindowSize(1280, 720);
    }
}

void announceAchievements()
{
    int earned[ACHIEVEMENT_COUNT];
    const int count = updateAchievements(app.profile, earned);
    for (int i = 0; i < count; ++i)
    {
        showToast("ACHIEVEMENT UNLOCKED", getAchievementInfo(earned[i]).name, Color{255, 215, 60, 255});
    }
    if (count > 0)
    {
        playSfx(Sfx::Achievement);
        saveProfile();
    }
}

void getToday(int& year, int& month, int& day)
{
    const std::time_t now = std::time(nullptr);
    const std::tm* local = std::localtime(&now);
    year = local->tm_year + 1900;
    month = local->tm_mon + 1;
    day = local->tm_mday;
}

bool drawScreenHeader(const char* title, const char* subtitle)
{
    drawGlowText(FontKind::Display, title, static_cast<float>(ui.width) * 0.5f, px(26), px(46), WHITE, ui.accent);
    if (subtitle != nullptr)
    {
        drawTextCentered(FontKind::Body, subtitle, static_cast<float>(ui.width) * 0.5f, px(84), px(24),
                         withAlpha(ui.accent2, 0.9f));
    }
    const bool back = button("< BACK", Rectangle{px(24), px(24), px(130), px(46)}) || ui.back;
    if (ui.back)
    {
        playSfx(Sfx::MenuSelect);
    }
    return back;
}

// ---------------------------------------------------------------------------
// Main
// ---------------------------------------------------------------------------

int main(int argc, char** argv)
{
    for (int i = 1; i < argc; ++i)
    {
        if (std::string(argv[i]) == "--tour" && i + 1 < argc)
        {
            tourActive = true;
            tourFolder = argv[i + 1];
        }
    }

    SetConfigFlags(FLAG_WINDOW_RESIZABLE | FLAG_VSYNC_HINT | FLAG_MSAA_4X_HINT);
    InitWindow(1280, 720, "LABYRINTH");
    SetWindowMinSize(960, 540);
    SetExitKey(KEY_NULL); // Esc opens menus instead of closing the window
    SetTargetFPS(120);
    // Several sizes, so Windows can pick a sharp one for the title bar (16),
    // the taskbar (24-48) and Alt+Tab (64+), the same art as the .exe icon.
    Image icon = makeWindowIcon();
    const int ICON_SIZES[] = {16, 24, 32, 48, 64, 128};
    Image icons[6];
    for (int i = 0; i < 6; ++i)
    {
        icons[i] = ImageCopy(icon);
        ImageResize(&icons[i], ICON_SIZES[i], ICON_SIZES[i]);
    }
    SetWindowIcons(icons, 6);
    for (Image& image : icons)
    {
        UnloadImage(image);
    }
    UnloadImage(icon);

    initAudio();
    loadFonts();
    loadThemes();
    initParticles();
    initRenderer();
    initPostFx(GetScreenWidth(), GetScreenHeight());
    setupSaveFolder();
    initMenuBackground();

    app.freePlayConfig.size = 20;
    app.freePlayConfig.gemCount = 8;
    app.freePlayConfig.powerUps = true;
    app.freePlayConfig.minotaurCount = 1;
    app.freePlayConfig.teleporterPairs = 1;

    if (tourActive)
    {
        app.hasProfile = true;
        app.profile.name = "Tour";
        app.profile.settings.masterVolume = 0;
        app.tourMode = true;
        app.saveDir = tourFolder;
        app.dataDir = tourFolder + "/";
        app.profile.levelStars[0] = 3;
        app.profile.levelStars[1] = 2;
        app.profile.levelStars[2] = 1;
        app.profile.achievements[0] = true;
        app.profile.achievements[2] = true;
        app.profile.stats.mazesCompleted = 3;
        ScoreEntry entry;
        const char* names[5] = {"Ana QA", "Max", "Lee", "Sam", "Jo"};
        for (int i = 0; i < 5; ++i)
        {
            entry.name = names[i];
            entry.score = 3000 - i * 420;
            entry.mazeSize = 25;
            entry.moves = 210 + i * 30;
            entry.seconds = 95 + i * 12;
            addToLeaderboard(app.boards.boards[0], entry);
        }
    }
    enterScreen(app.screen);

    while (!WindowShouldClose() && !app.quit)
    {
        float dt = GetFrameTime();
        if (tourActive)
        {
            dt = 1.0f / 30.0f; // the tour runs at a fixed pace even on slow machines
        }
        dt = dt > 0.05f ? 0.05f : dt;
        app.time += dt;
        ui.time = app.time;

        if (IsWindowResized())
        {
            resizePostFx(GetScreenWidth(), GetScreenHeight());
        }
        // F11 or Alt+Enter: always toggles, on every screen.
        if (IsKeyPressed(KEY_F11) ||
            ((IsKeyDown(KEY_LEFT_ALT) || IsKeyDown(KEY_RIGHT_ALT)) && IsKeyPressed(KEY_ENTER)))
        {
            app.profile.settings.fullscreen = !app.profile.settings.fullscreen;
            applySettings();
            saveProfile();
        }
        autosaveProfile(dt);
        // Mute while another window is in front (or the game is minimised).
        setBackgroundMute(app.profile.settings.muteInBackground && (!IsWindowFocused() || IsWindowMinimized()));
        updateAudio(dt);
        if (IsFileDropped())
        {
            importDroppedMaps();
        }

        // Page transition
        float cover = 0.0f;
        if (phase == TransitionPhase::Covering)
        {
            transitionTime += dt;
            cover = transitionTime / TRANSITION_SECONDS;
            if (cover >= 1.0f)
            {
                leaveScreen(app.screen);
                app.screen = app.nextScreen;
                clearParticles2D();
                enterScreen(app.screen);
                phase = TransitionPhase::Revealing;
                transitionTime = TRANSITION_SECONDS;
                cover = 1.0f;
            }
        }
        else if (phase == TransitionPhase::Revealing)
        {
            transitionTime -= dt;
            cover = transitionTime / TRANSITION_SECONDS;
            if (cover <= 0.0f)
            {
                phase = TransitionPhase::None;
                cover = 0.0f;
            }
        }

        BeginDrawing();
        ClearBackground(BLACK);
        updateScreen(app.screen, dt);
        drawTransition(cover);
        updateAndDrawToasts(dt);
        // F3 shows/hides the frame counter anywhere (also in Settings).
        if (IsKeyPressed(KEY_F3))
        {
            app.profile.settings.showFps = !app.profile.settings.showFps;
            saveProfile();
        }
        if (app.profile.settings.showFps)
        {
            drawFpsCounter();
        }
        EndDrawing();

        if (IsKeyPressed(KEY_F12))
        {
            const std::string name = "labyrinth_" + std::to_string(static_cast<long long>(std::time(nullptr))) + ".png";
            TakeScreenshot(name.c_str());
            showToast("SCREENSHOT", name, Color{60, 230, 255, 255});
        }
        runTour(dt);
    }

    leaveScreen(app.screen);
    saveProfile();
    unloadMenuBackground();
    unloadPostFx();
    unloadRenderer();
    unloadParticles();
    unloadThemes();
    unloadFonts();
    unloadAudio();
    CloseWindow();
    return 0;
}
