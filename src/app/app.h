// app.h
// Shared declarations for the graphical Labyrinth app (raylib).
//
// The app is split into modules:
//   theme.cpp      colour palettes, procedural textures and skies for the 5 worlds
//   render3d.cpp   3D maze meshes, lighting shader, props, Minotaurs, cameras
//   postfx.cpp     bloom, scanlines, chromatic aberration, vignette, flashes
//   particles.cpp  3D and 2D particle effects
//   audio.cpp      synthesized sound effects and procedural music
//   widgets.cpp    fonts, buttons, sliders, text fields, icons, transitions
//   screen_*.cpp   every screen of the app
//   main.cpp       window, main loop, saving and loading
//
// All game rules live in the pure-logic modules (../game.h etc.).
// This layer only reads the game state, draws it and turns key presses into
// calls such as movePlayer().

#ifndef APP_H
#define APP_H

// raylib's headers are not ours: don't let our strict warnings flag them.
#if defined(__GNUC__)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wmissing-field-initializers"
#endif
#include "raylib.h"
#include "raymath.h"
#if defined(__GNUC__)
#pragma GCC diagnostic pop
#endif

#include "../campaign.h"
#include "../custom_map.h"
#include "../game.h"
#include "../number_trail.h"
#include "../profile.h"
#include "../savegame.h"
#include "../validation.h"

#include <string>
#include <vector>

// ===========================================================================
// Themes (theme.cpp)
// ===========================================================================

enum class AmbientStyle
{
    NeonDust,
    DungeonMotes,
    Embers,
    Snow,
    Sprinkles
};

struct Theme
{
    const char* name;
    const char* tagline;
    Color accent;   // main UI colour
    Color accent2;  // second UI colour
    Color accent3;  // highlights
    Color panel;    // UI panel background
    Color skyTop;
    Color skyBottom;
    Color fog;
    float fogDensity;
    Color ambient;
    Color torch;
    float torchRadius;
    Color wallTop;
    Color door;
    Color key;
    Color gem;
    Color exit;
    Color hint;
    Color player;
    float bloomStrength;
    float bloomThreshold;
    float saturation;
    AmbientStyle ambientStyle;
    Texture2D wallTexture;
    Texture2D floorTexture;
    Texture2D groundTexture;
};

void loadThemes();
void unloadThemes();
Theme& getThemeData(ThemeId id);
void drawSky(const Theme& theme, ThemeId id, int width, int height, float time, float yaw);
Color withAlpha(Color color, float alpha);
Color lerpColor(Color a, Color b, float t);
Color rainbow(float t, float saturation = 0.8f, float value = 1.0f);

// ===========================================================================
// Particles (particles.cpp)
// ===========================================================================

void initParticles();
void unloadParticles();
void clearParticles();
void updateParticles(float dt);
void drawParticles3D(Camera3D camera);
void spawnBurst(Vector3 position, Color color, int count, float speed, float size = 0.12f);
void spawnConfetti3D(Vector3 position, int count);
void spawnAmbient(AmbientStyle style, Vector3 around, float dt, float radius);
void spawnRing(Vector3 position, Color color, int count, float radius);

void clearParticles2D();
void updateParticles2D(float dt);
void drawParticles2D();
void spawnBurst2D(Vector2 position, Color color, int count, float speed, float size = 6.0f);
void spawnConfetti2D(int screenWidth, int count);
Texture2D getGlowTexture();

// ===========================================================================
// Post-processing (postfx.cpp)
// ===========================================================================

struct PostEffects
{
    float bloomStrength = 0.8f;
    float bloomThreshold = 0.6f;
    float saturation = 1.1f;
    bool crt = true;
    Color flashColor = {0, 0, 0, 0}; // alpha = strength
    float danger = 0.0f;            // red pulsing edges
    float freeze = 0.0f;            // icy blue edges
    float time = 0.0f;
};

void initPostFx(int width, int height);
void unloadPostFx();
void resizePostFx(int width, int height);
void beginScene();
void endScene();
void drawSceneWithEffects(const PostEffects& effects, bool bloomEnabled);

// ===========================================================================
// 3D world (render3d.cpp)
// ===========================================================================

const float WALL_HEIGHT = 1.5f;
const float EYE_HEIGHT = 0.72f;

void initRenderer();
void unloadRenderer();
// Builds meshes for the maze (call again when walls change).
void buildWorld(const GameState& state, ThemeId theme);
void unloadWorld();
// World position of the centre of a cell.
Vector3 cellCenter(int row, int col);

