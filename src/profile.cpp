// profile.cpp
// Player profiles, achievements, settings and leaderboards, with saving to
// and loading from simple text files. Pure logic: no console or drawing.

#include "profile.h"

#include "validation.h"

#include <fstream>
#include <sstream>

namespace
{

const char* const PROFILE_HEADER = "LABYRINTH_PROFILE 1";
const char* const BOARDS_HEADER = "LABYRINTH_BOARDS 1";

const AchievementInfo ACHIEVEMENTS[ACHIEVEMENT_COUNT] = {
    {"First Steps", "Escape your first labyrinth."},
    {"Maze Runner", "Escape 25 labyrinths."},
    {"Perfect Path", "Finish a maze without a single wasted step."},
    {"Speed Demon", "Finish a 20x20 or bigger maze in under 60 seconds."},
    {"Locksmith", "Open 15 special doors."},
    {"Treasure Hunter", "Collect 100 gems."},
    {"Demolition Crew", "Smash 10 walls with the hammer."},
    {"Frequent Flyer", "Travel through 20 teleporters."},
    {"Power Player", "Collect 30 power-ups."},
    {"Untouchable", "Escape a Minotaur maze without losing a life."},
    {"Giant Slayer", "Escape a 50x50 labyrinth."},
    {"Clear Mind", "Finish a 30x30 or bigger maze without hints."},
    {"Daily Devotee", "Complete 3 Daily Mazes."},
    {"Beat the Clock", "Win the Time Attack challenge."},
    {"Night Vision", "Win the Blackout challenge."},
    {"Iron Will", "Win the Iron Runner challenge."},
    {"Trail Blazer", "Finish a Number Trail without a mistake."},
    {"Combo King", "Reach a 15x combo in Number Trail."},
    {"World Traveller", "Play in all five worlds."},
    {"Star Collector", "Earn every campaign star (45)."},
};

const char* const BOARD_NAMES[BOARD_COUNT] = {"Free Play", "Daily Maze", "Time Attack", "Blackout",
                                              "Minotaur Hunt", "Iron Runner", "Gem Rush", "Number Trail"};

int countBits(int mask)
{
    int count = 0;
    while (mask != 0)
    {
        count += mask & 1;
        mask >>= 1;
    }
    return count;
}

int atLeast(int value, int minimum)
{
    return value < minimum ? minimum : value;
}

// A named integer inside a profile, so saving and loading share one table.
struct IntField
{
    const char* key;
    int* value;
};

// Booleans are saved as 0 / 1 through a temporary int.
struct BoolField
{
    const char* key;
    bool* value;
};

const int INT_FIELD_COUNT = 32;
const int BOOL_FIELD_COUNT = 8;

void buildFields(Profile& profile, IntField* ints, BoolField* bools)
{
    PlayerStats& s = profile.stats;
    AppSettings& a = profile.settings;
    const IntField intTable[INT_FIELD_COUNT] = {
        {"stat.mazesCompleted", &s.mazesCompleted},
        {"stat.mazesLost", &s.mazesLost},
        {"stat.totalMoves", &s.totalMoves},
        {"stat.totalSeconds", &s.totalSeconds},
        {"stat.doorsOpened", &s.doorsOpened},
        {"stat.gemsCollected", &s.gemsCollected},
        {"stat.wallsBroken", &s.wallsBroken},
        {"stat.teleportsUsed", &s.teleportsUsed},
        {"stat.powerUpsCollected", &s.powerUpsCollected},
        {"stat.perfectRuns", &s.perfectRuns},
        {"stat.biggestMaze", &s.biggestMaze},
        {"stat.fastestBigMaze", &s.fastestBigMaze},
        {"stat.minotaurEscapes", &s.minotaurEscapes},
        {"stat.hintFreeBigWins", &s.hintFreeBigWins},
        {"stat.dailyCompleted", &s.dailyCompleted},
        {"stat.timeAttackWins", &s.timeAttackWins},
        {"stat.blackoutWins", &s.blackoutWins},
        {"stat.ironRunnerWins", &s.ironRunnerWins},
        {"stat.trailsCompleted", &s.trailsCompleted},
        {"stat.trailsPerfect", &s.trailsPerfect},
        {"stat.biggestTrail", &s.biggestTrail},
        {"stat.bestTrailCombo", &s.bestTrailCombo},
        {"stat.themesPlayedMask", &s.themesPlayedMask},
        {"dailyBestDate", &profile.dailyBestDate},
        {"dailyBestScore", &profile.dailyBestScore},
        {"setting.theme", &a.theme},
        {"setting.masterVolume", &a.masterVolume},
        {"setting.musicVolume", &a.musicVolume},
        {"setting.sfxVolume", &a.sfxVolume},
        {"setting.fieldOfView", &a.fieldOfView},
        {"setting.startView", &a.startView},
        {"setting.difficulty", &a.difficulty},
    };
    const BoolField boolTable[BOOL_FIELD_COUNT] = {
        {"setting.fullscreen", &a.fullscreen},   {"setting.bloom", &a.bloom},
        {"setting.crtEffect", &a.crtEffect},     {"setting.reduceMotion", &a.reduceMotion},
        {"setting.showFps", &a.showFps},         {"admin", &profile.admin},
        {"setting.muteInBackground", &a.muteInBackground}, {"mapTutorialDone", &profile.mapTutorialDone},
    };
    for (int i = 0; i < INT_FIELD_COUNT; ++i)
    {
        ints[i] = intTable[i];
    }
    for (int i = 0; i < BOOL_FIELD_COUNT; ++i)
    {
        bools[i] = boolTable[i];
    }
}

void writeIntList(std::ostringstream& out, const char* key, const int* values, int count)
{
    out << key << '=';
    for (int i = 0; i < count; ++i)
    {
        out << (i > 0 ? " " : "") << values[i];
    }
    out << '\n';
}

void readIntList(const std::string& text, int* values, int count)
{
    std::istringstream in(text);
    for (int i = 0; i < count; ++i)
    {
        int value = 0;
        if (!(in >> value))
        {
            return;
        }
        values[i] = value;
    }
}

int clampSetting(int value, int low, int high)
{
    return value < low ? low : (value > high ? high : value);
}

} // namespace

