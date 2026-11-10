// number_trail.h
// "Number Trail" mode - the example from the assignment.
//
// Every square holds a number. Starting at (0, 0) the player may move right,
// down or diagonally down-right, and must always step onto the SMALLEST of
// the numbers they can reach. Following the smallest numbers leads out of the
// grid at the bottom-right corner.
//
// Pure logic: no console input or output.

#ifndef NUMBER_TRAIL_H
#define NUMBER_TRAIL_H

const int TRAIL_MAX_MISTAKES = 3;

enum class TrailStep
{
    Right,
    Down,
    Diagonal
};

enum class TrailResult
{
    Correct,
    Wrong,
    OutOfBounds,
    Finished,
    GameOver
};

struct NumberTrail
{
    int size = 0;
    int** values = nullptr;   // size x size, all different
    bool** visited = nullptr; // size x size
    int row = 0;
    int col = 0;
    int moves = 0;
    int mistakes = 0;
    int maxValue = 0;
    bool finished = false;
    bool lost = false;
    int combo = 0;       // correct steps in a row, each within the combo window
    int bestCombo = 0;
    int bonusPoints = 0; // earned through combos
};

const float TRAIL_COMBO_WINDOW = 2.0f; // seconds allowed between steps to keep a combo

bool createNumberTrail(NumberTrail& trail, int size, unsigned int seed);
void destroyNumberTrail(NumberTrail& trail);

void getTrailStepOffset(TrailStep step, int& rowOffset, int& colOffset);
bool isTrailStepPossible(const NumberTrail& trail, TrailStep step);
bool isReachableFromPlayer(const NumberTrail& trail, int row, int col);
bool findBestTrailStep(const NumberTrail& trail, TrailStep& best);
TrailResult makeTrailStep(NumberTrail& trail, TrailStep step);
int calculateTrailScore(const NumberTrail& trail, long long elapsedSeconds);
// Updates the combo after a step. Quick correct steps build a combo worth
// 5 x combo bonus points each; a mistake resets it.
void registerTrailCombo(NumberTrail& trail, TrailResult result, float secondsSinceLastStep);
int countDigits(int value);

#endif // NUMBER_TRAIL_H
