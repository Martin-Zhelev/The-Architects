// screen_game.cpp
// Playing a labyrinth: keyboard / gamepad input, the first-person and
// top-down cameras, smooth movement, reacting to game events (sounds,
// particles, flashes), the HUD, minimap, text view, full map, pause menu
// and the results screen with stars, score and records.

#include "app.h"

#include <cmath>
#include <cstdio>
#include <cstdlib>

namespace
{

// ---- Session ----
GameState state;
RunRequest run;

// ---- Player visuals ----
Vector3 visualPos;
Vector3 moveFrom;
Vector3 moveTo;
float moveT = 1.0f;
float moveDuration = 0.17f;
float walkPhase = 0.0f;
float yaw = 0.0f;
float yawFrom = 0.0f;
float yawTo = 0.0f;
float turnT = 1.0f;
float bumpCooldown = 0.0f;

// ---- View state ----
enum ViewMode
{
    VIEW_THIRD_PERSON = 0,
    VIEW_FIRST_PERSON = 1,
    VIEW_TOP_DOWN = 2
};
int viewMode = VIEW_THIRD_PERSON;
float teleportLock = 0.0f;      // no movement right after a teleport...
bool waitForKeyRelease = false; // ...and not until the movement keys are let go
void resetHeldAction(); // movement key repeat, see handleMovementInput()
float topZoom = 15.0f;
int textViewMode = 0; // 0 off, 1 side panel, 2 full screen
bool mapOpen = false;
bool paused = false;
bool overlayJustOpened = false; // the key that opened an overlay must not close it in the same frame
float introTimer = 0.0f;

// ---- Effects ----
float shake = 0.0f;
Color flashColor = {0, 0, 0, 0};
float flashAlpha = 0.0f;
std::string message;
Color messageColor = WHITE;
float messageTimer = 0.0f;
std::vector<OpeningDoor> openingDoors;
std::vector<Vector3> minotaurPos;
std::vector<float> minotaurYaw;
std::vector<float> minotaurWalk;
std::vector<std::vector<Vector3>> minotaurPath; // cell centres still to walk through, in order
float heartbeatTimer = 0.0f;
float danger = 0.0f;

// ---- Results ----
bool finished = false;
const float RESULTS_DELAY = 0.7f; // seconds of victory/defeat animation before the results panel
bool resultsFocusReset = false;
float endTimer = 0.0f;
bool recorded = false;
RunResult result;
int stars = 0;
bool newBest = false;
int shownScore = 0;
int starsShown = 0;

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------

float yawForDirection(Direction direction)
{
    switch (direction)
    {
    case Direction::Down:
        return 0.0f;
    case Direction::Right:
        return PI / 2.0f;
    case Direction::Up:
        return PI;
    case Direction::Left:
        return -PI / 2.0f;
    }
    return 0.0f;
}

Direction turnLeft(Direction d)
{
    switch (d)
    {
    case Direction::Up:
        return Direction::Left;
    case Direction::Left:
        return Direction::Down;
    case Direction::Down:
        return Direction::Right;
    case Direction::Right:
        return Direction::Up;
    }
    return d;
}

Direction turnRight(Direction d)
{
    return turnLeft(turnLeft(turnLeft(d)));
}

Direction opposite(Direction d)
{
    return turnLeft(turnLeft(d));
}

float smoothStep(float t)
{
    t = t < 0.0f ? 0.0f : (t > 1.0f ? 1.0f : t);
    return t * t * (3.0f - 2.0f * t);
}

const Theme& theme()
{
    return getThemeData(run.theme);
}

bool relativeControls()
{
    return viewMode != VIEW_TOP_DOWN;
}

bool reducedMotion()
{
    return app.profile.settings.reduceMotion;
}

void showMessage(const std::string& text, Color color, float seconds = 2.5f)
{
    message = text;
    messageColor = color;
    messageTimer = seconds;
}

void flash(Color color, float alpha)
{
    if (reducedMotion())
    {
        alpha *= 0.3f;
    }
    flashColor = color;
    flashAlpha = std::fmax(flashAlpha, alpha);
}

void addShake(float amount)
{
    if (!reducedMotion())
    {
        shake = std::fmax(shake, amount);
    }
}

std::string formatClock(int seconds)
{
    if (seconds < 0)
    {
        seconds = 0;
    }
    char text[16];
    std::snprintf(text, sizeof(text), "%d:%02d", seconds / 60, seconds % 60);
    return text;
}

float distanceToPlayer(int row, int col)
{
    return static_cast<float>(std::abs(row - state.playerRow) + std::abs(col - state.playerCol));
}

void snapVisuals()
{
    visualPos = cellCenter(state.playerRow, state.playerCol);
    moveFrom = moveTo = visualPos;
    moveT = 1.0f;
    yaw = yawFrom = yawTo = yawForDirection(state.facing);
    turnT = 1.0f;
    minotaurPos.clear();
    minotaurYaw.clear();
    minotaurWalk.clear();
    minotaurPath.clear();
    for (int i = 0; i < state.minotaurCount; ++i)
    {
        minotaurPos.push_back(cellCenter(state.minotaurs[i].row, state.minotaurs[i].col));
        minotaurYaw.push_back(0.0f);
        minotaurWalk.push_back(0.0f);
        minotaurPath.push_back(std::vector<Vector3>());
    }
}

std::string runTitle()
{
    switch (run.mode)
    {
    case RunMode::Campaign:
        return "LEVEL " + std::to_string(run.index + 1) + ": " + getLevel(run.index).name;
    case RunMode::Challenge:
        return getChallengeInfo(static_cast<ChallengeId>(run.index)).name;
    case RunMode::Custom:
        return std::string(run.testPlay ? "TEST PLAY: " : "MAP: ") + app.pendingMap.name;
    case RunMode::FreePlay:
        break;
    }
    return "FREE PLAY  " + std::to_string(state.maze.size) + "x" + std::to_string(state.maze.size) + "  seed " +
           std::to_string(state.config.seed);
}

// ---------------------------------------------------------------------------
// Starting, finishing, saving
// ---------------------------------------------------------------------------

void startSession()
{
    finished = false;
    recorded = false;
    resultsFocusReset = false;
    endTimer = 0.0f;
    paused = false;
    mapOpen = false;
    textViewMode = 0;
    openingDoors.clear();
    shake = 0.0f;
    flashAlpha = 0.0f;
    danger = 0.0f;
    introTimer = 3.0f;
    starsShown = 0;
    shownScore = 0;
    viewMode = app.profile.settings.startView;
    teleportLock = 0.0f;
    waitForKeyRelease = false;
    resetHeldAction();
    buildWorld(state, run.theme);
    invalidateMenuBackground();
    snapVisuals();
    clearParticles();
    playMusic(musicForTheme(run.theme));
    showMessage(state.config.minotaurChase ? "RUN! The Minotaurs are let loose behind you!" : "Find the exit!",
                state.config.minotaurChase ? Color{255, 90, 70, 255} : theme().accent2, 3.0f);
}

void recordResults()
{
    if (recorded)
    {
        return;
    }
    recorded = true;
    result = summarizeRun(state);
    newBest = false;
    stars = 0;
    if (run.mode == RunMode::Custom)
    {
        if (run.testPlay && result.won)
        {
            // Beating your own map is what makes it publishable.
            markMapVerified(app.pendingMap, result.moves, result.seconds);
            app.editorMap = app.pendingMap;
            if (saveCustomMap(app.pendingMap))
            {
                showToast("MAP VERIFIED", "You beat it - it can now be published and shared", Color{90, 255, 150, 255});
            }
        }
        else if (!run.testPlay)
        {
            recordRun(app.profile.stats, result, run.mode, run.index, run.theme);
            saveProfile();
            announceAchievements();
        }
        return;
    }
    recordRun(app.profile.stats, result, run.mode, run.index, run.theme);

    if (result.won)
    {
        switch (run.mode)
        {
        case RunMode::Campaign:
            stars = computeStars(result);
            if (stars > app.profile.levelStars[run.index])
            {
                app.profile.levelStars[run.index] = stars;
            }
            if (result.score > app.profile.levelBestScore[run.index])
            {
                app.profile.levelBestScore[run.index] = result.score;
                newBest = true;
            }
            break;
        case RunMode::Challenge:
        {
            if (result.score > app.profile.challengeBest[run.index])
            {
                app.profile.challengeBest[run.index] = result.score;
                newBest = true;
            }
            if (static_cast<ChallengeId>(run.index) == ChallengeId::Daily)
            {
                int year = 0;
                int month = 0;
                int day = 0;
                getToday(year, month, day);
                const int today = makeDateNumber(year, month, day);
                if (app.profile.dailyBestDate != today || result.score > app.profile.dailyBestScore)
                {
                    app.profile.dailyBestDate = today;
                    app.profile.dailyBestScore = result.score;
                }
            }
            break;
        }
        case RunMode::FreePlay:
        case RunMode::Custom:
            break;
        }

        if (run.mode != RunMode::Campaign)
        {
            const BoardId board = run.mode == RunMode::Challenge ? boardForChallenge(static_cast<ChallengeId>(run.index))
                                                                 : BoardId::FreePlay;
            ScoreEntry entry;
            entry.name = app.profile.name;
            entry.mazeSize = result.size;
            entry.moves = result.moves;
            entry.seconds = result.seconds;
            entry.score = result.score;
            if (addToLeaderboard(app.boards.boards[static_cast<int>(board)], entry))
            {
                saveLeaderboards();
            }
        }
    }

    std::remove(runSavePath().c_str()); // a finished game cannot be continued
    saveProfile();
    announceAchievements();
}

void saveAndQuit()
{
    SaveMeta meta;
    meta.mode = static_cast<int>(run.mode);
    meta.index = run.index;
    meta.theme = static_cast<int>(run.theme);
    state.facing = state.facing; // the facing direction is part of the save
    if (writeTextFile(runSavePath(), serializeRun(state, meta)))
    {
        showToast("GAME SAVED", "Choose CONTINUE in the menu to carry on", theme().accent2);
    }
    goTo(ScreenId::MainMenu);
}

void restartRun()
{
    app.pendingRun = run;
    app.resumeRequested = false;
    goTo(ScreenId::Game);
}

// ---------------------------------------------------------------------------
// Events from the game logic
// ---------------------------------------------------------------------------

void handleEvents()
{
    const Theme& t = theme();
    for (int i = 0; i < state.eventCount; ++i)
    {
        const GameEvent& e = state.events[i];
        const Vector3 at = Vector3Add(cellCenter(e.row, e.col), Vector3{0, 0.5f, 0});
        switch (e.type)
        {
        case EventType::Step:
            playSfx(Sfx::Step, 0.9f + 0.2f * static_cast<float>(std::rand() % 100) / 100.0f, 0.45f);
            break;
        case EventType::Bump:
            playSfx(Sfx::Bump, 1.0f, 0.7f);
            addShake(0.12f);
            break;
        case EventType::KeyPickup:
            playSfx(Sfx::Key);
            spawnBurst(at, t.key, 40, 3.0f);
            showMessage("KEY FOUND! Keys open the glowing doors.", t.key);
            break;
        case EventType::DoorOpened:
        {
            playSfx(Sfx::Door);
            int rowOffset = 0;
            int colOffset = 0;
            getDirectionOffset(static_cast<Direction>(e.value), rowOffset, colOffset);
            openingDoors.push_back(OpeningDoor{cellToTile(e.row) + rowOffset, cellToTile(e.col) + colOffset, 0.0f});
            spawnBurst(at, t.door, 60, 4.0f);
            showMessage("DOOR UNLOCKED - shortcut open!", t.door);
            break;
        }
        case EventType::GemPickup:
            playSfx(Sfx::Gem, 1.0f + 0.03f * static_cast<float>(e.value % 20));
            spawnBurst(at, t.gem, 35, 2.5f);
            showMessage("GEM " + std::to_string(state.gemsCollected) + " / " + std::to_string(state.gemsTotal) +
                            "  (+50)",
                        t.gem, 1.6f);
            break;
        case EventType::PowerUp:
        {
            const Item item = static_cast<Item>(e.value);
            playSfx(Sfx::PowerUp);
            spawnRing(at, itemColor(item), 40, 1.0f);
            spawnBurst(at, itemColor(item), 30, 3.0f);
            showMessage(std::string(itemName(item)) + "!  " + itemDescription(item), itemColor(item), 3.2f);
            if (item == Item::Torch)
            {
                revealAround(state);
            }
            break;
        }
        case EventType::Teleport:
        {
            playSfx(Sfx::Teleport);
            const int fromCell = e.value;
            const Color pad = teleporterColor(state, state.playerRow, state.playerCol);
            spawnRing(Vector3Add(cellCenter(fromCell / state.maze.size, fromCell % state.maze.size), Vector3{0, 0.3f, 0}),
                      pad, 50, 1.2f);
            spawnRing(at, pad, 50, 1.2f);
            flash(pad, 0.6f);
            // You land ON the twin pad. Turn to face the way out, and wait for
            // the movement keys to be released so you don't walk straight off.
            visualPos = moveFrom = moveTo = cellCenter(state.playerRow, state.playerCol);
            moveT = 1.0f;
            for (const Direction d : {Direction::Up, Direction::Down, Direction::Left, Direction::Right})
            {
                if (isPassable(state.maze, state.playerRow, state.playerCol, d, true))
                {
                    state.facing = d;
                    yaw = yawFrom = yawTo = yawForDirection(d);
                    turnT = 1.0f;
                    break;
                }
            }
            teleportLock = 0.35f;
            waitForKeyRelease = true;
            showMessage("WHOOSH! Teleported - you are on the twin pad.", pad, 1.8f);
            break;
        }
        case EventType::Hurt:
            playSfx(Sfx::Hurt);
            flash(Color{255, 0, 30, 255}, 0.55f);
            addShake(0.35f);
            spawnBurst(at, Color{255, 40, 40, 255}, 30, 3.0f);
            showMessage("OUCH! Spikes!  -1 life", Color{255, 80, 80, 255});
            break;
        case EventType::Caught:
            playSfx(Sfx::Roar, 1.0f, 1.0f);
            playSfx(Sfx::Hurt);
            flash(Color{255, 0, 0, 255}, 0.75f);
            addShake(0.6f);
            showMessage("THE MINOTAUR CAUGHT YOU!  -1 life", Color{255, 60, 60, 255}, 3.0f);
            snapVisuals(); // the Minotaur jumped back to its lair
            visualPos = moveFrom = moveTo = cellCenter(state.playerRow, state.playerCol);
            break;
        case EventType::MinotaurStep:
        {
            const float distance = distanceToPlayer(e.row, e.col);
            if (distance < 7.0f)
            {
                playSfx(Sfx::Step, 0.5f, 0.9f - distance * 0.12f);
            }
            break;
        }
        case EventType::MinotaurReleased:
            playSfx(Sfx::Roar, 0.85f, 1.0f);
            addShake(0.3f);
            showMessage(e.value == 0 ? "THE FIRST MINOTAUR IS LOOSE - RUN!" : "ANOTHER MINOTAUR IS LET LOOSE!",
                        Color{255, 70, 50, 255}, 2.2f);
            if (static_cast<std::size_t>(e.value) < minotaurPos.size())
            {
                minotaurPos[static_cast<std::size_t>(e.value)] = cellCenter(e.row, e.col);
                minotaurPath[static_cast<std::size_t>(e.value)].clear();
            }
            break;
        case EventType::MinotaurAlert:
        {
            if (state.config.minotaurChase)
            {
                break; // in the gauntlet they always hunt
            }
            const float distance = distanceToPlayer(e.row, e.col);
            playSfx(Sfx::Roar, 0.95f, std::fmax(0.25f, 1.0f - distance / 14.0f));
            showMessage("A MINOTAUR HAS YOUR SCENT!", Color{255, 70, 50, 255}, 2.0f);
            break;
        }
        case EventType::WallBroken:
        {
            playSfx(Sfx::WallBreak);
            addShake(0.45f);
            int rowOffset = 0;
            int colOffset = 0;
            getDirectionOffset(static_cast<Direction>(e.value), rowOffset, colOffset);
            const Vector3 wallAt{static_cast<float>(cellToTile(e.col) + colOffset) + 0.5f, 0.7f,
                                 static_cast<float>(cellToTile(e.row) + rowOffset) + 0.5f};
            spawnBurst(wallAt, t.wallTop, 80, 5.0f, 0.16f);
            buildWorld(state, run.theme);
            showMessage("SMASH! -" + std::to_string(getHammerCost()) + " points.  Hammer charges left: " +
                            std::to_string(state.hammerCharges),
                        WHITE);
            break;
        }
        case EventType::ExitLocked:
            playSfx(Sfx::Bump);
            showMessage("THE EXIT IS SEALED - " + std::to_string(e.value) + " gem(s) still missing!",
                        Color{255, 90, 90, 255}, 3.0f);
            break;
        case EventType::TimeWarning:
            playSfx(Sfx::Tick, 1.0f, 1.0f);
            if (e.value == 30)
            {
                showMessage("30 SECONDS LEFT!", Color{255, 120, 60, 255});
            }
            break;
        case EventType::Won:
            playSfx(Sfx::Victory);
            flash(WHITE, 0.8f);
            spawnConfetti3D(at, 250);
            spawnConfetti2D(ui.width, 250);
            finished = true;
            break;
        case EventType::Lost:
            playSfx(Sfx::Defeat);
            flash(Color{120, 0, 0, 255}, 0.7f);
            finished = true;
            break;
        }
    }
    clearEvents(state);
}

// ---------------------------------------------------------------------------
// Input and movement
// ---------------------------------------------------------------------------

bool keyDown(int a, int b)
{
    return IsKeyDown(a) || IsKeyDown(b);
}

bool padDown(int button)
{
    return IsGamepadAvailable(0) && IsGamepadButtonDown(0, button);
}

bool padPressed(int button)
{
    return IsGamepadAvailable(0) && IsGamepadButtonPressed(0, button);
}

float padAxis(int axis)
{
    return IsGamepadAvailable(0) ? GetGamepadAxisMovement(0, axis) : 0.0f;
}

void attemptMove(Direction direction)
{
    if (bumpCooldown > 0.0f)
    {
        return;
    }
    const int beforeRow = state.playerRow;
    const int beforeCol = state.playerCol;
    const MoveResult outcome = movePlayer(state, direction);
    if (isMoveSuccessful(outcome) && outcome != MoveResult::Teleported &&
        (state.playerRow != beforeRow || state.playerCol != beforeCol))
    {
        moveFrom = visualPos;
        moveTo = cellCenter(state.playerRow, state.playerCol);
        moveT = 0.0f;
        moveDuration = state.bootsTime > 0.0f ? 0.09f : 0.17f;
    }
    else if (!isMoveSuccessful(outcome))
    {
        bumpCooldown = 0.25f;
    }
}

void startTurn(Direction newFacing)
{
    state.facing = newFacing;
    yawFrom = yaw;
    float target = yawForDirection(newFacing);
    while (target - yawFrom > PI)
    {
        target -= 2.0f * PI;
    }
    while (target - yawFrom < -PI)
    {
        target += 2.0f * PI;
    }
    yawTo = target;
    turnT = 0.0f;
}

bool anyMovementKeyDown()
{
    const int keys[] = {KEY_UP, KEY_DOWN, KEY_LEFT, KEY_RIGHT, KEY_W, KEY_A, KEY_S, KEY_D, KEY_Q, KEY_E};
    for (const int key : keys)
    {
        if (IsKeyDown(key))
        {
            return true;
        }
    }
    return std::fabs(padAxis(GAMEPAD_AXIS_LEFT_X)) > 0.5f || std::fabs(padAxis(GAMEPAD_AXIS_LEFT_Y)) > 0.5f ||
           padDown(GAMEPAD_BUTTON_LEFT_FACE_UP) || padDown(GAMEPAD_BUTTON_LEFT_FACE_DOWN) ||
           padDown(GAMEPAD_BUTTON_LEFT_FACE_LEFT) || padDown(GAMEPAD_BUTTON_LEFT_FACE_RIGHT);
}

// What the movement keys ask for this frame. One action at a time, in this
// order of priority (turning wins over walking, like before).
enum class MoveAction
{
    None,
    TurnLeft,
    TurnRight,
    Forward,
    Back,
    StrafeLeft,
    StrafeRight,
    Up, // top-down / text view: absolute directions
    Down,
    Left,
    Right
};

// Like keyboard repeat: a tap is exactly ONE turn or ONE step, however firmly
// or slowly the key is pressed. Only if the key is still held after a short
// pause does the action repeat (walking then continues smoothly; turning
// repeats at a calm pace). Before this, a key held a little longer than the
// 0.15 s turn animation turned twice - a 180 degree spin.
MoveAction heldAction = MoveAction::None;
float heldTime = 0.0f;       // how long the current action's key has been held
int heldCount = 0;           // how many times it has been done during this hold
float lastActionTime = 0.0f; // heldTime when it was last done
const float MOVE_REPEAT_DELAY = 0.28f; // hold this long before walking on
const float TURN_REPEAT_DELAY = 0.55f; // hold this long before a second turn
const float TURN_REPEAT_GAP = 0.40f;   // then one turn every this many seconds

void resetHeldAction()
{
    heldAction = MoveAction::None;
    heldTime = 0.0f;
    heldCount = 0;
    lastActionTime = 0.0f;
}

MoveAction readMoveAction()
{
    const float stickX = padAxis(GAMEPAD_AXIS_LEFT_X);
    const float stickY = padAxis(GAMEPAD_AXIS_LEFT_Y);
    const bool up = keyDown(KEY_UP, KEY_W) || padDown(GAMEPAD_BUTTON_LEFT_FACE_UP) || stickY < -0.5f;
    const bool down = keyDown(KEY_DOWN, KEY_S) || padDown(GAMEPAD_BUTTON_LEFT_FACE_DOWN) || stickY > 0.5f;
    const bool left = keyDown(KEY_LEFT, KEY_A) || padDown(GAMEPAD_BUTTON_LEFT_FACE_LEFT) || stickX < -0.5f;
    const bool right = keyDown(KEY_RIGHT, KEY_D) || padDown(GAMEPAD_BUTTON_LEFT_FACE_RIGHT) || stickX > 0.5f;
    const bool strafeLeft = IsKeyDown(KEY_Q) || padDown(GAMEPAD_BUTTON_LEFT_TRIGGER_1);
    const bool strafeRight = IsKeyDown(KEY_E) || padDown(GAMEPAD_BUTTON_RIGHT_TRIGGER_1);

    if (relativeControls() && textViewMode != 2)
    {
        return left          ? MoveAction::TurnLeft
               : right       ? MoveAction::TurnRight
               : up          ? MoveAction::Forward
               : down        ? MoveAction::Back
               : strafeLeft  ? MoveAction::StrafeLeft
               : strafeRight ? MoveAction::StrafeRight
                             : MoveAction::None;
    }
    return up ? MoveAction::Up : down ? MoveAction::Down : left ? MoveAction::Left : right ? MoveAction::Right : MoveAction::None;
}

void handleMovementInput()
{
    if (waitForKeyRelease)
    {
        if (anyMovementKeyDown())
        {
            return;
        }
        waitForKeyRelease = false;
        resetHeldAction();
    }

    const MoveAction action = readMoveAction();
    if (action != heldAction)
    {
        // A new key (or a different one): it counts as a fresh press.
        heldAction = action;
        heldTime = 0.0f;
        heldCount = 0;
        lastActionTime = 0.0f;
    }
    else
    {
        heldTime += GetFrameTime();
    }
    if (action == MoveAction::None)
    {
        return;
    }

    // Is this action due? The first one always is (and waits for the current
    // animation to finish, so a quick press is never lost).
    const bool turning = action == MoveAction::TurnLeft || action == MoveAction::TurnRight;
    bool due = heldCount == 0;
    if (!due && turning)
    {
        due = heldTime >= TURN_REPEAT_DELAY && heldTime - lastActionTime >= TURN_REPEAT_GAP;
    }
    else if (!due)
    {
        due = heldTime >= MOVE_REPEAT_DELAY; // then walk on as fast as the animation allows
    }
    if (!due || teleportLock > 0.0f || moveT < 0.85f || turnT < 1.0f)
    {
        return;
    }
    ++heldCount;
    lastActionTime = heldTime;

    switch (action)
    {
    case MoveAction::TurnLeft:
        startTurn(turnLeft(state.facing));
        return;
    case MoveAction::TurnRight:
        startTurn(turnRight(state.facing));
        return;
    case MoveAction::Forward:
        attemptMove(state.facing);
        return;
    case MoveAction::Back:
        attemptMove(opposite(state.facing));
        return;
    case MoveAction::StrafeLeft:
        attemptMove(turnLeft(state.facing));
        return;
    case MoveAction::StrafeRight:
        attemptMove(turnRight(state.facing));
        return;
    default:
        break;
    }

    // Top-down and text view: arrows move in the direction they show.
    const Direction direction = action == MoveAction::Up     ? Direction::Up
                                : action == MoveAction::Down ? Direction::Down
                                : action == MoveAction::Left ? Direction::Left
                                                             : Direction::Right;
    state.facing = direction;
    yaw = yawFrom = yawTo = yawForDirection(direction);
    attemptMove(direction);
}

void handleActionKeys()
{
    if (IsKeyPressed(KEY_V) || IsKeyPressed(KEY_TAB) || padPressed(GAMEPAD_BUTTON_RIGHT_FACE_UP))
    {
        // Third person -> first person -> top-down -> third person ...
        viewMode = (viewMode + 1) % 3;
        playSfx(Sfx::Whoosh, 1.3f, 0.5f);
        yaw = yawFrom = yawTo = yawForDirection(state.facing);
        turnT = 1.0f;
        const char* const NAMES[3] = {"THIRD PERSON VIEW", "FIRST PERSON VIEW", "TOP-DOWN VIEW"};
        showMessage(NAMES[viewMode], theme().accent2, 1.2f);
    }
    if (IsKeyPressed(KEY_F9) && app.profile.admin)
    {
        // Admin tool: walk the rest of the way to the exit (for testing).
        state.invulnerableTime = 1000.0f;
        for (int attempt = 0; attempt < 8 && !state.won && !state.lost; ++attempt)
        {
            if (state.config.requireAllGems)
            {
                state.gemsCollected = state.gemsTotal;
            }
            Path route;
            if (!findShortestPath(state.maze, state.playerRow, state.playerCol, false, route))
            {
                break;
            }
            for (int i = 1; i < route.length && !state.won; ++i)
            {
                const int dr = route.rows[i] - state.playerRow;
                const int dc = route.cols[i] - state.playerCol;
                if (std::abs(dr) + std::abs(dc) != 1)
                {
                    break;
                }
                movePlayer(state, dr > 0 ? Direction::Down : dr < 0 ? Direction::Up : dc > 0 ? Direction::Right : Direction::Left);
            }
            destroyPath(route);
        }
        state.invulnerableTime = 0.0f;
        snapVisuals();
        showMessage("ADMIN: warped to the exit", Color{255, 215, 60, 255}, 1.5f);
    }
    if (IsKeyPressed(KEY_T))
    {
        textViewMode = (textViewMode + 1) % 3;
        playSfx(Sfx::MenuSelect);
    }
    if (IsKeyPressed(KEY_M) || padPressed(GAMEPAD_BUTTON_MIDDLE_LEFT))
    {
        mapOpen = true;
        overlayJustOpened = true;
        playSfx(Sfx::MenuSelect);
    }
    if (IsKeyPressed(KEY_H) || padPressed(GAMEPAD_BUTTON_RIGHT_FACE_LEFT))
    {
        if (toggleHint(state))
        {
            showMessage(state.hintVisible ? "HINT ON - follow the glowing orbs (-100 points)" : "Hint hidden.",
                        theme().hint);
            playSfx(Sfx::PowerUp, 1.5f, 0.6f);
        }
        else
        {
            showMessage("No hints in this challenge!", Color{255, 90, 90, 255});
            playSfx(Sfx::Bump);
        }
    }
    if (IsKeyPressed(KEY_SPACE) || padPressed(GAMEPAD_BUTTON_RIGHT_FACE_DOWN))
    {
        if (state.hammerCharges <= 0)
        {
            showMessage("No hammer charges. Find a hammer!", Color{200, 200, 220, 255}, 1.5f);
        }
        else if (!useHammer(state, state.facing))
        {
            showMessage("Nothing to smash there (the outer wall is too strong).", Color{200, 200, 220, 255}, 1.8f);
            playSfx(Sfx::Bump);
        }
    }
    const float wheel = GetMouseWheelMove();
    if (wheel != 0.0f)
    {
        topZoom = std::fmax(6.0f, std::fmin(45.0f, topZoom - wheel * 1.5f));
    }
}

// ---------------------------------------------------------------------------
// Updating visuals
// ---------------------------------------------------------------------------

void updateVisuals(float dt)
{
    if (moveT < 1.0f)
    {
        moveT = std::fmin(1.0f, moveT + dt / moveDuration);
        visualPos = Vector3Lerp(moveFrom, moveTo, smoothStep(moveT));
        walkPhase += dt * (state.bootsTime > 0.0f ? 18.0f : 12.0f);
    }
    if (turnT < 1.0f)
    {
        turnT = std::fmin(1.0f, turnT + dt / 0.15f);
        yaw = yawFrom + (yawTo - yawFrom) * smoothStep(turnT);
    }
    bumpCooldown = std::fmax(0.0f, bumpCooldown - dt);
    teleportLock = std::fmax(0.0f, teleportLock - dt);

    for (OpeningDoor& door : openingDoors)
    {
        door.progress = std::fmin(1.0f, door.progress + dt * 1.5f);
    }
    while (!openingDoors.empty() && openingDoors.front().progress >= 1.0f)
    {
        openingDoors.erase(openingDoors.begin());
    }

    // Minotaurs walk from cell centre to cell centre, along the corridor and
    // never diagonally across a corner, so they can't clip through walls.
    for (int i = 0; i < state.minotaurCount && i < static_cast<int>(minotaurPos.size()); ++i)
    {
        const std::size_t index = static_cast<std::size_t>(i);
        std::vector<Vector3>& path = minotaurPath[index];
        const Vector3 cell = cellCenter(state.minotaurs[i].row, state.minotaurs[i].col);
        const Vector3 last = path.empty() ? minotaurPos[index] : path.back();
        if (Vector3Distance(last, cell) > 0.01f)
        {
            if (Vector3Distance(last, cell) > 2.5f || path.size() > 6)
            {
                // Not a neighbour (back to the lair) or far behind: jump there.
                path.clear();
                minotaurPos[index] = cell;
            }
            else
            {
                path.push_back(cell);
            }
        }
        if (path.empty())
        {
            continue;
        }
        const Vector3 target = path.front();
        const Vector3 delta = Vector3Subtract(target, minotaurPos[index]);
        const float distance = Vector3Length(delta);
        // A little faster when it is behind, so the queue stays short.
        const float speed = 2.0f / (getMinotaurStepSeconds(state) * 0.85f) * (path.size() > 1 ? 1.6f : 1.0f);
        const float step = std::fmin(distance, speed * dt);
        if (distance <= step + 0.001f)
        {
            minotaurPos[index] = target;
            path.erase(path.begin());
        }
        else
        {
            minotaurPos[index] = Vector3Add(minotaurPos[index], Vector3Scale(delta, step / distance));
        }
        if (distance > 0.01f)
        {
            const float wanted = std::atan2(delta.x, delta.z);
            float& y = minotaurYaw[index];
            float diff = wanted - y;
            while (diff > PI)
            {
                diff -= 2.0f * PI;
            }
            while (diff < -PI)
            {
                diff += 2.0f * PI;
            }
            y += diff * std::fmin(1.0f, dt * 14.0f);
            minotaurWalk[index] += dt;
        }
    }

    // Danger level from the nearest hunting Minotaur.
    float wantedDanger = 0.0f;
    for (int i = 0; i < state.minotaurCount; ++i)
    {
        const float d = distanceToPlayer(state.minotaurs[i].row, state.minotaurs[i].col);
        if (state.minotaurs[i].released && state.minotaurs[i].hunting && d < 6.0f && state.freezeTime <= 0.0f)
        {
            wantedDanger = std::fmax(wantedDanger, (6.0f - d) / 6.0f);
        }
    }
    danger += (wantedDanger - danger) * std::fmin(1.0f, dt * 4.0f);
    if (danger > 0.15f && !finished)
    {
        heartbeatTimer -= dt;
        if (heartbeatTimer <= 0.0f)
        {
            playSfx(Sfx::Heartbeat, 1.0f, 0.4f + danger * 0.6f);
            heartbeatTimer = 1.0f - danger * 0.55f;
        }
    }

    shake = std::fmax(0.0f, shake - dt * 1.8f);
    flashAlpha = std::fmax(0.0f, flashAlpha - dt * 1.6f);
    messageTimer = std::fmax(0.0f, messageTimer - dt);
    introTimer = std::fmax(0.0f, introTimer - dt);

    updateParticles(dt);
    if (!reducedMotion())
    {
        spawnAmbient(theme().ambientStyle, visualPos, dt, viewMode == VIEW_FIRST_PERSON ? 7.0f : 14.0f);
    }
}

Camera3D makeCamera()
{
    Camera3D camera{};
    camera.up = Vector3{0, 1, 0};
    camera.projection = CAMERA_PERSPECTIVE;
    const float shakeX = shake > 0.0f ? (static_cast<float>(std::rand() % 100) / 100.0f - 0.5f) * shake * 0.3f : 0.0f;
    const float shakeY = shake > 0.0f ? (static_cast<float>(std::rand() % 100) / 100.0f - 0.5f) * shake * 0.3f : 0.0f;
    if (viewMode == VIEW_THIRD_PERSON)
    {
        // Chase camera: behind and above the player, looking past them down
        // the corridor. High enough to see over the walls a little.
        const Vector3 forward{std::sin(yaw), 0.0f, std::cos(yaw)};
        const float back = 3.4f;
        const float height = 3.3f;
        camera.position = Vector3{visualPos.x - forward.x * back + shakeX, height + shakeY, visualPos.z - forward.z * back};
        camera.target = Vector3{visualPos.x + forward.x * 2.2f, 0.35f, visualPos.z + forward.z * 2.2f};
        camera.fovy = static_cast<float>(app.profile.settings.fieldOfView) * 0.9f;
    }
    else if (viewMode == VIEW_FIRST_PERSON)
    {
        const float bob = (moveT < 1.0f && !reducedMotion()) ? std::sin(walkPhase) * 0.035f : 0.0f;
        const Vector3 forward{std::sin(yaw), 0.0f, std::cos(yaw)};
        camera.position = Vector3{visualPos.x - forward.x * 0.35f + shakeX, EYE_HEIGHT + bob + shakeY,
                                  visualPos.z - forward.z * 0.35f};
        camera.target = Vector3Add(camera.position, Vector3{forward.x, -0.04f, forward.z});
        camera.fovy = static_cast<float>(app.profile.settings.fieldOfView);
    }
    else
    {
        camera.position = Vector3{visualPos.x + shakeX, topZoom, visualPos.z + topZoom * 0.45f + shakeY};
        camera.target = visualPos;
        camera.fovy = 55.0f;
    }
    return camera;
}

// ---------------------------------------------------------------------------
// HUD pieces
// ---------------------------------------------------------------------------

bool tileDiscovered(int tileRow, int tileCol)
{
    const int last = state.maze.size - 1;
    const int rows[2] = {std::min(last, std::max(0, (tileRow - 1) / 2)), std::min(last, std::max(0, tileRow / 2))};
    const int cols[2] = {std::min(last, std::max(0, (tileCol - 1) / 2)), std::min(last, std::max(0, tileCol / 2))};
    for (const int r : rows)
    {
        for (const int c : cols)
        {
            if (state.discovered[r][c])
            {
                return true;
            }
        }
    }
    return false;
}

// Draws the maze from above into `area`, showing `window` tiles around the player.
void drawMapArea(Rectangle area, int window, bool showItems)
{
    const Theme& t = theme();
    const int total = state.maze.tileCount;
    const int visible = std::min(total, window);
    const int firstRow = computeViewportStart(cellToTile(state.playerRow), total, window);
    const int firstCol = computeViewportStart(cellToTile(state.playerCol), total, window);
    const float cell = std::fmin(area.width, area.height) / static_cast<float>(visible);
    const float ox = area.x + (area.width - cell * visible) * 0.5f;
    const float oy = area.y + (area.height - cell * visible) * 0.5f;

    for (int r = 0; r < visible; ++r)
    {
        for (int c = 0; c < visible; ++c)
        {
            const int tr = firstRow + r;
            const int tc = firstCol + c;
            const Rectangle rect{ox + static_cast<float>(c) * cell, oy + static_cast<float>(r) * cell, cell + 0.5f,
                                 cell + 0.5f};
            if (!tileDiscovered(tr, tc))
            {
                DrawRectangleRec(rect, Color{10, 10, 18, 230});
                continue;
            }
            const Tile tile = state.maze.tiles[tr][tc];
            Color color = Color{20, 18, 34, 220};
            if (tile == Tile::Wall)
            {
                color = lerpColor(t.wallTop, Color{40, 40, 60, 255}, 0.3f);
            }
            else if (tile == Tile::Door)
            {
                color = t.door;
            }
            else if (state.hintVisible && state.hintTiles[tr][tc])
            {
                color = withAlpha(t.hint, 0.7f);
            }
            else if (tr % 2 == 1 && tc % 2 == 1 && state.visitedCells[tr / 2][tc / 2])
            {
                color = withAlpha(t.accent, 0.3f);
            }
            DrawRectangleRec(rect, color);
            if (tr % 2 == 0 || tc % 2 == 0)
            {
                continue;
            }
            if (!showItems)
            {
                // The small minimap still shows hazards and pads.
                const Item item = state.items[tr / 2][tc / 2];
                const Vector2 middle{rect.x + cell * 0.5f, rect.y + cell * 0.5f};
                if (item == Item::Spikes)
                {
                    DrawCircleV(middle, cell * 0.45f, areSpikesUp(state, tr / 2, tc / 2) ? Color{255, 50, 40, 255}
                                                                                         : Color{150, 40, 40, 255});
                }
                else if (item == Item::Teleporter)
                {
                    DrawCircleV(middle, cell * 0.5f, teleporterColor(state, tr / 2, tc / 2));
                }
                continue;
            }
            const int row = tr / 2;
            const int col = tc / 2;
            const Vector2 center{rect.x + cell * 0.5f, rect.y + cell * 0.5f};
            if (tile == Tile::Key)
            {
                DrawCircleV(center, cell * 0.45f, t.key);
            }
            const Item item = state.items[row][col];
            if (item == Item::Teleporter)
            {
                const Color pad = teleporterColor(state, row, col);
                DrawCircleV(center, cell * 0.55f, pad);
                DrawCircleV(center, cell * 0.25f, Color{20, 10, 30, 255});
                if (cell >= 12.0f)
                {
                    const std::string number = std::to_string(teleporterPairIndex(state, row, col) + 1);
                    drawTextCentered(FontKind::Display, number.c_str(), center.x, center.y - cell * 0.42f, cell * 0.8f, WHITE);
                }
            }
            else if (item == Item::Spikes)
            {
                const bool up = areSpikesUp(state, row, col);
                DrawRectangleRec(Rectangle{center.x - cell * 0.4f, center.y - cell * 0.4f, cell * 0.8f, cell * 0.8f},
                                 up ? Color{255, 50, 40, 255} : Color{150, 40, 40, 255});
            }
            else if (item != Item::None)
            {
                const Color ic = item == Item::Gem ? t.gem : itemColor(item);
                if (cell >= 10.0f)
                {
                    drawItemIcon(item, center, cell * 1.1f, app.time);
                }
                else
                {
                    DrawCircleV(center, cell * 0.4f, ic);
                }
            }
        }
    }

    auto toScreen = [&](int row, int col) {
        return Vector2{ox + (static_cast<float>(cellToTile(col) - firstCol) + 0.5f) * cell,
                       oy + (static_cast<float>(cellToTile(row) - firstRow) + 0.5f) * cell};
    };
    auto inside = [&](int row, int col) {
        const int tr = cellToTile(row) - firstRow;
        const int tc = cellToTile(col) - firstCol;
        return tr >= 0 && tc >= 0 && tr < visible && tc < visible;
    };

    const int last = state.maze.size - 1;
    if (inside(last, last))
    {
        const Vector2 exit = toScreen(last, last);
        DrawCircleV(exit, cell * (1.0f + 0.3f * std::sin(app.time * 5.0f)), withAlpha(t.exit, 0.5f));
        DrawRectangleRec(Rectangle{exit.x - cell * 0.5f, exit.y - cell * 0.5f, cell, cell}, t.exit);
    }
    for (int i = 0; i < state.minotaurCount; ++i)
    {
        const Minotaur& m = state.minotaurs[i];
        if (m.released && inside(m.row, m.col) && state.discovered[m.row][m.col])
        {
            DrawCircleV(toScreen(m.row, m.col), cell * 0.9f, m.hunting ? Color{255, 40, 30, 255} : Color{200, 110, 60, 255});
        }
    }
    if (inside(state.playerRow, state.playerCol))
    {
        // The map is drawn with rows going down the screen (world +z) and
        // columns to the right (world +x). The player faces (sin yaw, cos yaw)
        // in (x, z), so on the map the tip points along (sin yaw, cos yaw).
        // The arrow follows the smooth turn and the dot follows the walk.
        const Vector2 p{ox + (visualPos.x - static_cast<float>(firstCol)) * cell,
                        oy + (visualPos.z - static_cast<float>(firstRow)) * cell};
        const float a = yaw;
        const Vector2 tip{p.x + std::sin(a) * cell * 1.5f, p.y + std::cos(a) * cell * 1.5f};
        const Vector2 l{p.x + std::sin(a + 2.5f) * cell * 1.0f, p.y + std::cos(a + 2.5f) * cell * 1.0f};
        const Vector2 r{p.x + std::sin(a - 2.5f) * cell * 1.0f, p.y + std::cos(a - 2.5f) * cell * 1.0f};
        DrawCircleV(p, cell * 1.2f, withAlpha(t.player, 0.35f));
        DrawTriangle(tip, r, l, t.player);
        DrawTriangle(tip, l, r, t.player);
    }
}

void drawMinimap()
{
    const float size = px(210);
    const Rectangle area{static_cast<float>(ui.width) - size - px(20), px(20), size, size};
    drawPanel(Rectangle{area.x - px(6), area.y - px(6), area.width + px(12), area.height + px(12)},
              Color{0, 0, 0, 200}, theme().accent);
    drawMapArea(area, 31, false);
}

// The classic text view: the maze drawn with characters, top-down - two
// characters per tile, exactly like the terminal edition of the game.
void drawTextView(Rectangle area, bool fullscreen)
{
    const Theme& t = theme();
    DrawRectangleRec(area, fullscreen ? Color{2, 10, 6, 255} : Color{2, 6, 10, 235});
    if (!fullscreen)
    {
        DrawRectangleLinesEx(area, px(2), t.accent2);
    }
    const Color green{80, 255, 140, 255};
    drawText(FontKind::Mono, "TEXT VIEW  -  the maze as characters, seen from above", area.x + px(16), area.y + px(10),
             px(18), fullscreen ? green : t.accent2);

    const int window = fullscreen ? 41 : 25;
    const int total = state.maze.tileCount;
    const int visible = std::min(total, window);
    const int firstRow = computeViewportStart(cellToTile(state.playerRow), total, window);
    const int firstCol = computeViewportStart(cellToTile(state.playerCol), total, window);
    const Rectangle grid{area.x + px(12), area.y + px(40), area.width - px(24), area.height - px(110)};
    // A monospace glyph is about 0.5 x its size wide; two glyphs make a square tile.
    const float tile = std::fmin(grid.width, grid.height) / static_cast<float>(visible);
    const float fontSize = tile * 0.98f;
    const float ox = grid.x + (grid.width - tile * visible) * 0.5f;
    const float oy = grid.y + (grid.height - tile * visible) * 0.5f;
    const int last = state.maze.size - 1;

    for (int r = 0; r < visible; ++r)
    {
        for (int c = 0; c < visible; ++c)
        {
            const int tr = firstRow + r;
            const int tc = firstCol + c;
            const char* symbol = "  ";
            Color color = fullscreen ? green : Color{200, 200, 220, 255};
            const bool isCell = tr % 2 == 1 && tc % 2 == 1;
            const int row = tr / 2;
            const int col = tc / 2;
            if (!tileDiscovered(tr, tc))
            {
                symbol = "~~";
                color = Color{40, 60, 50, 255};
            }
            else if (isCell && row == state.playerRow && col == state.playerCol)
            {
                symbol = "()";
                color = t.player;
            }
            else if (isCell && row == last && col == last)
            {
                symbol = "EX";
                color = t.exit;
            }
            else
            {
                switch (state.maze.tiles[tr][tc])
                {
                case Tile::Wall:
                    symbol = "##";
                    color = fullscreen ? Color{40, 170, 90, 255} : lerpColor(t.wallTop, WHITE, 0.3f);
                    break;
                case Tile::Door:
                    symbol = "[]";
                    color = t.door;
                    break;
                case Tile::Key:
                    symbol = "o-";
                    color = t.key;
                    break;
                case Tile::Path:
                    if (state.hintVisible && state.hintTiles[tr][tc])
                    {
                        symbol = "::";
                        color = t.hint;
                    }
                    else if (isCell && state.visitedCells[row][col])
                    {
                        symbol = "..";
                        color = Color{120, 140, 130, 255};
                    }
                    break;
                }
                if (isCell && state.maze.tiles[tr][tc] != Tile::Key)
                {
                    switch (state.items[row][col])
                    {
                    case Item::Gem:
                        symbol = "<>";
                        color = t.gem;
                        break;
                    case Item::Spikes:
                        symbol = areSpikesUp(state, row, col) ? "^^" : "__";
                        color = Color{255, 90, 90, 255};
                        break;
                    case Item::Teleporter:
                    {
                        static const char* const PADS[4] = {"O1", "O2", "O3", "O4"};
                        const int pair = teleporterPairIndex(state, row, col);
                        symbol = PADS[pair % 4];
                        color = teleporterPairColor(pair);
                        break;
                    }
                    case Item::None:
                        break;
                    default:
                        symbol = "++";
                        color = itemColor(state.items[row][col]);
                        break;
                    }
                }
                for (int i = 0; i < state.minotaurCount && isCell; ++i)
                {
                    if (state.minotaurs[i].released && state.minotaurs[i].row == row && state.minotaurs[i].col == col)
                    {
                        symbol = "MM";
                        color = Color{255, 50, 40, 255};
                    }
                }
            }
            if (symbol[0] == ' ')
            {
                continue;
            }
            drawText(FontKind::Mono, symbol, ox + static_cast<float>(c) * tile, oy + static_cast<float>(r) * tile,
                     fontSize, color);
        }
    }
    const char* legend = "() you  EX exit  ## wall  [] door  o- key  <> gem  ++ power-up  ^^ spikes  O1 O2 O3 teleporter pairs  "
                         "MM Minotaur  :: hint  .. footprints";
    drawWrappedText(FontKind::Mono, legend, area.x + px(16), area.y + area.height - px(64), area.width - px(32), px(15),
                    fullscreen ? green : Color{180, 180, 200, 255});
}

void drawCompass()
{
    if (state.compassTime <= 0.0f)
    {
        return;
    }
    // The compass points to the nearest gem (then the exit), and its route
    // is drawn on the floor for free while it lasts.
    int targetRow = 0;
    int targetCol = 0;
    if (!getGuideTarget(state, targetRow, targetCol))
    {
        return;
    }
    const bool toGem = getItem(state, targetRow, targetCol) == Item::Gem;
    const Vector3 goal = cellCenter(targetRow, targetCol);
    const float worldAngle = std::atan2(goal.x - visualPos.x, goal.z - visualPos.z);
    // Behind-the-player cameras: "up" on the compass is where you face, and
    // the camera's right is world -x when facing +z, so the angle is yaw - target.
    // Top-down: up on the screen is world -z.
    const float a = relativeControls() ? yaw - worldAngle : PI - worldAngle;
    const Vector2 c{static_cast<float>(ui.width) * 0.5f, px(150)};
    DrawCircleV(c, px(34), Color{0, 0, 0, 160});
    DrawRing(c, px(30), px(34), 0, 360, 32, itemColor(Item::Compass));
    const Vector2 tip{c.x + std::sin(a) * px(26), c.y - std::cos(a) * px(26)};
    const Vector2 side{std::cos(a) * px(9), std::sin(a) * px(9)};
    DrawTriangle(tip, Vector2Subtract(c, side), Vector2Add(c, side), Color{255, 60, 60, 255});
    DrawTriangle(tip, Vector2Add(c, side), Vector2Subtract(c, side), Color{255, 60, 60, 255});
    drawTextCentered(FontKind::Body, toGem ? "NEAREST GEM" : "EXIT", c.x, c.y + px(38), px(18), itemColor(Item::Compass));
}

void drawTimerRing(Vector2 center, float radius, float fraction, Item item)
{
    DrawCircleV(center, radius, Color{0, 0, 0, 170});
    DrawRing(center, radius * 0.82f, radius, -90.0f, -90.0f + 360.0f * fraction, 32, itemColor(item));
    drawItemIcon(item, center, radius * 1.2f, app.time);
}

void drawHud()
{
    const Theme& t = theme();
    const float w = static_cast<float>(ui.width);

    // ---- Top-left: title, lives and inventory ----
    const Rectangle panel{px(16), px(16), px(370), px(118)};
    drawPanel(panel, withAlpha(t.panel, 0.88f), t.accent);
    drawText(FontKind::Display, runTitle().c_str(), panel.x + px(16), panel.y + px(10), px(16), t.accent3);
    for (int i = 0; i < state.maxLives; ++i)
    {
        const bool full = i < state.lives;
        const float beat = full && state.lives == 1 ? 1.0f + 0.15f * std::sin(app.time * 8.0f) : 1.0f;
        drawHeart(Vector2{panel.x + px(30) + static_cast<float>(i) * px(34), panel.y + px(54)}, px(28) * beat,
                  full ? Color{255, 60, 90, 255} : Color{60, 50, 60, 255});
    }
    float x = panel.x + px(26);
    const float iconY = panel.y + px(94);
    drawKeyIcon(Vector2{x, iconY}, px(30), t.key);
    drawText(FontKind::Display, std::to_string(state.keysHeld).c_str(), x + px(22), iconY - px(11), px(20), WHITE);
    x += px(70);
    if (state.gemsTotal > 0)
    {
        drawItemIcon(Item::Gem, Vector2{x, iconY}, px(26), app.time);
        const std::string gems = std::to_string(state.gemsCollected) + "/" + std::to_string(state.gemsTotal);
        drawText(FontKind::Display, gems.c_str(), x + px(20), iconY - px(11), px(20),
                 state.config.requireAllGems && state.gemsCollected < state.gemsTotal ? Color{255, 140, 140, 255} : WHITE);
        x += px(100);
    }
    if (state.hammerCharges > 0)
    {
        drawItemIcon(Item::Hammer, Vector2{x, iconY}, px(28), app.time);
        drawText(FontKind::Display, std::to_string(state.hammerCharges).c_str(), x + px(20), iconY - px(11), px(20),
                 WHITE);
        x += px(60);
    }
    if (state.torchLevel > 0)
    {
        drawItemIcon(Item::Torch, Vector2{x, iconY}, px(28), app.time);
        drawText(FontKind::Display, std::to_string(state.torchLevel).c_str(), x + px(20), iconY - px(11), px(20), WHITE);
    }

    // ---- Top-centre: clock ----
    const int secondsLeft = getSecondsLeft(state);
    const bool countdown = secondsLeft >= 0;
    const int shown = countdown ? secondsLeft : static_cast<int>(state.elapsed);
    const bool urgent = countdown && secondsLeft <= 30;
    const float clockSize = px(44) * (urgent ? 1.0f + 0.08f * std::sin(app.time * 10.0f) : 1.0f);
    DrawRectangleRounded(Rectangle{w * 0.5f - px(140), px(12), px(280), px(92)}, 0.4f, 8, Color{0, 0, 0, 120});
    drawGlowText(FontKind::Display, formatClock(shown).c_str(), w * 0.5f, px(18), clockSize,
                 urgent ? Color{255, 80, 70, 255} : WHITE, urgent ? Color{255, 0, 0, 255} : t.accent);
    std::string sub = std::to_string(state.moves);
    const int stepPar = computeStepPar(state);
    sub += stepPar > 0 ? " / " + std::to_string(stepPar) + " steps" : " steps";
    if (run.mode == RunMode::Campaign)
    {
        sub += "   |   par " + formatClock(computeParSeconds(state));
    }
    sub += std::string("   |   ") + getDifficultyName(state.config.difficulty);
    drawTextCentered(FontKind::Body, sub.c_str(), w * 0.5f, px(72), px(22), Color{210, 210, 230, 220});
    drawCompass();

    // ---- Top-right: minimap ----
    if (textViewMode == 0)
    {
        drawMinimap();
    }

    // ---- Bottom-left: active power-ups ----
    float ringX = px(52);
    const float ringY = static_cast<float>(ui.height) - px(60);
    if (state.bootsTime > 0.0f)
    {
        drawTimerRing(Vector2{ringX, ringY}, px(30), state.bootsTime / 12.0f, Item::Boots);
        ringX += px(76);
    }
    if (state.freezeTime > 0.0f)
    {
        drawTimerRing(Vector2{ringX, ringY}, px(30), state.freezeTime / 8.0f, Item::Hourglass);
        ringX += px(76);
    }
    if (state.compassTime > 0.0f)
    {
        drawTimerRing(Vector2{ringX, ringY}, px(30), state.compassTime / 25.0f, Item::Compass);
    }

    // ---- Minotaur warning ----
    if (danger > 0.3f)
    {
        const float alpha = 0.5f + 0.5f * std::sin(app.time * 9.0f);
        drawGlowText(FontKind::Display, "! MINOTAUR NEARBY !", w * 0.5f, static_cast<float>(ui.height) * 0.22f, px(30),
                     withAlpha(Color{255, 60, 50, 255}, alpha), Color{255, 0, 0, 255});
    }

    // ---- Message ----
    if (messageTimer > 0.0f && !message.empty())
    {
        const float alpha = std::fmin(1.0f, messageTimer * 2.0f);
        const float size = px(28);
        const Vector2 m = measure(FontKind::Body, message.c_str(), size);
        const Rectangle box{w * 0.5f - m.x * 0.5f - px(24), static_cast<float>(ui.height) - px(118), m.x + px(48), px(48)};
        drawPanel(box, Color{0, 0, 0, static_cast<unsigned char>(190 * alpha)}, withAlpha(messageColor, alpha));
        drawTextCentered(FontKind::Body, message.c_str(), w * 0.5f, box.y + px(9), size, withAlpha(messageColor, alpha));
    }

    // ---- Controls ----
    const char* controls = relativeControls()
                               ? "Arrows/WASD walk & turn   Q/E strafe   V view   T text   M map   H hint   SPACE hammer   ESC pause"
                               : "Arrows/WASD move   wheel zoom   V view   T text   M map   H hint   SPACE hammer   ESC pause";
    drawTextCentered(FontKind::Body, controls, w * 0.5f, static_cast<float>(ui.height) - px(32), px(19),
                     Color{220, 220, 240, 170});

    // ---- Intro banner ----
    if (introTimer > 0.0f)
    {
        const float alpha = std::fmin(1.0f, introTimer) * std::fmin(1.0f, (3.0f - introTimer) * 3.0f);
        const float y = static_cast<float>(ui.height) * 0.36f;
        DrawRectangleGradientH(0, static_cast<int>(y - px(16)), static_cast<int>(w), static_cast<int>(px(110)),
                               withAlpha(BLACK, 0.0f), withAlpha(BLACK, 0.7f * alpha));
        drawGlowText(FontKind::Display, runTitle().c_str(), w * 0.5f, y, px(40), withAlpha(WHITE, alpha),
                     withAlpha(t.accent, alpha));
        std::string subtitle = "Find the exit in the far corner.";
        if (run.mode == RunMode::Campaign)
        {
            subtitle = getLevel(run.index).description;
        }
        else if (run.mode == RunMode::Challenge)
        {
            subtitle = getChallengeInfo(static_cast<ChallengeId>(run.index)).description;
        }
        else if (run.mode == RunMode::Custom)
        {
            subtitle = run.testPlay ? "Beat your map to verify it - then you can publish it."
                                    : "A map by " + (app.pendingMap.author.empty() ? std::string("someone") : app.pendingMap.author);
        }
        drawTextCentered(FontKind::Body, subtitle.c_str(), w * 0.5f, y + px(56), px(26), withAlpha(t.accent2, alpha));
    }
}

// ---------------------------------------------------------------------------
// Overlays: full map, pause, results
// ---------------------------------------------------------------------------

void updateMapOverlay()
{
    const Theme& t = theme();
    DrawRectangle(0, 0, ui.width, ui.height, Color{0, 0, 0, 215});
    beginUi(t.accent, t.accent2, t.panel);
    drawGlowText(FontKind::Display, "MAP", static_cast<float>(ui.width) * 0.5f, px(16), px(36), WHITE, t.accent);
    const float size = static_cast<float>(ui.height) - px(150);
    const Rectangle area{static_cast<float>(ui.width) * 0.5f - size * 0.5f, px(70), size, size};
    drawMapArea(area, state.maze.tileCount, true);
    drawTextCentered(FontKind::Body,
                     state.config.fogOfWar ? "Only places you have seen are shown.  M / ESC to close"
                                           : "M / ESC to close",
                     static_cast<float>(ui.width) * 0.5f, static_cast<float>(ui.height) - px(60), px(24), t.accent2);
    if (!overlayJustOpened && (IsKeyPressed(KEY_M) || ui.back || IsMouseButtonPressed(MOUSE_BUTTON_LEFT)))
    {
        mapOpen = false;
    }
    endUi();
}

void updatePauseMenu()
{
    const Theme& t = theme();
    DrawRectangle(0, 0, ui.width, ui.height, Color{0, 0, 0, 170});
    beginUi(t.accent, t.accent2, t.panel);
    const float w = static_cast<float>(ui.width);
    const Rectangle panel{w * 0.5f - px(260), px(90), px(520), px(560)};
    drawPanel(panel, withAlpha(t.panel, 0.97f), t.accent, 2.0f);
    drawGlowText(FontKind::Display, "PAUSED", w * 0.5f, panel.y + px(24), px(44), WHITE, t.accent);
    const float bx = panel.x + px(50);
    const float bw = panel.width - px(100);
    float y = panel.y + px(100);
    if (button("RESUME", Rectangle{bx, y, bw, px(52)}) || (ui.back && !overlayJustOpened))
    {
        paused = false;
    }
    y += px(64);
    if (button("RESTART", Rectangle{bx, y, bw, px(52)}))
    {
        restartRun();
    }
    y += px(64);
    if (button(run.mode == RunMode::Custom ? "SAVE & QUIT (not for custom maps)" : "SAVE & QUIT", Rectangle{bx, y, bw, px(52)},
               run.mode != RunMode::Custom))
    {
        saveAndQuit();
    }
    y += px(64);
    AppSettings& s = app.profile.settings;
    if (slider("Music", Rectangle{bx, y, bw, px(48)}, s.musicVolume, 0, 100, "%") |
        slider("Effects", Rectangle{bx, y + px(58), bw, px(48)}, s.sfxVolume, 0, 100, "%"))
    {
        setVolumes(s.masterVolume, s.musicVolume, s.sfxVolume);
    }
    y += px(116);
    toggle("Retro CRT effect", Rectangle{bx, y, bw, px(48)}, s.crtEffect);
    y += px(64);
    if (button(run.testPlay ? "BACK TO EDITOR" : "QUIT TO MENU", Rectangle{bx, y, bw, px(52)}))
    {
        saveProfile();
        goTo(run.testPlay ? ScreenId::Editor : run.mode == RunMode::Custom ? ScreenId::Maps : ScreenId::MainMenu);
    }
    endUi();
}

void updateResults(float dt)
{
    const Theme& t = theme();
    const float age = endTimer - RESULTS_DELAY;
    const float w = static_cast<float>(ui.width);
    DrawRectangle(0, 0, ui.width, ui.height, Color{0, 0, 0, static_cast<unsigned char>(std::fmin(180.0f, age * 300.0f))});
    beginUi(t.accent, t.accent2, t.panel);
    const Rectangle panel{w * 0.5f - px(330), px(30), px(660), px(660)};
    const float pop = smoothStep(age * 3.0f);
    drawPanel(Rectangle{panel.x, panel.y + (1.0f - pop) * px(60), panel.width, panel.height}, withAlpha(t.panel, 0.96f * pop),
              result.won ? t.accent3 : Color{255, 70, 60, 255}, 2.0f);

    if (result.won)
    {
        drawRainbowTitle("ESCAPED!", w * 0.5f, panel.y + px(18), px(62), app.time);
    }
    else
    {
        const char* title = state.lossReason == LossReason::OutOfTime ? "TIME'S UP!" : "CAUGHT!";
        drawGlowText(FontKind::Display, title, w * 0.5f, panel.y + px(22), px(58), Color{255, 80, 70, 255},
                     Color{255, 0, 0, 255});
    }

    // Stars appear one by one.
    if (run.mode == RunMode::Campaign && result.won)
    {
        for (int i = 0; i < 3; ++i)
        {
            const float appear = age - 0.5f - 0.4f * static_cast<float>(i);
            const bool earned = i < stars;
            if (appear > 0.0f && earned && starsShown <= i)
            {
                starsShown = i + 1;
                playSfx(Sfx::Star, 1.0f + 0.15f * static_cast<float>(i));
                spawnBurst2D(Vector2{w * 0.5f + static_cast<float>(i - 1) * px(90), panel.y + px(140)},
                             Color{255, 215, 60, 255}, 40, 400.0f, 6.0f);
            }
            const float scale = earned && appear > 0.0f ? 1.0f + 0.4f * std::fmax(0.0f, 1.0f - appear * 4.0f) : 1.0f;
            drawStar(Vector2{w * 0.5f + static_cast<float>(i - 1) * px(90), panel.y + px(140)}, px(36) * scale,
                     earned && appear > 0.0f ? Color{255, 215, 60, 255} : Color{60, 60, 75, 255},
                     earned && appear > 0.0f ? std::sin(app.time * 2.0f) * 0.1f : 0.0f);
        }
    }

    // Statistics
    float y = panel.y + px(200);
    const float lx = panel.x + px(70);
    const float rw = panel.width - px(140);
    auto line = [&](const char* label, const std::string& value, Color color) {
        drawText(FontKind::Body, label, lx, y, px(26), Color{200, 200, 220, 255});
        drawTextRight(FontKind::Display, value.c_str(), lx + rw, y + px(2), px(22), color);
        y += px(36);
    };
    line("Time", formatClock(result.seconds) + (run.mode == RunMode::Campaign ? "  (par " + formatClock(result.parSeconds) + ")" : ""),
         run.mode == RunMode::Campaign && result.seconds > result.parSeconds ? Color{255, 140, 120, 255} : WHITE);
    const std::string stepTarget = result.stepPar > 0 ? "  (3 stars: " + std::to_string(result.stepPar) + ")"
                                                      : "  (shortest " + std::to_string(result.optimalMoves) + ")";
    line("Steps", std::to_string(result.moves) + stepTarget,
         result.stepPar > 0 && result.moves > result.stepPar ? Color{255, 140, 120, 255} : WHITE);
    line("Difficulty", getDifficultyName(result.difficulty), t.accent2);
    if (result.gemsTotal > 0)
    {
        line("Gems", std::to_string(result.gemsCollected) + " / " + std::to_string(result.gemsTotal), t.gem);
    }
    line("Doors opened", std::to_string(result.doorsOpened), t.door);
    line("Hints used", std::to_string(result.hintsUsed), result.hintsUsed > 0 ? Color{255, 140, 120, 255} : WHITE);
    line("Lives lost", std::to_string(result.livesLost), result.livesLost > 0 ? Color{255, 140, 120, 255} : WHITE);

    if (result.won)
    {
        const int target = result.score;
        shownScore = std::min(target, shownScore + std::max(1, static_cast<int>(static_cast<float>(target) * dt * 1.2f)));
        if (age < 0.4f)
        {
            shownScore = 0;
        }
        drawGlowText(FontKind::Display, ("SCORE  " + std::to_string(shownScore)).c_str(), w * 0.5f, y + px(8), px(40),
                     t.accent3, t.accent);
        if (newBest && shownScore == target)
        {
            const float pulse = 1.0f + 0.1f * std::sin(app.time * 8.0f);
            drawTextCentered(FontKind::Display, "NEW BEST!", w * 0.5f, y + px(58), px(22) * pulse, Color{255, 215, 60, 255});
        }
    }
    else
    {
        const char* tip = state.lossReason == LossReason::OutOfTime
                              ? "Tip: Speed Boots and the hint make the route much faster."
                              : "Tip: Hourglasses freeze Minotaurs. Listen for the heartbeat!";
        drawTextCentered(FontKind::Body, tip, w * 0.5f, y + px(16), px(24), t.accent2);
    }

    // Buttons. They work straight away: mouse, Left/Right/Up/Down + Enter,
    // or the shortcut keys shown underneath.
    if (!resultsFocusReset)
    {
        resultsFocusReset = true;
        resetFocus(); // start on the first button, whatever menu was used before
    }
    const float by = panel.y + panel.height - px(84);
    const bool test = run.mode == RunMode::Custom && run.testPlay;
    const bool hasNext = run.mode == RunMode::Campaign && result.won && run.index + 1 < LEVEL_COUNT;
    const int buttonCount = hasNext ? 3 : 2;
    const float gap = px(24);
    const float bw = (panel.width - px(60) - gap * static_cast<float>(buttonCount - 1)) / static_cast<float>(buttonCount);
    float bx = panel.x + px(30);

    if (IsKeyPressed(KEY_RIGHT) || IsKeyPressed(KEY_D))
    {
        ui.focus = (ui.focus + 1) % buttonCount;
        playSfx(Sfx::MenuMove);
    }
    if (IsKeyPressed(KEY_LEFT) || IsKeyPressed(KEY_A))
    {
        ui.focus = (ui.focus + buttonCount - 1) % buttonCount;
        playSfx(Sfx::MenuMove);
    }

    auto goNext = [&]() {
        app.pendingRun.mode = RunMode::Campaign;
        app.pendingRun.index = run.index + 1;
        app.pendingRun.theme = getLevel(run.index + 1).theme;
        app.pendingRun.config = getLevel(run.index + 1).config;
        app.pendingRun.config.difficulty = app.profile.settings.difficulty;
        app.pendingRun.testPlay = false;
        app.selectedLevel = run.index + 1;
        app.resumeRequested = false;
        goTo(ScreenId::Game);
    };
    auto goMenu = [&]() {
        goTo(test                              ? ScreenId::Editor
             : run.mode == RunMode::Custom     ? ScreenId::Maps
             : run.mode == RunMode::Campaign   ? ScreenId::Campaign
             : run.mode == RunMode::Challenge  ? ScreenId::Challenges
                                               : ScreenId::FreePlay);
    };

    if (hasNext)
    {
        if (button("NEXT LEVEL", Rectangle{bx, by, bw, px(54)}))
        {
            goNext();
        }
        bx += bw + gap;
    }
    if (button(result.won ? (test ? "PLAY IT AGAIN" : "PLAY AGAIN") : "RETRY", Rectangle{bx, by, bw, px(54)}))
    {
        restartRun();
    }
    bx += bw + gap;
    if (button(test ? "BACK TO EDITOR" : "MENU", Rectangle{bx, by, bw, px(54)}))
    {
        goMenu();
    }

    if (!app.switching)
    {
        if (hasNext && IsKeyPressed(KEY_N))
        {
            goNext();
        }
        else if (IsKeyPressed(KEY_R))
        {
            restartRun();
        }
        else if (IsKeyPressed(KEY_ESCAPE) || IsKeyPressed(KEY_BACKSPACE) || IsKeyPressed(KEY_M))
        {
            goMenu();
        }
    }
    const std::string keys = std::string(hasNext ? "N next   " : "") + "R " + (result.won ? "play again" : "retry") +
                             "   ESC " + (test ? "editor" : "menu") + "   ENTER selected button";
    drawTextCentered(FontKind::Body, keys.c_str(), w * 0.5f, by + px(62), px(18), Color{200, 200, 220, 200});
    if (test && result.won)
    {
        drawTextCentered(FontKind::Display, "MAP VERIFIED - READY TO PUBLISH", w * 0.5f, by - px(40), px(22),
                         Color{90, 255, 150, 255});
    }
    else if (test)
    {
        drawTextCentered(FontKind::Body, "Beat your map to verify it. Is it too hard? Change it in the editor.", w * 0.5f,
                         by - px(36), px(20), Color{255, 160, 120, 255});
    }
    endUi();
}

} // namespace