// ---------------------------------------------------------------------------
// Statistics
// ---------------------------------------------------------------------------

void recordRun(PlayerStats& stats, const RunResult& result, RunMode mode, int challenge, ThemeId theme)
{
    stats.themesPlayedMask |= 1 << static_cast<int>(theme);
    stats.totalMoves += result.moves;
    stats.totalSeconds += result.seconds;
    stats.doorsOpened += result.doorsOpened;
    stats.gemsCollected += result.gemsCollected;
    stats.wallsBroken += result.wallsBroken;
    stats.teleportsUsed += result.teleportsUsed;
    stats.powerUpsCollected += result.powerUpsCollected;

    if (!result.won)
    {
        ++stats.mazesLost;
        return;
    }

    ++stats.mazesCompleted;
    if (result.moves <= result.optimalMoves)
    {
        ++stats.perfectRuns;
    }
    if (result.size > stats.biggestMaze)
    {
        stats.biggestMaze = result.size;
    }
    if (result.size >= 20 && (stats.fastestBigMaze == 0 || result.seconds < stats.fastestBigMaze))
    {
        stats.fastestBigMaze = atLeast(result.seconds, 1);
    }
    if (result.minotaurCount > 0 && result.livesLost == 0)
    {
        ++stats.minotaurEscapes;
    }
    if (result.size >= 30 && result.hintsUsed == 0)
    {
        ++stats.hintFreeBigWins;
    }

    if (mode == RunMode::Challenge)
    {
        switch (static_cast<ChallengeId>(challenge))
        {
        case ChallengeId::Daily:
            ++stats.dailyCompleted;
            break;
        case ChallengeId::TimeAttack:
            ++stats.timeAttackWins;
            break;
        case ChallengeId::Blackout:
            ++stats.blackoutWins;
            break;
        case ChallengeId::IronRunner:
            ++stats.ironRunnerWins;
            break;
        default:
            break;
        }
    }
}

