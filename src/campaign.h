// campaign.h
// Campaign levels, challenge modes and star ratings.
// Pure logic: no input, output or drawing.

#ifndef CAMPAIGN_H
#define CAMPAIGN_H

#include "game.h"

// Visual worlds. The logic only stores which one a level uses;
// the app decides what each one looks like.
enum class ThemeId
{
    Neon,
    Dungeon,
    Lava,
    Ice,
    Candy
};

const int THEME_COUNT = 5;
const char* getThemeName(ThemeId theme);

// ---- Campaign ----
const int WORLD_COUNT = 5;
const int LEVELS_PER_WORLD = 3;
const int LEVEL_COUNT = WORLD_COUNT * LEVELS_PER_WORLD;

struct LevelDef
{
    const char* name;
    const char* description;
    ThemeId theme;
    RunConfig config;
};

const LevelDef& getLevel(int index);
const char* getWorldName(int world);

// Summary of a finished run, used for stars, achievements and statistics.
struct RunResult
{
    bool won = false;
    int size = 0;
    int moves = 0;
    int optimalMoves = 0;
    int seconds = 0;
    int hintsUsed = 0;
    int livesLost = 0;
    int gemsCollected = 0;
    int gemsTotal = 0;
    int doorsOpened = 0;
    int wallsBroken = 0;
    int teleportsUsed = 0;
    int powerUpsCollected = 0;
    int minotaurCount = 0;
    bool fogOfWar = false;
    int parSeconds = 0;
    int stepPar = 0;     // most steps allowed for 3 stars (0 = no limit)
    int difficulty = 1;
    int score = 0;
};

// Par time: how long a good (not perfect) player needs. It grows with the
// length of the shortest route, the maze size and the number of gems, so
// every maze gets a fair target. Rounded up to 5 seconds.
int computeParSeconds(const GameState& state); // already scaled by the difficulty
// Most steps a 3-star run may take: the gem tour times the difficulty's
// allowance. 0 on Easy (no step limit).
int computeStepPar(const GameState& state);
RunResult summarizeRun(const GameState& state);
// 0 = not finished, 1 = finished, 2 = within par time,
// 3 = within par time, no hints, no lives lost, all gems collected and
//     no more steps than the step par.
int computeStars(const RunResult& result);

// ---- Challenges ----
enum class ChallengeId
{
    Daily,
    TimeAttack,
    Blackout,
    MinotaurHunt,
    IronRunner,
    GemRush
};

const int CHALLENGE_COUNT = 6;

struct ChallengeInfo
{
    const char* name;
    const char* description;
    ThemeId theme;
};

const ChallengeInfo& getChallengeInfo(ChallengeId challenge);
// Builds the run for a challenge. Daily uses the date so everyone gets the
// same maze on the same day; the other challenges use the given seed.
RunConfig makeChallengeConfig(ChallengeId challenge, unsigned int seed);
unsigned int makeDailySeed(int year, int month, int day);
int makeDateNumber(int year, int month, int day); // e.g. 20261106

#endif // CAMPAIGN_H