struct WorldView
{
    Camera3D camera;
    Vector3 playerPosition; // for the player avatar and the torch light
    float playerYaw = 0.0f;
    bool showPlayer = false;
    bool topDown = false; // camera far above: less fog, longer draw distance
    float time = 0.0f;
    bool hideUndiscovered = false; // fog of war in the top-down view
    float fogScale = 1.0f;         // distance fog: 1 first person, less from higher cameras
    const std::vector<Vector3>* minotaurPositions = nullptr;
    const std::vector<float>* minotaurYaws = nullptr;
    const std::vector<float>* minotaurWalk = nullptr;
};

struct OpeningDoor
{
    int tileRow;
    int tileCol;
    float progress; // 0 = closed, 1 = fully sunk into the floor
};

void drawWorld(const GameState& state, ThemeId theme, const WorldView& view,
               const std::vector<OpeningDoor>& openingDoors);
// Each teleporter pair has its own colour, so you can see which pads belong together.
int teleporterPairIndex(const GameState& state, int row, int col); // 0, 1, 2
Color teleporterPairColor(int pairIndex);
Color teleporterColor(const GameState& state, int row, int col);

// ===========================================================================
// Audio (audio.cpp)
// ===========================================================================

enum class Sfx
{
    Step,
    Bump,
    Key,
    Gem,
    PowerUp,
    Door,
    Teleport,
    Hurt,
    Roar,
    Heartbeat,
    WallBreak,
    Victory,
    Defeat,
    MenuMove,
    MenuSelect,
    Achievement,
    Star,
    Tick,
    TrailCorrect,
    TrailWrong,
    Whoosh,
    Count
};

enum class MusicTrack
{
    Menu,
    Neon,
    Dungeon,
    Lava,
    Ice,
    Candy,
    Trail,
    None
};

void initAudio();
void unloadAudio();
void updateAudio(float dt);
void playSfx(Sfx sfx, float pitch = 1.0f, float volume = 1.0f);
void playMusic(MusicTrack track);
MusicTrack musicForTheme(ThemeId theme);
void setVolumes(int master, int music, int sfx);
// Silences the game while another window is in front (fades out and back in).
void setBackgroundMute(bool muted);
bool isAudioAvailable();

// ===========================================================================
// UI widgets (widgets.cpp)
// ===========================================================================

struct UiState
{
    float scale = 1.0f; // 1.0 at 1280x720
    int width = 1280;
    int height = 720;
    int focus = 0;
    int widgetCount = 0;
    int previousWidgetCount = 0;
    bool navUp = false;
    bool navDown = false;
    bool navLeft = false;
    bool navRight = false;
    bool activate = false;
    bool back = false;
    bool mouseUsed = false;
    float time = 0.0f;
    Color accent = {255, 60, 200, 255};
    Color accent2 = {60, 220, 255, 255};
    Color panel = {12, 10, 30, 220};
    std::string* activeField = nullptr;
};

extern UiState ui;

enum class FontKind
{
    Display,
    Body,
    Mono
};

void loadFonts();
void unloadFonts();
Font getFont(FontKind kind);
float px(float value); // scale design pixels to screen pixels
Vector2 measure(FontKind kind, const char* text, float size);
void drawText(FontKind kind, const char* text, float x, float y, float size, Color color);
void drawTextCentered(FontKind kind, const char* text, float centerX, float y, float size, Color color);
void drawTextRight(FontKind kind, const char* text, float rightX, float y, float size, Color color);
void drawGlowText(FontKind kind, const char* text, float centerX, float y, float size, Color color, Color glow);
void drawRainbowTitle(const char* text, float centerX, float y, float size, float time);
void drawPanel(Rectangle rect, Color fill, Color border, float glow = 1.0f);
// Draws text wrapped to `width`; returns the height used.
float drawWrappedText(FontKind kind, const char* text, float x, float y, float width, float size, Color color);
void drawFpsCounter(); // the frame counter chip (Show FPS / F3)
void drawRoundedGradientV(Rectangle rect, float radius, Color top, Color bottom);
void drawGradientPanel(Rectangle rect, Color top, Color bottom, Color border);
void drawProgressBar(Rectangle rect, float fraction, Color fill, Color back);
void drawStar(Vector2 center, float radius, Color color, float rotation = 0.0f);
void drawHeart(Vector2 center, float size, Color color);
void drawItemIcon(Item item, Vector2 center, float size, float time);
void drawKeyIcon(Vector2 center, float size, Color color);
void drawMinotaurIcon(Vector2 center, float size, Color color);
void drawExitIcon(Vector2 center, float size, Color color, float time);
void drawTrophy(Vector2 center, float size, Color color);
void drawLock(Vector2 center, float size, Color color);
const char* itemName(Item item);
const char* itemDescription(Item item);
Color itemColor(Item item);

