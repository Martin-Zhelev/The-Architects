// campaign.cpp
// Campaign levels, challenge modes and star ratings. Pure logic.

#include "campaign.h"

namespace
{

// Small helper so the level table below stays readable.
RunConfig makeConfig(int size, unsigned int seed, int lives, int minotaurs, float minotaurStep, bool fog,
                     bool powerUps, bool traps, int teleporters, int gems, bool requireGems, int timeLimit,
                     bool hints)
{
    RunConfig config;
    config.size = size;
    config.seed = seed;
    config.lives = lives;
    config.minotaurCount = minotaurs;
    config.minotaurStepSeconds = minotaurStep;
    config.fogOfWar = fog;
    config.powerUps = powerUps;
    config.traps = traps;
    config.teleporterPairs = teleporters;
    config.gemCount = gems;
    config.requireAllGems = requireGems;
    config.timeLimitSeconds = timeLimit;
    config.hintsAllowed = hints;
    return config;
}

//                         size seed lives mino step  fog    power  traps  tele gems reqGem limit hints
const LevelDef LEVELS[LEVEL_COUNT] = {
    // World 1 - Neon Grid
    {"Boot Sequence", "Your first maze. Find the exit in the far corner.", ThemeId::Neon,
     makeConfig(10, 1101, 3, 0, 0.6f, false, false, false, 0, 3, false, 0, true)},
    {"Key Protocol", "Keys open the glowing doors - each one a shortcut.", ThemeId::Neon,
     makeConfig(14, 1102, 3, 0, 0.6f, false, true, false, 0, 6, false, 0, true)},
    {"Glitch Hunter", "Something is moving in the grid. Don't let it touch you.", ThemeId::Neon,
     makeConfig(16, 1103, 3, 1, 0.75f, false, true, false, 0, 6, false, 0, true)},

    // World 2 - The Dungeon
    {"Torchlight", "Darkness everywhere. Torches let you see further.", ThemeId::Dungeon,
     makeConfig(18, 2201, 3, 0, 0.6f, true, true, false, 0, 8, false, 0, true)},
    {"The Pit", "Spike traps rise and fall. Time your steps.", ThemeId::Dungeon,
     makeConfig(20, 2202, 3, 0, 0.6f, false, true, true, 0, 8, false, 0, true)},
    {"Bull's Lair", "The Minotaur lives here. Hourglasses freeze him.", ThemeId::Dungeon,
     makeConfig(22, 2203, 3, 1, 0.6f, false, true, true, 0, 8, false, 0, true)},

    // World 3 - Lava Core
    {"Ember Run", "The core is melting. Escape before time runs out.", ThemeId::Lava,
     makeConfig(24, 3301, 3, 0, 0.6f, false, true, false, 1, 8, false, 170, true)},
    {"Magma Gates", "Teleporters and traps everywhere. Mind your step.", ThemeId::Lava,
     makeConfig(26, 3302, 3, 0, 0.6f, false, true, true, 3, 10, false, 0, true)},
    {"Molten Fury", "Two Minotaurs patrol the magma halls.", ThemeId::Lava,
     makeConfig(28, 3303, 3, 2, 0.55f, false, true, true, 1, 10, false, 0, true)},

    // World 4 - Frozen Depths
    {"Whiteout", "A blizzard hides the maze. Trust your torch.", ThemeId::Ice,
     makeConfig(30, 4401, 3, 0, 0.6f, true, true, false, 1, 10, false, 0, true)},
    {"Crystal Rush", "The exit is sealed until all six crystals are found.", ThemeId::Ice,
     makeConfig(32, 4402, 3, 0, 0.6f, false, true, false, 2, 6, true, 0, true)},
    {"Frostbite", "One life. No hints. Traps everywhere.", ThemeId::Ice,
     makeConfig(35, 4403, 1, 0, 0.6f, false, true, true, 2, 12, false, 0, false)},

    // World 5 - Candy Kingdom
    {"Sugar Rush", "Grab the sweets and run - the clock is ticking.", ThemeId::Candy,
     makeConfig(38, 5501, 3, 1, 0.55f, false, true, false, 2, 14, false, 400, true)},
    {"Gumdrop Gauntlet", "Everything at once. Good luck.", ThemeId::Candy,
     makeConfig(42, 5502, 3, 2, 0.5f, false, true, true, 3, 16, false, 0, true)},
    {"The Minotaur's Feast", "50x50. Three Minotaurs. Total darkness. The final maze.", ThemeId::Candy,
     makeConfig(50, 5503, 5, 3, 0.5f, true, true, true, 3, 20, false, 0, true)},
};

const char* const WORLD_NAMES[WORLD_COUNT] = {"Neon Grid", "The Dungeon", "Lava Core", "Frozen Depths",
                                              "Candy Kingdom"};

const char* const THEME_NAMES[THEME_COUNT] = {"Neon", "Dungeon", "Lava", "Ice", "Candy"};

const ChallengeInfo CHALLENGES[CHALLENGE_COUNT] = {
    {"Daily Maze", "One maze per day - the same for everyone. 25x25 with gems and a Minotaur.",
     ThemeId::Neon},
    {"Time Attack", "20x20 against a 90 second clock. Every second left is worth points.", ThemeId::Lava},
    {"Blackout", "Thick fog of war. You only see what your torch shows you.", ThemeId::Dungeon},
    {"Minotaur Hunt", "A gauntlet: three Minotaurs are let loose behind you, one after another. Run!",
     ThemeId::Dungeon},
    {"Iron Runner", "One life, no hints, spike traps everywhere.", ThemeId::Ice},
    {"Gem Rush", "The exit stays sealed until you collect all 20 gems.", ThemeId::Candy},
};

} // namespace