// ---------------------------------------------------------------------------
// Screen functions
// ---------------------------------------------------------------------------

void enterGame()
{
    if (app.resumeRequested)
    {
        app.resumeRequested = false;
        std::string text;
        SaveMeta meta;
        if (readTextFile(runSavePath(), text) && deserializeRun(text, state, meta))
        {
            run.mode = static_cast<RunMode>(meta.mode);
            run.index = meta.index;
            run.theme = static_cast<ThemeId>(meta.theme);
            run.config = state.config;
            startSession();
            showMessage("Welcome back! Your game was restored.", theme().accent2, 3.0f);
            return;
        }
        showToast("COULD NOT LOAD", "The saved game was damaged - starting fresh", Color{255, 90, 90, 255});
        std::remove(runSavePath().c_str());
    }
    run = app.pendingRun;
    if (run.mode == RunMode::Custom)
    {
        if (startCustomRun(state, app.pendingMap))
        {
            startSession();
            return;
        }
        showToast("CAN'T PLAY THIS MAP", "Open it in the editor to see what is wrong", Color{255, 90, 90, 255});
        run.mode = RunMode::FreePlay;
        run.testPlay = false;
    }
    if (!startRun(state, run.config))
    {
        RunConfig fallback;
        startRun(state, fallback);
    }
    startSession();
}