void recordTrail(PlayerStats& stats, bool finished, int mistakes, int size, int bestCombo)
{
    if (bestCombo > stats.bestTrailCombo)
    {
        stats.bestTrailCombo = bestCombo;
    }
    if (!finished)
    {
        return;
    }
    ++stats.trailsCompleted;
    if (mistakes == 0)
    {
        ++stats.trailsPerfect;
    }
    if (size > stats.biggestTrail)
    {
        stats.biggestTrail = size;
    }
}

// ---------------------------------------------------------------------------
// Achievements
// ---------------------------------------------------------------------------

const AchievementInfo& getAchievementInfo(int index)
{
    if (index < 0 || index >= ACHIEVEMENT_COUNT)
    {
        index = 0;
    }
    return ACHIEVEMENTS[index];
}

void getAchievementProgress(int index, const PlayerStats& stats, int totalStars, int& current, int& target)
{
    target = 1;
    switch (index)
    {
    case 0:
        current = stats.mazesCompleted;
        break;
    case 1:
        current = stats.mazesCompleted;
        target = 25;
        break;
    case 2:
        current = stats.perfectRuns;
        break;
    case 3:
        current = (stats.fastestBigMaze > 0 && stats.fastestBigMaze <= 60) ? 1 : 0;
        break;
    case 4:
        current = stats.doorsOpened;
        target = 15;
        break;
    case 5:
        current = stats.gemsCollected;
        target = 100;
        break;
    case 6:
        current = stats.wallsBroken;
        target = 10;
        break;
    case 7:
        current = stats.teleportsUsed;
        target = 20;
        break;
    case 8:
        current = stats.powerUpsCollected;
        target = 30;
        break;
    case 9:
        current = stats.minotaurEscapes;
        break;
    case 10:
        current = stats.biggestMaze >= 50 ? 1 : 0;
        break;
    case 11:
        current = stats.hintFreeBigWins;
        break;
    case 12:
        current = stats.dailyCompleted;
        target = 3;
        break;
    case 13:
        current = stats.timeAttackWins;
        break;
    case 14:
        current = stats.blackoutWins;
        break;
    case 15:
        current = stats.ironRunnerWins;
        break;
    case 16:
        current = stats.trailsPerfect;
        break;
    case 17:
        current = stats.bestTrailCombo;
        target = 15;
        break;
    case 18:
        current = countBits(stats.themesPlayedMask);
        target = THEME_COUNT;
        break;
    case 19:
        current = totalStars;
        target = LEVEL_COUNT * 3;
        break;
    default:
        current = 0;
        break;
    }
    if (current > target)
    {
        current = target;
    }
}

bool isAchievementEarned(int index, const PlayerStats& stats, int totalStars)
{
    int current = 0;
    int target = 1;
    getAchievementProgress(index, stats, totalStars, current, target);
    return current >= target;
}

// ---------------------------------------------------------------------------
// Profile
// ---------------------------------------------------------------------------

int countTotalStars(const Profile& profile)
{
    int total = 0;
    for (int i = 0; i < LEVEL_COUNT; ++i)
    {
        total += profile.levelStars[i];
    }
    return total;
}

bool isLevelUnlocked(const Profile& profile, int level)
{
    if (level <= 0)
    {
        return true;
    }
    if (level >= LEVEL_COUNT)
    {
        return false;
    }
    return profile.levelStars[level - 1] > 0;
}

int countUnlockedLevels(const Profile& profile)
{
    int count = 0;
    while (count < LEVEL_COUNT && isLevelUnlocked(profile, count))
    {
        ++count;
    }
    return count;
}

int updateAchievements(Profile& profile, int* newlyEarned)
{
    const int stars = countTotalStars(profile);
    int count = 0;
    for (int i = 0; i < ACHIEVEMENT_COUNT; ++i)
    {
        if (!profile.achievements[i] && isAchievementEarned(i, profile.stats, stars))
        {
            profile.achievements[i] = true;
            newlyEarned[count++] = i;
        }
    }
    return count;
}