const char* getThemeName(ThemeId theme)
{
    return THEME_NAMES[static_cast<int>(theme)];
}

const LevelDef& getLevel(int index)
{
    if (index < 0)
    {
        index = 0;
    }
    if (index >= LEVEL_COUNT)
    {
        index = LEVEL_COUNT - 1;
    }
    return LEVELS[index];
}

const char* getWorldName(int world)
{
    if (world < 0 || world >= WORLD_COUNT)
    {
        return "";
    }
    return WORLD_NAMES[world];
}

RunResult summarizeRun(const GameState& state)
{
    RunResult result;
    result.won = state.won;
    result.size = state.maze.size;
    result.moves = state.moves;
    result.optimalMoves = state.optimalMoves;
    result.seconds = static_cast<int>(state.elapsed);
    result.hintsUsed = state.hintsUsed;
    result.livesLost = state.damageTaken;
    result.gemsCollected = state.gemsCollected;
    result.gemsTotal = state.gemsTotal;
    result.doorsOpened = state.doorsOpened;
    result.wallsBroken = state.wallsBroken;
    result.teleportsUsed = state.teleportsUsed;
    result.powerUpsCollected = state.powerUpsCollected;
    result.minotaurCount = state.minotaurCount;
    result.fogOfWar = state.config.fogOfWar;
    result.parSeconds = computeParSeconds(state);
    result.stepPar = computeStepPar(state);
    result.difficulty = state.config.difficulty;
    result.score = calculateScore(state, static_cast<long long>(state.elapsed));
    return result;
}

int computeParSeconds(const GameState& state)
{
    // The tour walks start -> every gem -> exit, so gem detours are counted
    // from the real maze instead of guessed.
    const int tour = state.tourMoves > 0 ? state.tourMoves : state.optimalMoves;
    const double fogFactor = state.config.fogOfWar ? 1.3 : 1.0;
    const double seconds = (tour * 0.45 + state.maze.size * 2.0) * fogFactor *
                           static_cast<double>(getParTimeFactor(state.config.difficulty));
    const int rounded = static_cast<int>(seconds);
    return (rounded + 4) / 5 * 5;
}

int computeStepPar(const GameState& state)
{
    const float factor = getStepParFactor(state.config.difficulty);
    if (factor <= 0.0f)
    {
        return 0;
    }
    const int tour = state.tourMoves > 0 ? state.tourMoves : state.optimalMoves;
    return static_cast<int>(static_cast<float>(tour) * factor + 0.999f);
}

int computeStars(const RunResult& result)
{
    if (!result.won)
    {
        return 0;
    }
    if (result.seconds > result.parSeconds)
    {
        return 1;
    }
    const bool withinSteps = result.stepPar <= 0 || result.moves <= result.stepPar;
    const bool flawless = result.hintsUsed == 0 && result.livesLost == 0 &&
                          result.gemsCollected >= result.gemsTotal && withinSteps;
    return flawless ? 3 : 2;
}

const ChallengeInfo& getChallengeInfo(ChallengeId challenge)
{
    return CHALLENGES[static_cast<int>(challenge)];
}

RunConfig makeChallengeConfig(ChallengeId challenge, unsigned int seed)
{
    switch (challenge)
    {
    case ChallengeId::Daily:
        return makeConfig(25, seed, 3, 1, 0.6f, false, true, true, 1, 12, false, 0, true);
    case ChallengeId::TimeAttack:
        return makeConfig(20, seed, 3, 0, 0.6f, false, true, false, 1, 6, false, 90, true);
    case ChallengeId::Blackout:
        return makeConfig(25, seed, 3, 0, 0.6f, true, true, false, 0, 10, false, 0, true);
    case ChallengeId::MinotaurHunt:
    {
        RunConfig config = makeConfig(25, seed, 3, 3, 0.42f, false, true, false, 0, 8, false, 0, true);
        config.minotaurChase = true;
        return config;
    }
    case ChallengeId::IronRunner:
        return makeConfig(25, seed, 1, 0, 0.6f, false, true, true, 1, 8, false, 0, false);
    case ChallengeId::GemRush:
        return makeConfig(25, seed, 3, 1, 0.65f, false, true, false, 2, 20, true, 0, true);
    }
    return RunConfig();
}

int makeDateNumber(int year, int month, int day)
{
    return year * 10000 + month * 100 + day;
}

unsigned int makeDailySeed(int year, int month, int day)
{
    // Mix the date so neighbouring days give very different mazes.
    unsigned int seed = static_cast<unsigned int>(makeDateNumber(year, month, day));
    seed ^= seed >> 16;
    seed *= 0x7feb352dU;
    seed ^= seed >> 15;
    seed *= 0x846ca68bU;
    seed ^= seed >> 16;
    return seed;
}
