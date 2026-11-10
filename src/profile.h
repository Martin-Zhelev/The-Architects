// profile.h
// Player profiles: statistics, achievements, campaign progress, settings and
// the shared leaderboards - plus saving all of it to text files.
//
// Pure logic: files are read and written with <fstream>, but nothing here
// uses cin / cout or draws anything.

#ifndef PROFILE_H
#define PROFILE_H

#include "campaign.h"
#include "game.h"

#include <string>

// ---- Statistics ----
struct PlayerStats
{
    int mazesCompleted = 0;
    int mazesLost = 0;
    int totalMoves = 0;
    int totalSeconds = 0;
    int doorsOpened = 0;
    int gemsCollected = 0;
    int wallsBroken = 0;
    int teleportsUsed = 0;
    int powerUpsCollected = 0;
    int perfectRuns = 0;        // finished in exactly the corridor route
    int biggestMaze = 0;
    int fastestBigMaze = 0;     // seconds for a 20x20 or bigger maze, 0 = none yet
    int minotaurEscapes = 0;    // wins with a Minotaur and no life lost
    int hintFreeBigWins = 0;    // 30x30+ wins without a hint
    int dailyCompleted = 0;
    int timeAttackWins = 0;
    int blackoutWins = 0;
    int ironRunnerWins = 0;
    int trailsCompleted = 0;
    int trailsPerfect = 0;      // no mistakes
    int biggestTrail = 0;
    int bestTrailCombo = 0;
    int themesPlayedMask = 0;   // bit per ThemeId
};

// Which mode a run belongs to (for statistics and leaderboards).
enum class RunMode
{
    Campaign,
    Challenge,
    FreePlay,
    Custom // a map from the map maker
};

void recordRun(PlayerStats& stats, const RunResult& result, RunMode mode, int challenge, ThemeId theme);
void recordTrail(PlayerStats& stats, bool finished, int mistakes, int size, int bestCombo);

// ---- Achievements ----
const int ACHIEVEMENT_COUNT = 20;

struct AchievementInfo
{
    const char* name;
    const char* description;
};

const AchievementInfo& getAchievementInfo(int index);
// Fills current/target with the progress towards an achievement.
void getAchievementProgress(int index, const PlayerStats& stats, int totalStars, int& current, int& target);
bool isAchievementEarned(int index, const PlayerStats& stats, int totalStars);

// ---- Settings ----
struct AppSettings
{
    int theme = 0;          // ThemeId used in free play
    int masterVolume = 80;  // 0 - 100
    int musicVolume = 60;
    int sfxVolume = 80;
    bool fullscreen = false;
    bool bloom = true;
    bool crtEffect = true;  // scanlines + chromatic aberration
    bool reduceMotion = false; // no shake, no flashes, no head bob
    bool showFps = false;
    bool muteInBackground = true; // silence while another window is in front
    int fieldOfView = 75;   // 60 - 100
    int startView = 0;      // 0 third person, 1 first person, 2 top-down
    int difficulty = 1;     // 0 Easy, 1 Normal, 2 Hard, 3 Expert
};

// ---- Profile ----
struct Profile
{
    std::string name;
    PlayerStats stats;
    bool achievements[ACHIEVEMENT_COUNT] = {};
    int levelStars[LEVEL_COUNT] = {};
    int levelBestScore[LEVEL_COUNT] = {};
    int challengeBest[CHALLENGE_COUNT] = {};
    int dailyBestDate = 0;
    int dailyBestScore = 0;
    bool admin = false;     // everything unlocked with the admin code
    bool mapTutorialDone = false; // the map maker tutorial was finished or skipped
    AppSettings settings;
};

int countTotalStars(const Profile& profile);
int countUnlockedLevels(const Profile& profile); // level i is open if level i-1 has a star
bool isLevelUnlocked(const Profile& profile, int level);
// Marks newly earned achievements. Writes their indexes to newlyEarned
// (must hold ACHIEVEMENT_COUNT ints) and returns how many there are.
int updateAchievements(Profile& profile, int* newlyEarned);

// ---- Admin ----
// The code typed in Settings that unlocks everything (case does not matter).
bool isAdminCode(const std::string& code);
// Every level with 3 stars, every achievement, admin tools on.
void unlockEverything(Profile& profile);

std::string serializeProfile(const Profile& profile);
bool deserializeProfile(const std::string& text, Profile& profile);

// ---- Leaderboards shared by all profiles ----
enum class BoardId
{
    FreePlay,
    Daily,
    TimeAttack,
    Blackout,
    MinotaurHunt,
    IronRunner,
    GemRush,
    NumberTrail
};

const int BOARD_COUNT = 8;
const char* getBoardName(BoardId board);
BoardId boardForChallenge(ChallengeId challenge);

struct LeaderboardSet
{
    Leaderboard boards[BOARD_COUNT];
};

std::string serializeLeaderboards(const LeaderboardSet& set);
bool deserializeLeaderboards(const std::string& text, LeaderboardSet& set);

// ---- Files ----
bool writeTextFile(const std::string& path, const std::string& text);
bool readTextFile(const std::string& path, std::string& text);
std::string profileFileName(const std::string& name); // "Ana QA" -> "Ana_QA.profile"

#endif // PROFILE_H