void leaveGame()
{
    endGame(state);
    clearParticles();
    invalidateMenuBackground();
}

void updateGame(float dt)
{
    const Theme& t = theme();
    const bool overlay = paused || mapOpen;

    // ---- Logic ----
    if (!overlay && !finished && introTimer < 2.6f)
    {
        handleMovementInput();
        handleActionKeys();
        updateRun(state, dt);
    }
    else if (!overlay && !finished)
    {
        handleActionKeys();
    }
    handleEvents();
    if (finished)
    {
        endTimer += dt;
        if (endTimer > 0.3f)
        {
            recordResults();
        }
    }
    if (!finished && !mapOpen && (IsKeyPressed(KEY_ESCAPE) || IsKeyPressed(KEY_P) ||
                                  (IsGamepadAvailable(0) && IsGamepadButtonPressed(0, GAMEPAD_BUTTON_MIDDLE_RIGHT))))
    {
        if (!paused)
        {
            paused = true;
            overlayJustOpened = true;
            resetFocus();
            playSfx(Sfx::MenuSelect);
        }
    }
    if (!paused)
    {
        updateVisuals(dt);
    }

    // ---- 3D scene ----
    PostEffects effects;
    effects.bloomStrength = t.bloomStrength;
    effects.bloomThreshold = t.bloomThreshold;
    effects.saturation = t.saturation;
    effects.crt = app.profile.settings.crtEffect;
    effects.time = app.time;
    effects.danger = danger;
    effects.freeze = state.freezeTime > 0.0f ? std::fmin(1.0f, state.freezeTime) : 0.0f;
    effects.flashColor = withAlpha(flashColor, flashAlpha);

    beginScene();
    if (textViewMode == 2)
    {
        ClearBackground(Color{2, 10, 6, 255});
        setUiStyle(t.accent, t.accent2, t.panel);
        drawTextView(Rectangle{0, 0, static_cast<float>(ui.width), static_cast<float>(ui.height)}, true);
        effects.crt = true;
        effects.bloomStrength = 0.9f;
        effects.bloomThreshold = 0.5f;
    }
    else
    {
        WorldView view;
        view.camera = makeCamera();
        view.playerPosition = visualPos;
        view.playerYaw = yaw;
        view.showPlayer = viewMode != VIEW_FIRST_PERSON;
        view.topDown = viewMode == VIEW_TOP_DOWN;
        view.hideUndiscovered = viewMode == VIEW_TOP_DOWN && state.config.fogOfWar;
        view.fogScale = viewMode == VIEW_TOP_DOWN ? 0.3f : (viewMode == VIEW_THIRD_PERSON ? 0.6f : 1.0f);
        view.time = app.time;
        view.minotaurPositions = &minotaurPos;
        view.minotaurYaws = &minotaurYaw;
        view.minotaurWalk = &minotaurWalk;
        drawWorld(state, run.theme, view, openingDoors);
    }
    endScene();
    drawSceneWithEffects(effects, app.profile.settings.bloom);

    // ---- 2D on top ----
    setUiStyle(t.accent, t.accent2, t.panel);
    if (textViewMode == 1)
    {
        const float panelWidth = static_cast<float>(ui.width) * 0.44f;
        drawTextView(Rectangle{static_cast<float>(ui.width) - panelWidth - px(16), px(150), panelWidth,
                               static_cast<float>(ui.height) - px(220)},
                     false);
    }
    if (!finished || endTimer < RESULTS_DELAY)
    {
        drawHud();
    }
    updateParticles2D(dt);
    drawParticles2D();

    if (mapOpen)
    {
        updateMapOverlay();
    }
    else if (paused)
    {
        updatePauseMenu();
    }
    else if (finished && endTimer >= RESULTS_DELAY && recorded)
    {
        updateResults(dt);
    }
    overlayJustOpened = false;
}