// Sets colours and scale for drawing only (no input handling).
void setUiStyle(Color accent, Color accent2, Color panel);
// Begins a frame of UI: reads keyboard/gamepad navigation.
void beginUi(Color accent, Color accent2, Color panel);
void endUi();
void resetFocus();
// Widgets return true when activated (click or Enter on the focused one).
bool button(const char* label, Rectangle rect, bool enabled = true, const char* subtitle = nullptr);
bool bigButton(const char* label, Rectangle rect, Color color, bool enabled = true);
bool slider(const char* label, Rectangle rect, int& value, int minValue, int maxValue, const char* suffix = "");
bool stepper(const char* label, Rectangle rect, int& value, int minValue, int maxValue,
             const char* const* names = nullptr);
bool toggle(const char* label, Rectangle rect, bool& value);
// Single-line text field with validation of every typed character.
bool textField(const char* label, Rectangle rect, std::string& text, std::size_t maxLength, bool digitsOnly,
               const char* placeholder);
bool isFocused(int widgetIndex);

// Toasts (achievement unlocked, saved, ...)
void showToast(const std::string& title, const std::string& text, Color color);
void updateAndDrawToasts(float dt);

// ===========================================================================
// App and screens
// ===========================================================================

enum class ScreenId
{
    Splash,
    Profiles,
    MainMenu,
    Campaign,
    Challenges,
    FreePlay,
    Game,
    TrailSetup,
    Trail,
    Achievements,
    Leaderboards,
    Settings,
    HowTo,
    Maps,   // list of hand-made maps
    Editor  // the map maker
};

// Everything needed to describe the run that is about to start.
struct RunRequest
{
    RunMode mode = RunMode::FreePlay;
    int index = 0; // level or challenge
    ThemeId theme = ThemeId::Neon;
    RunConfig config;
    bool testPlay = false; // the maker is testing app.pendingMap from the editor
};

struct App
{
    ScreenId screen = ScreenId::Splash;
    ScreenId nextScreen = ScreenId::Splash;
    float transition = 0.0f; // 0 = none, counts 1 -> 0 while fading
    bool switching = false;
    float time = 0.0f;
    bool quit = false;

    std::string saveDir;          // profiles, saved games, leaderboards
    std::string dataDir;          // this device's Labyrinth folder (saves/ and maps/ are inside)
    std::string lastSavedProfile; // what is on disk, for autosave
    bool tourMode = false;
    bool hasProfile = false;
    Profile profile;
    LeaderboardSet boards;

    RunRequest pendingRun;
    bool resumeRequested = false;
    ScreenId settingsReturn = ScreenId::MainMenu;

    // Free play settings (kept between games)
    RunConfig freePlayConfig;
    std::string freePlaySeed;
    int freePlayTheme = 0;
    int trailSize = 12;
    std::string trailSeed;
    int trailTheme = 0;
    int selectedLevel = 0;

    // Map maker
    CustomMap pendingMap; // the map being played
    CustomMap editorMap;  // the map open in the editor
    std::string editorFile; // file name of the map in the editor ("" = new)
};

extern App app;

void goTo(ScreenId screen);
void saveProfile();
void saveLeaderboards();
std::string runSavePath();
bool hasSavedRun();
void applySettings();
void announceAchievements();
void getToday(int& year, int& month, int& day);
std::string mapsDirectory();                // folder next to the game: maps/
bool saveCustomMap(CustomMap& map);         // writes maps/<name>.lmap
void launchCustomMap(const CustomMap& map, bool testPlay);

// Screens: each has enter / update+draw / leave.
void enterSplash();
void updateSplash(float dt);
void enterProfiles();
void updateProfiles(float dt);
void enterMainMenu();
void updateMainMenu(float dt);
void enterCampaign();
void updateCampaign(float dt);
void enterChallenges();
void updateChallenges(float dt);
void enterFreePlay();
void updateFreePlay(float dt);
void enterGame();
void updateGame(float dt);
void leaveGame();
void enterTrailSetup();
void updateTrailSetup(float dt);
void enterTrail();
void updateTrail(float dt);
void leaveTrail();
void enterAchievementsScreen();
void updateAchievementsScreen(float dt);
void enterLeaderboards();
void updateLeaderboards(float dt);
void enterSettings();
void updateSettings(float dt);
void enterHowTo();
void updateHowTo(float dt);
void enterMaps();
void updateMaps(float dt);
void enterEditor();
void updateEditor(float dt);
// A .lmap file was dropped onto the window: copy it into maps/.
void importDroppedMaps();

// Animated 3D maze behind the menus.
void initMenuBackground();
void unloadMenuBackground();
void drawMenuBackground(float dt);
void invalidateMenuBackground(); // the game replaced the 3D world
ThemeId menuTheme();

// Common menu header (title + back button). Returns true when Back is used.
bool drawScreenHeader(const char* title, const char* subtitle);

#endif // APP_H
