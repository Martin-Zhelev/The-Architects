// game.h
// Game rules for the Labyrinth mode: the player, movement, keys, doors,
// power-ups, traps, teleporters, the Minotaur, fog of war, hints, scoring
// and the leaderboard.
//
// Pure logic: these functions never use cin / cout and never draw anything.
// The game reports what happened through a small event list that the user
// interface reads (to play sounds, spawn particles, show messages...).

#ifndef GAME_H
#define GAME_H

#include "maze.h"

#include <cstddef>
#include <random>
#include <string>

// What happened when the player tried to move.
enum class MoveResult
{
    Moved,
    PickedUpKey,
    PickedUpItem,
    OpenedDoor,
    Teleported,
    Hurt,
    Caught,
    ExitLocked,
    ReachedExit,
    GameOver,
    BlockedByWall,
    BlockedByLockedDoor,
    BlockedByEdge
};

// Things that can lie on a cell (keys are stored in the maze as Tile::Key).
enum class Item : unsigned char
{
    None,
    Gem,        // treasure: +50 points
    Torch,      // see further in the fog
    Boots,      // move faster for a while
    Hammer,     // break walls (2 charges)
    Hourglass,  // freeze the Minotaur for a while
    Heart,      // +1 life
    Compass,    // shows the direction of the exit for a while
    Spikes,     // trap: hurts while the spikes are up
    Teleporter  // pad that moves you to its twin pad
};

const int ITEM_TYPE_COUNT = 10;

// How a run is set up. Campaign levels, challenges and free play all build one.
struct RunConfig
{
    int size = 15;                     // cells per side (10 - 50)
    unsigned int seed = 0;             // same seed = same maze and same items
    int lives = 3;                     // 1 - 5
    int minotaurCount = 0;             // 0 - 3
    float minotaurStepSeconds = 0.55f; // seconds per Minotaur step
    bool fogOfWar = false;             // map is revealed only around the player
    bool powerUps = false;             // torches, boots, hammers, ...
    bool traps = false;                // spike traps
    int teleporterPairs = 0;           // 0 - 3
    int gemCount = 0;                  // 0 - 60
    bool requireAllGems = false;       // exit stays sealed until all gems are found
    int timeLimitSeconds = 0;          // 0 = no limit
    bool hintsAllowed = true;
    int difficulty = 1;                // 0 Easy, 1 Normal, 2 Hard, 3 Expert
    bool minotaurChase = false;        // gauntlet: Minotaurs start behind the player and always hunt
};

// ---- Difficulty ----
const int DIFFICULTY_COUNT = 4;
const char* getDifficultyName(int difficulty);
float getParTimeFactor(int difficulty);    // how much time a 2-3 star run may take
float getStepParFactor(int difficulty);    // allowed steps for 3 stars (0 = no step limit)
float getMinotaurSpeedFactor(int difficulty); // multiplies the Minotaur's seconds per step
float getScoreFactor(int difficulty);

// Events the logic reports to the interface.
enum class EventType : unsigned char
{
    Step,
    Bump,
    KeyPickup,
    DoorOpened,
    GemPickup,
    PowerUp,       // value = (int)Item
    Teleport,
    Hurt,
    Caught,
    MinotaurStep,  // value = index of the Minotaur
    MinotaurAlert, // value = index of the Minotaur that started hunting
    WallBroken,
    ExitLocked,
    MinotaurReleased, // value = index of the Minotaur (gauntlet)
    TimeWarning,   // value = seconds left
    Won,
    Lost
};

struct GameEvent
{
    EventType type = EventType::Step;
    int row = 0;
    int col = 0;
    int value = 0;
};

const int MAX_EVENTS = 64;

enum class LossReason
{
    None,
    OutOfLives,
    OutOfTime
};

struct Minotaur
{
    int row = 0;
    int col = 0;
    int prevRow = 0; // where it came from (for smooth animation and wandering)
    int prevCol = 0;
    int spawnRow = 0;
    int spawnCol = 0;
    bool hunting = false;
    float stepTimer = 0.0f;   // seconds until the next step
    float releaseTime = 0.0f; // gauntlet: seconds of play before it starts chasing
    bool released = true;     // false while it waits at the start (gauntlet)
};

// Everything that describes one running game.
struct GameState
{
    Maze maze;
    RunConfig config;

    int playerRow = 0;
    int playerCol = 0;
    Direction facing = Direction::Right;
    int moves = 0;
    int keysHeld = 0;
    int doorsOpened = 0;
    int hintsUsed = 0;
    int optimalMoves = 0;          // shortest route along normal corridors
    int tourMoves = 0;             // start -> every gem -> exit, used for par time and step par
    bool won = false;
    bool lost = false;
    LossReason lossReason = LossReason::None;
    bool hintVisible = false;