// Used by the automatic screenshot tour in main.cpp.
void tourGameAction(int action)
{
    if (app.screen != ScreenId::Game || state.maze.tiles == nullptr)
    {
        return;
    }
    introTimer = 0.0f;
    switch (action)
    {
    case 1:
    case 11:
    case 12:
    case 13:
    case 14:
    {
        // Walk a few steps along the route so the view is interesting.
        Path route;
        if (findShortestPath(state.maze, 0, 0, false, route))
        {
            for (int i = 1; i < route.length && i < 5; ++i)
            {
                const int dr = route.rows[i] - state.playerRow;
                const int dc = route.cols[i] - state.playerCol;
                const Direction d = dr > 0 ? Direction::Down : dr < 0 ? Direction::Up : dc > 0 ? Direction::Right : Direction::Left;
                state.facing = d; // look along the route
                if (i < 4)
                {
                    movePlayer(state, d);
                }
            }
            destroyPath(route);
        }
        handleEvents();
        snapVisuals();
        break;
    }
    case 2:
        viewMode = VIEW_TOP_DOWN;
        textViewMode = 0;
        mapOpen = false;
        break;
    case 3:
        textViewMode = 1;
        break;
    case 8:
        viewMode = VIEW_FIRST_PERSON;
        break;
    case 5:
        textViewMode = 0;
        mapOpen = true;
        break;
    case 20:
    {
        mapOpen = false;
        textViewMode = 0;
        state.invulnerableTime = 1000.0f;
        for (int r = 0; r < state.maze.size; ++r)
        {
            for (int c = 0; c < state.maze.size; ++c)
            {
                if (state.items[r][c] == Item::Teleporter)
                {
                    state.items[r][c] = Item::None; // keep the tour's walk simple
                }
            }
        }
        // Walk the shortest route; if a teleporter moves us, plan again.
        for (int attempt = 0; attempt < 8 && !state.won; ++attempt)
        {
            Path route;
            if (!findShortestPath(state.maze, state.playerRow, state.playerCol, false, route))
            {
                break;
            }
            for (int i = 1; i < route.length && !state.won; ++i)
            {
                const int dr = route.rows[i] - state.playerRow;
                const int dc = route.cols[i] - state.playerCol;
                if (std::abs(dr) + std::abs(dc) != 1)
                {
                    break; // teleported away
                }
                const Direction d = dr > 0 ? Direction::Down : dr < 0 ? Direction::Up : dc > 0 ? Direction::Right : Direction::Left;
                movePlayer(state, d);
            }
            destroyPath(route);
            if (state.config.requireAllGems)
            {
                state.gemsCollected = state.gemsTotal;
            }
        }
        state.elapsed = 212.0;
        snapVisuals();
        endTimer = 0.2f;
        break;
    }
    case 21:
        finished = false;
        paused = true;
        break;
    case 6:
    {
        // Stand two cells from the first Minotaur, looking at it.
        if (state.minotaurCount == 0)
        {
            break;
        }
        Minotaur& m = state.minotaurs[0];
        const Direction dirs[4] = {Direction::Up, Direction::Down, Direction::Left, Direction::Right};
        for (const Direction d : dirs)
        {
            if (isPassable(state.maze, m.row, m.col, d, false))
            {
                int dr = 0;
                int dc = 0;
                getDirectionOffset(d, dr, dc);
                state.playerRow = m.row + dr;
                state.playerCol = m.col + dc;
                state.facing = opposite(d);
                break;
            }
        }
        m.stepTimer = 1000.0f;
        state.invulnerableTime = 1000.0f;
        state.discovered[state.playerRow][state.playerCol] = true;
        viewMode = VIEW_FIRST_PERSON;
        snapVisuals();
        break;
    }
    case 7:
    {
        // Put one of every item around the player and look from above.
        const Item showcase[10] = {Item::Gem, Item::Torch, Item::Boots, Item::Hammer, Item::Hourglass,
                                   Item::Heart, Item::Compass, Item::Spikes, Item::Teleporter, Item::Gem};
        int placed = 0;
        for (int dr = -2; dr <= 2 && placed < 10; ++dr)
        {
            for (int dc = -2; dc <= 2 && placed < 10; ++dc)
            {
                const int r = state.playerRow + dr;
                const int c = state.playerCol + dc;
                if ((dr != 0 || dc != 0) && isInsideMaze(state.maze, r, c) && !isExitCell(state.maze, r, c))
                {
                    state.items[r][c] = showcase[placed++];
                }
            }
        }
        viewMode = VIEW_TOP_DOWN;
        topZoom = 9.0f;
        break;
    }
    default:
        break;
    }
}