bool isAdminCode(const std::string& code)
{
    const char* const ADMIN_CODE = "MINOTAUR";
    std::string upper;
    for (const char symbol : code)
    {
        if (symbol != ' ')
        {
            upper += (symbol >= 'a' && symbol <= 'z') ? static_cast<char>(symbol - 'a' + 'A') : symbol;
        }
    }
    return upper == ADMIN_CODE;
}

void unlockEverything(Profile& profile)
{
    profile.admin = true;
    for (int i = 0; i < LEVEL_COUNT; ++i)
    {
        profile.levelStars[i] = 3;
    }
    for (int i = 0; i < ACHIEVEMENT_COUNT; ++i)
    {
        profile.achievements[i] = true;
    }
    // Every world counts as visited for the theme achievement.
    profile.stats.themesPlayedMask = (1 << THEME_COUNT) - 1;
}

std::string serializeProfile(const Profile& profile)
{
    Profile copy = profile;
    IntField ints[INT_FIELD_COUNT];
    BoolField bools[BOOL_FIELD_COUNT];
    buildFields(copy, ints, bools);

    std::ostringstream out;
    out << PROFILE_HEADER << '\n';
    out << "name=" << profile.name << '\n';
    for (const IntField& field : ints)
    {
        out << field.key << '=' << *field.value << '\n';
    }
    for (const BoolField& field : bools)
    {
        out << field.key << '=' << (*field.value ? 1 : 0) << '\n';
    }
    out << "achievements=";
    for (int i = 0; i < ACHIEVEMENT_COUNT; ++i)
    {
        out << (profile.achievements[i] ? '1' : '0');
    }
    out << '\n';
    writeIntList(out, "levelStars", profile.levelStars, LEVEL_COUNT);
    writeIntList(out, "levelBestScore", profile.levelBestScore, LEVEL_COUNT);
    writeIntList(out, "challengeBest", profile.challengeBest, CHALLENGE_COUNT);
    return out.str();
}

bool deserializeProfile(const std::string& text, Profile& profile)
{
    std::istringstream in(text);
    std::string line;
    if (!std::getline(in, line) || trim(line) != PROFILE_HEADER)
    {
        return false;
    }

    Profile loaded;
    IntField ints[INT_FIELD_COUNT];
    BoolField bools[BOOL_FIELD_COUNT];
    buildFields(loaded, ints, bools);

    while (std::getline(in, line))
    {
        const std::size_t equals = line.find('=');
        if (equals == std::string::npos)
        {
            continue;
        }
        const std::string key = trim(line.substr(0, equals));
        const std::string value = trim(line.substr(equals + 1));

        if (key == "name")
        {
            loaded.name = value;
        }
        else if (key == "achievements")
        {
            for (int i = 0; i < ACHIEVEMENT_COUNT && i < static_cast<int>(value.size()); ++i)
            {
                loaded.achievements[i] = value[static_cast<std::size_t>(i)] == '1';
            }
        }
        else if (key == "levelStars")
        {
            readIntList(value, loaded.levelStars, LEVEL_COUNT);
        }
        else if (key == "levelBestScore")
        {
            readIntList(value, loaded.levelBestScore, LEVEL_COUNT);
        }
        else if (key == "challengeBest")
        {
            readIntList(value, loaded.challengeBest, CHALLENGE_COUNT);
        }
        else
        {
            int number = 0;
            if (!parseInt(value, number))
            {
                continue; // unknown or broken line: skip it, keep the default
            }
            for (const IntField& field : ints)
            {
                if (key == field.key)
                {
                    *field.value = number;
                }
            }
            for (const BoolField& field : bools)
            {
                if (key == field.key)
                {
                    *field.value = number != 0;
                }
            }
        }
    }

    if (!isValidPlayerName(loaded.name))
    {
        return false;
    }

    // Never trust numbers from a file blindly.
    AppSettings& a = loaded.settings;
    a.theme = clampSetting(a.theme, 0, THEME_COUNT - 1);
    a.masterVolume = clampSetting(a.masterVolume, 0, 100);
    a.musicVolume = clampSetting(a.musicVolume, 0, 100);
    a.sfxVolume = clampSetting(a.sfxVolume, 0, 100);
    a.fieldOfView = clampSetting(a.fieldOfView, 60, 100);
    a.startView = clampSetting(a.startView, 0, 2);
    a.difficulty = clampSetting(a.difficulty, 0, DIFFICULTY_COUNT - 1);
    for (int i = 0; i < LEVEL_COUNT; ++i)
    {
        loaded.levelStars[i] = clampSetting(loaded.levelStars[i], 0, 3);
    }

    profile = loaded;
    return true;
}

