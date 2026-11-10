// number_trail.cpp
// "Number Trail" mode. Pure logic: no console input or output.

#include "number_trail.h"

#include "maze.h" // isValidMazeSize, flag grid helpers

#include <random>
#include <utility>

namespace
{

const int ALL_STEP_COUNT = 3;
const TrailStep ALL_STEPS[ALL_STEP_COUNT] = {TrailStep::Right, TrailStep::Down, TrailStep::Diagonal};

int valueAfterStep(const NumberTrail& trail, TrailStep step)
{
    int rowOffset = 0;
    int colOffset = 0;
    getTrailStepOffset(step, rowOffset, colOffset);
    return trail.values[trail.row + rowOffset][trail.col + colOffset];
}

} // namespace

bool createNumberTrail(NumberTrail& trail, int size, unsigned int seed)
{
    destroyNumberTrail(trail);
    if (!isValidMazeSize(size))
    {
        return false;
    }

    // Pick size*size different numbers out of 1 .. 2*size*size
    // with a partial Fisher-Yates shuffle.
    const int cellTotal = size * size;
    const int poolSize = cellTotal * 2;
    int* pool = new int[poolSize];
    for (int i = 0; i < poolSize; ++i)
    {
        pool[i] = i + 1;
    }

    std::mt19937 rng(seed);
    for (int i = 0; i < cellTotal; ++i)
    {
        std::uniform_int_distribution<int> pickIndex(i, poolSize - 1);
        std::swap(pool[i], pool[pickIndex(rng)]);
    }

    trail.size = size;
    trail.values = new int*[size];
    for (int row = 0; row < size; ++row)
    {
        trail.values[row] = new int[size];
        for (int col = 0; col < size; ++col)
        {
            trail.values[row][col] = pool[row * size + col];
        }
    }
    trail.values[0][0] = 0; // the start square, as in the assignment example
    delete[] pool;

    trail.maxValue = 0;
    for (int row = 0; row < size; ++row)
    {
        for (int col = 0; col < size; ++col)
        {
            if (trail.values[row][col] > trail.maxValue)
            {
                trail.maxValue = trail.values[row][col];
            }
        }
    }

    trail.visited = createFlagGrid(size, size);
    trail.visited[0][0] = true;
    return true;
}

void destroyNumberTrail(NumberTrail& trail)
{
    if (trail.values != nullptr)
    {
        for (int row = 0; row < trail.size; ++row)
        {
            delete[] trail.values[row];
        }
        delete[] trail.values;
    }
    destroyFlagGrid(trail.visited, trail.size);
    trail = NumberTrail();
}

void getTrailStepOffset(TrailStep step, int& rowOffset, int& colOffset)
{
    rowOffset = (step == TrailStep::Down || step == TrailStep::Diagonal) ? 1 : 0;
    colOffset = (step == TrailStep::Right || step == TrailStep::Diagonal) ? 1 : 0;
}

bool isTrailStepPossible(const NumberTrail& trail, TrailStep step)
{
    int rowOffset = 0;
    int colOffset = 0;
    getTrailStepOffset(step, rowOffset, colOffset);
    return trail.row + rowOffset < trail.size && trail.col + colOffset < trail.size;
}

bool isReachableFromPlayer(const NumberTrail& trail, int row, int col)
{
    for (const TrailStep step : ALL_STEPS)
    {
        int rowOffset = 0;
        int colOffset = 0;
        getTrailStepOffset(step, rowOffset, colOffset);
        if (isTrailStepPossible(trail, step) && trail.row + rowOffset == row && trail.col + colOffset == col)
        {
            return true;
        }
    }
    return false;
}

bool findBestTrailStep(const NumberTrail& trail, TrailStep& best)
{
    bool found = false;
    int bestValue = 0;
    for (const TrailStep step : ALL_STEPS)
    {
        if (!isTrailStepPossible(trail, step))
        {
            continue;
        }
        const int value = valueAfterStep(trail, step);
        if (!found || value < bestValue)
        {
            found = true;
            bestValue = value;
            best = step;
        }
    }
    return found;
}

TrailResult makeTrailStep(NumberTrail& trail, TrailStep step)
{
    if (trail.finished)
    {
        return TrailResult::Finished;
    }
    if (trail.lost)
    {
        return TrailResult::GameOver;
    }
    if (!isTrailStepPossible(trail, step))
    {
        return TrailResult::OutOfBounds;
    }

    TrailStep best = TrailStep::Right;
    findBestTrailStep(trail, best);
    if (valueAfterStep(trail, step) != valueAfterStep(trail, best))
    {
        ++trail.mistakes;
        if (trail.mistakes >= TRAIL_MAX_MISTAKES)
        {
            trail.lost = true;
            return TrailResult::GameOver;
        }
        return TrailResult::Wrong;
    }

    int rowOffset = 0;
    int colOffset = 0;
    getTrailStepOffset(step, rowOffset, colOffset);
    trail.row += rowOffset;
    trail.col += colOffset;
    ++trail.moves;
    trail.visited[trail.row][trail.col] = true;

    if (trail.row == trail.size - 1 && trail.col == trail.size - 1)
    {
        trail.finished = true;
        return TrailResult::Finished;
    }
    return TrailResult::Correct;
}

int calculateTrailScore(const NumberTrail& trail, long long elapsedSeconds)
{
    if (!trail.finished)
    {
        return 0;
    }
    long long score = static_cast<long long>(trail.size) * 50;
    score -= static_cast<long long>(trail.mistakes) * 100;
    score -= elapsedSeconds;
    score += trail.bonusPoints;
    return score < 0 ? 0 : static_cast<int>(score);
}

void registerTrailCombo(NumberTrail& trail, TrailResult result, float secondsSinceLastStep)
{
    if (result == TrailResult::Wrong || result == TrailResult::GameOver)
    {
        trail.combo = 0;
        return;
    }
    if (result != TrailResult::Correct && result != TrailResult::Finished)
    {
        return; // bumping into the edge changes nothing
    }
    trail.combo = (trail.combo > 0 && secondsSinceLastStep <= TRAIL_COMBO_WINDOW) ? trail.combo + 1 : 1;
    if (trail.combo > trail.bestCombo)
    {
        trail.bestCombo = trail.combo;
    }
    if (trail.combo > 1)
    {
        trail.bonusPoints += 5 * trail.combo;
    }
}

int countDigits(int value)
{
    if (value < 0)
    {
        value = -value;
    }
    int digits = 1;
    while (value >= 10)
    {
        value /= 10;
        ++digits;
    }
    return digits;
}