    // Dynamic grids
    bool** visitedCells = nullptr; // size x size, footprints
    bool** hintTiles = nullptr;    // tileCount x tileCount, hint overlay
    bool** discovered = nullptr;   // size x size, fog of war
    Item** items = nullptr;        // size x size
    int* teleportLinks = nullptr;  // size*size, index of the twin pad or -1

    // Minotaurs (dynamic array)
    Minotaur* minotaurs = nullptr;
    int minotaurCount = 0;

    // Player resources and timers
    int lives = 3;
    int maxLives = 5;
    int gemsCollected = 0;
    int gemsTotal = 0;
    int torchLevel = 0;
    int hammerCharges = 0;
    float bootsTime = 0.0f;
    float freezeTime = 0.0f;
    float compassTime = 0.0f;
    float invulnerableTime = 0.0f;
    double elapsed = 0.0; // seconds of play
    int lastTimeWarning = 0;

    // Statistics
    int wallsBroken = 0;
    int teleportsUsed = 0;
    int timesCaught = 0;
    int damageTaken = 0;
    int powerUpsCollected = 0;

    GameEvent events[MAX_EVENTS];
    int eventCount = 0;

    std::mt19937 rng;
};

// ---- Game lifecycle ----
bool startRun(GameState& state, const RunConfig& config);
bool startGame(GameState& state, int size, unsigned int seed); // classic rules
void endGame(GameState& state);
unsigned int generateRandomSeed();
RunConfig clampConfig(const RunConfig& config);
// Building blocks of startRun, also used for hand-made maps:
// initRunGrids needs state.maze ready; finishRunSetup counts gems,
// computes the reference routes and reveals the start.
void initRunGrids(GameState& state, const RunConfig& clampedConfig);
void placeMinotaur(GameState& state, int index, int row, int col); // state.minotaurs must exist
void finishRunSetup(GameState& state);

// ---- Rules ----
MoveResult movePlayer(GameState& state, Direction direction);
void updateRun(GameState& state, float deltaSeconds);
bool useHammer(GameState& state, Direction direction);
bool isMoveSuccessful(MoveResult result);
bool isExitCell(const Maze& maze, int row, int col);
bool isExitUnlocked(const GameState& state);
bool toggleHint(GameState& state);
void refreshHint(GameState& state);
int countVisitedCells(const GameState& state);
int countDiscoveredCells(const GameState& state);
int calculateScore(const GameState& state, long long elapsedSeconds);
int getRevealRadius(const GameState& state);
void revealAround(GameState& state);
bool areSpikesUp(const GameState& state, int row, int col);
// 0 = spikes fully down, 1 = fully up, in between while moving (for animation).
float getSpikeLift(const GameState& state, int row, int col);
Item getItem(const GameState& state, int row, int col);
int getTeleportTarget(const GameState& state, int row, int col);
int getSecondsLeft(const GameState& state); // -1 when there is no time limit
int distanceToNearestMinotaur(const GameState& state); // ignores Minotaurs not yet released
void clearEvents(GameState& state);
float getMinotaurStepSeconds(const GameState& state); // after difficulty
// Where the compass and hints lead: the nearest gem while the compass is
// active (or the exit is sealed), otherwise the exit. False if nothing to find.
bool getGuideTarget(const GameState& state, int& row, int& col);
int getHammerCost(); // score lost per broken wall
// Cell is a dead end: exactly one open side (doors count as open).
bool isDeadEnd(const Maze& maze, int row, int col);
// Greedy tour length: start -> nearest gem -> ... -> exit, through doors closed.
int computeTourMoves(const GameState& state, int fromRow, int fromCol);

// ---- Camera ----
// First visible row/column of a scrolling window that keeps `focus` centred.
int computeViewportStart(int focus, int total, int window);

// ---- Leaderboard ----
const int LEADERBOARD_SIZE = 5;

struct ScoreEntry
{
    std::string name;
    int mazeSize = 0;
    int moves = 0;
    long long seconds = 0;
    int score = 0;
};

struct Leaderboard
{
    ScoreEntry entries[LEADERBOARD_SIZE]; // sorted, best score first
    int count = 0;
};

bool qualifiesForLeaderboard(const Leaderboard& board, int score);
bool addToLeaderboard(Leaderboard& board, const ScoreEntry& entry);

#endif // GAME_H