// ---------------------------------------------------------------------------
// Leaderboards
// ---------------------------------------------------------------------------

const char* getBoardName(BoardId board)
{
    return BOARD_NAMES[static_cast<int>(board)];
}

BoardId boardForChallenge(ChallengeId challenge)
{
    switch (challenge)
    {
    case ChallengeId::Daily:
        return BoardId::Daily;
    case ChallengeId::TimeAttack:
        return BoardId::TimeAttack;
    case ChallengeId::Blackout:
        return BoardId::Blackout;
    case ChallengeId::MinotaurHunt:
        return BoardId::MinotaurHunt;
    case ChallengeId::IronRunner:
        return BoardId::IronRunner;
    case ChallengeId::GemRush:
        return BoardId::GemRush;
    }
    return BoardId::FreePlay;
}

// Each entry is one line:  e <board> <score> <size> <moves> <seconds> <name>
std::string serializeLeaderboards(const LeaderboardSet& set)
{
    std::ostringstream out;
    out << BOARDS_HEADER << '\n';
    for (int board = 0; board < BOARD_COUNT; ++board)
    {
        for (int i = 0; i < set.boards[board].count; ++i)
        {
            const ScoreEntry& entry = set.boards[board].entries[i];
            out << "e " << board << ' ' << entry.score << ' ' << entry.mazeSize << ' ' << entry.moves << ' '
                << entry.seconds << ' ' << entry.name << '\n';
        }
    }
    return out.str();
}

bool deserializeLeaderboards(const std::string& text, LeaderboardSet& set)
{
    std::istringstream in(text);
    std::string line;
    if (!std::getline(in, line) || trim(line) != BOARDS_HEADER)
    {
        return false;
    }

    LeaderboardSet loaded;
    while (std::getline(in, line))
    {
        std::istringstream fields(line);
        std::string tag;
        int board = 0;
        ScoreEntry entry;
        if (!(fields >> tag >> board >> entry.score >> entry.mazeSize >> entry.moves >> entry.seconds) ||
            tag != "e" || board < 0 || board >= BOARD_COUNT)
        {
            continue;
        }
        std::string name;
        std::getline(fields, name);
        entry.name = trim(name);
        if (isValidPlayerName(entry.name))
        {
            addToLeaderboard(loaded.boards[board], entry);
        }
    }
    set = loaded;
    return true;
}

// ---------------------------------------------------------------------------
// Files
// ---------------------------------------------------------------------------

bool writeTextFile(const std::string& path, const std::string& text)
{
    std::ofstream file(path, std::ios::binary | std::ios::trunc);
    if (!file)
    {
        return false;
    }
    file << text;
    return static_cast<bool>(file);
}

bool readTextFile(const std::string& path, std::string& text)
{
    std::ifstream file(path, std::ios::binary);
    if (!file)
    {
        return false;
    }
    std::ostringstream buffer;
    buffer << file.rdbuf();
    text = buffer.str();
    return true;
}

std::string profileFileName(const std::string& name)
{
    std::string file;
    for (const char symbol : name)
    {
        file += (symbol == ' ') ? '_' : symbol;
    }
    return file + ".profile";
}
