// game.cpp
// Game rules for the Labyrinth mode. Pure logic: no console input or output.

#include "game.h"

#include <chrono>
#include <cmath>
#include <cstdlib>

namespace
{

const float HURT_INVULNERABILITY = 1.5f;
const float CAUGHT_INVULNERABILITY = 3.0f;
const float CAUGHT_MINOTAUR_PAUSE = 2.0f;
const float BOOTS_DURATION = 12.0f;
const float FREEZE_DURATION = 8.0f;
const float COMPASS_DURATION = 25.0f;
const int MAX_TORCH_LEVEL = 3;
const int HAMMER_CHARGES_PER_PICKUP = 1;
const int HAMMER_SCORE_COST = 40;
const float GAUNTLET_FIRST_RELEASE = 4.0f;
const float GAUNTLET_RELEASE_GAP = 9.0f;
const float SPIKE_CYCLE_SECONDS = 2.8f;
const float SPIKE_UP_SECONDS = 1.2f;
const int MINOTAUR_HUNT_RADIUS = 10;
const int SAFE_START_DISTANCE = 3;

const Direction ALL_DIRECTIONS[4] = {Direction::Up, Direction::Down, Direction::Left, Direction::Right};

int cellIndex(const GameState& state, int row, int col)
{
    return row * state.maze.size + col;
}

void pushEvent(GameState& state, EventType type, int row, int col, int value = 0)
{
    if (state.eventCount >= MAX_EVENTS)
    {
        // The list is full. Winning or losing must never be lost, so it
        // replaces the last (least important) event.
        if (type != EventType::Won && type != EventType::Lost)
        {
            return;
        }
        state.eventCount = MAX_EVENTS - 1;
    }
    GameEvent& event = state.events[state.eventCount++];
    event.type = type;
    event.row = row;
    event.col = col;
    event.value = value;
}

int clampInt(int value, int low, int high)
{
    return value < low ? low : (value > high ? high : value);
}

Item** createItemGrid(int size)
{
    Item** grid = new Item*[size];
    for (int row = 0; row < size; ++row)
    {
        grid[row] = new Item[size];
        for (int col = 0; col < size; ++col)
        {
            grid[row][col] = Item::None;
        }
    }
    return grid;
}

void destroyItemGrid(Item**& grid, int size)
{
    if (grid == nullptr)
    {
        return;
    }
    for (int row = 0; row < size; ++row)
    {
        delete[] grid[row];
    }
    delete[] grid;
    grid = nullptr;
}

// A cell where something new may be placed: an empty corridor cell that is
// not the start, not the exit and far enough from the start.
bool isFreeCell(const GameState& state, const int* startDistances, int row, int col, int minDistance)
{
    if ((row == 0 && col == 0) || isExitCell(state.maze, row, col))
    {
        return false;
    }
    if (getCellTile(state.maze, row, col) != Tile::Path || state.items[row][col] != Item::None)
    {
        return false;
    }
    return startDistances[cellIndex(state, row, col)] >= minDistance;
}

bool pickFreeCell(GameState& state, const int* startDistances, int minDistance, int& row, int& col,
                  bool deadEndOnly = false)
{
    std::uniform_int_distribution<int> pick(0, state.maze.size - 1);
    for (int attempt = 0; attempt < 4000; ++attempt)
    {
        row = pick(state.rng);
        col = pick(state.rng);
        if (isFreeCell(state, startDistances, row, col, minDistance) &&
            (!deadEndOnly || isDeadEnd(state.maze, row, col)))
        {
            return true;
        }
    }
    return false;
}

void placeItems(GameState& state, const int* startDistances)
{
    const RunConfig& config = state.config;
    int row = 0;
    int col = 0;

    // Gems
    for (int i = 0; i < config.gemCount; ++i)
    {
        if (pickFreeCell(state, startDistances, 2, row, col))
        {
            state.items[row][col] = Item::Gem;
        }
    }

    // Power-ups: only offer the ones that make sense in this run.
    if (config.powerUps)
    {
        Item pool[8];
        int poolSize = 0;
        pool[poolSize++] = Item::Boots;
        pool[poolSize++] = Item::Hammer;
        pool[poolSize++] = Item::Compass;
        if (config.fogOfWar)
        {
            pool[poolSize++] = Item::Torch;
            pool[poolSize++] = Item::Torch;
        }
        if (config.minotaurCount > 0)
        {
            pool[poolSize++] = Item::Hourglass;
        }
        // One-life runs (Frostbite, Iron Runner) never get extra hearts.
        if ((config.minotaurCount > 0 || config.traps) && config.lives > 1)
        {
            pool[poolSize++] = Item::Heart;
        }

        // Hammers are rare: one per 25 rows, so they can't flatten the maze.
        const int maxHammers = 1 + config.size / 25;
        int hammers = 0;
        const int powerUpCount = 3 + config.size / 6;
        std::uniform_int_distribution<int> pickType(0, poolSize - 1);
        for (int i = 0; i < powerUpCount; ++i)
        {
            Item item = pool[pickType(state.rng)];
            if (item == Item::Hammer && hammers >= maxHammers)
            {
                item = Item::Boots;
            }
            if (pickFreeCell(state, startDistances, 2, row, col))
            {
                state.items[row][col] = item;
                hammers += item == Item::Hammer ? 1 : 0;
            }
        }
    }

    // Spike traps
    if (config.traps)
    {
        const int spikeCount = config.size * config.size / 30;
        for (int i = 0; i < spikeCount; ++i)
        {
            if (pickFreeCell(state, startDistances, SAFE_START_DISTANCE, row, col))
            {
                state.items[row][col] = Item::Spikes;
            }
        }
    }

    // Teleporter pairs: the two pads should be far apart along the corridors.
    // Pads sit in dead ends, so they never block a corridor: stepping off a
    // pad can only lead back into the maze, never into another pad.
    for (int pair = 0; pair < config.teleporterPairs; ++pair)
    {
        int firstRow = 0;
        int firstCol = 0;
        bool deadEnds = true;
        if (!pickFreeCell(state, startDistances, SAFE_START_DISTANCE, firstRow, firstCol, true))
        {
            deadEnds = false;
            if (!pickFreeCell(state, startDistances, SAFE_START_DISTANCE, firstRow, firstCol))
            {
                break;
            }
        }
        const int firstDistance = startDistances[cellIndex(state, firstRow, firstCol)];
        bool placed = false;
        for (int attempt = 0; attempt < 200 && !placed; ++attempt)
        {
            if (!pickFreeCell(state, startDistances, SAFE_START_DISTANCE, row, col, deadEnds))
            {
                break;
            }
            const int gap = std::abs(startDistances[cellIndex(state, row, col)] - firstDistance);
            if ((row != firstRow || col != firstCol) && gap >= config.size)
            {
                state.items[firstRow][firstCol] = Item::Teleporter;
                state.items[row][col] = Item::Teleporter;
                state.teleportLinks[cellIndex(state, firstRow, firstCol)] = cellIndex(state, row, col);
                state.teleportLinks[cellIndex(state, row, col)] = cellIndex(state, firstRow, firstCol);
                placed = true;
            }
        }
    }
}

} // namespace

void placeMinotaur(GameState& state, int index, int row, int col)
{
    Minotaur& minotaur = state.minotaurs[index];
    if (state.config.minotaurChase)
    {
        // Gauntlet: every Minotaur waits at the entrance and is let loose
        // one after another, then chases the player for the rest of the run.
        row = 0;
        col = 0;
        minotaur.released = false;
        minotaur.releaseTime = GAUNTLET_FIRST_RELEASE + GAUNTLET_RELEASE_GAP * static_cast<float>(index);
        minotaur.stepTimer = 0.0f;
    }
    else
    {
        minotaur.released = true;
        minotaur.releaseTime = 0.0f;
        minotaur.stepTimer = getMinotaurStepSeconds(state) * (1.0f + 0.5f * static_cast<float>(index));
    }
    minotaur.row = minotaur.prevRow = minotaur.spawnRow = row;
    minotaur.col = minotaur.prevCol = minotaur.spawnCol = col;
    minotaur.hunting = false;
}

namespace
{

void spawnMinotaurs(GameState& state, const int* startDistances)
{
    state.minotaurCount = state.config.minotaurCount;
    if (state.minotaurCount <= 0)
    {
        return;
    }
    state.minotaurs = new Minotaur[state.minotaurCount];

    int maxDistance = 0;
    for (int i = 0; i < state.maze.size * state.maze.size; ++i)
    {
        if (startDistances[i] > maxDistance)
        {
            maxDistance = startDistances[i];
        }
    }

    std::uniform_int_distribution<int> pick(0, state.maze.size - 1);
    for (int index = 0; index < state.minotaurCount; ++index)
    {
        int row = state.maze.size - 1;
        int col = 0;
        for (int attempt = 0; attempt < 4000; ++attempt)
        {
            const int r = pick(state.rng);
            const int c = pick(state.rng);
            if (startDistances[cellIndex(state, r, c)] >= maxDistance / 2 && !isExitCell(state.maze, r, c))
            {
                row = r;
                col = c;
                break;
            }
        }
        placeMinotaur(state, index, row, col);
    }
}

void loseGame(GameState& state, LossReason reason)
{
    if (state.lost || state.won)
    {
        return;
    }
    state.lost = true;
    state.lossReason = reason;
    state.hintVisible = false;
    pushEvent(state, EventType::Lost, state.playerRow, state.playerCol, static_cast<int>(reason));
}

void hurtPlayer(GameState& state)
{
    if (state.invulnerableTime > 0.0f || state.won || state.lost)
    {
        return;
    }
    --state.lives;
    ++state.damageTaken;
    state.invulnerableTime = HURT_INVULNERABILITY;
    pushEvent(state, EventType::Hurt, state.playerRow, state.playerCol, state.lives);
    if (state.lives <= 0)
    {
        loseGame(state, LossReason::OutOfLives);
    }
}

// Returns true if a Minotaur caught the player.
bool checkMinotaurCollision(GameState& state)
{
    if (state.won || state.lost || state.invulnerableTime > 0.0f)
    {
        return false;
    }
    for (int i = 0; i < state.minotaurCount; ++i)
    {
        Minotaur& minotaur = state.minotaurs[i];
        if (minotaur.released && minotaur.row == state.playerRow && minotaur.col == state.playerCol)
        {
            --state.lives;
            ++state.damageTaken;
            ++state.timesCaught;
            state.invulnerableTime = CAUGHT_INVULNERABILITY;
            pushEvent(state, EventType::Caught, state.playerRow, state.playerCol, state.lives);

            // The Minotaur returns to its lair to rest.
            minotaur.row = minotaur.prevRow = minotaur.spawnRow;
            minotaur.col = minotaur.prevCol = minotaur.spawnCol;
            minotaur.hunting = false;
            minotaur.stepTimer = CAUGHT_MINOTAUR_PAUSE;
            if (state.config.minotaurChase)
            {
                // Back to the entrance, then it starts the chase again.
                minotaur.stepTimer = CAUGHT_MINOTAUR_PAUSE * 1.5f;
            }

            if (state.lives <= 0)
            {
                loseGame(state, LossReason::OutOfLives);
            }
            return true;
        }
    }
    return false;
}

void stepMinotaur(GameState& state, int index, const int* playerDistances)
{
    Minotaur& minotaur = state.minotaurs[index];
    const int distance = playerDistances[cellIndex(state, minotaur.row, minotaur.col)];
    const bool wasHunting = minotaur.hunting;
    minotaur.hunting = distance >= 0 && (state.config.minotaurChase || distance <= MINOTAUR_HUNT_RADIUS);
    if (minotaur.hunting && !wasHunting)
    {
        pushEvent(state, EventType::MinotaurAlert, minotaur.row, minotaur.col, index);
    }

    Direction options[4];
    int optionCount = 0;
    Direction chosen = Direction::Up;
    bool found = false;

    for (const Direction direction : ALL_DIRECTIONS)
    {
        if (!isPassable(state.maze, minotaur.row, minotaur.col, direction, false))
        {
            continue;
        }
        int rowOffset = 0;
        int colOffset = 0;
        getDirectionOffset(direction, rowOffset, colOffset);
        const int nextRow = minotaur.row + rowOffset;
        const int nextCol = minotaur.col + colOffset;

        if (minotaur.hunting)
        {
            // Follow the distance field downhill: always towards the player.
            if (playerDistances[cellIndex(state, nextRow, nextCol)] == distance - 1)
            {
                chosen = direction;
                found = true;
            }
        }
        else if (nextRow != minotaur.prevRow || nextCol != minotaur.prevCol)
        {
            options[optionCount++] = direction; // wander, but don't turn back
        }
    }

    if (!minotaur.hunting)
    {
        if (optionCount > 0)
        {
            std::uniform_int_distribution<int> pick(0, optionCount - 1);
            chosen = options[pick(state.rng)];
            found = true;
        }
        else if (minotaur.prevRow != minotaur.row || minotaur.prevCol != minotaur.col)
        {
            // Dead end: turn around.
            const int rowDelta = minotaur.prevRow - minotaur.row;
            const int colDelta = minotaur.prevCol - minotaur.col;
            chosen = rowDelta < 0 ? Direction::Up
                     : rowDelta > 0 ? Direction::Down
                     : colDelta < 0 ? Direction::Left
                                    : Direction::Right;
            found = true;
        }
    }

    minotaur.prevRow = minotaur.row;
    minotaur.prevCol = minotaur.col;
    if (!found)
    {
        return;
    }

    int rowOffset = 0;
    int colOffset = 0;
    getDirectionOffset(chosen, rowOffset, colOffset);
    minotaur.row += rowOffset;
    minotaur.col += colOffset;
    pushEvent(state, EventType::MinotaurStep, minotaur.row, minotaur.col, index);
    checkMinotaurCollision(state);
}

void applyPowerUp(GameState& state, Item item)
{
    switch (item)
    {
    case Item::Torch:
        state.torchLevel = clampInt(state.torchLevel + 1, 0, MAX_TORCH_LEVEL);
        break;
    case Item::Boots:
        state.bootsTime = BOOTS_DURATION;
        break;
    case Item::Hammer:
        state.hammerCharges += HAMMER_CHARGES_PER_PICKUP;
        break;
    case Item::Hourglass:
        state.freezeTime = FREEZE_DURATION;
        break;
    case Item::Heart:
        state.lives = clampInt(state.lives + 1, 0, state.maxLives);
        break;
    case Item::Compass:
        state.compassTime = COMPASS_DURATION;
        break;
    default:
        return;
    }
    ++state.powerUpsCollected;
    pushEvent(state, EventType::PowerUp, state.playerRow, state.playerCol, static_cast<int>(item));
}

} // namespace

// ---------------------------------------------------------------------------
// Game lifecycle
// ---------------------------------------------------------------------------

RunConfig clampConfig(const RunConfig& config)
{
    RunConfig result = config;
    result.size = clampInt(result.size, MIN_MAZE_SIZE, MAX_MAZE_SIZE);
    result.lives = clampInt(result.lives, 1, 5);
    result.minotaurCount = clampInt(result.minotaurCount, 0, 3);
    result.teleporterPairs = clampInt(result.teleporterPairs, 0, 3);
    result.gemCount = clampInt(result.gemCount, 0, 60);
    result.timeLimitSeconds = result.timeLimitSeconds < 0 ? 0 : result.timeLimitSeconds;
    result.difficulty = clampInt(result.difficulty, 0, DIFFICULTY_COUNT - 1);
    if (result.minotaurStepSeconds < 0.15f)
    {
        result.minotaurStepSeconds = 0.15f;
    }
    if (result.gemCount == 0)
    {
        result.requireAllGems = false;
    }
    return result;
}

void initRunGrids(GameState& state, const RunConfig& clamped)
{
    const int size = state.maze.size;
    state.config = clamped;
    state.visitedCells = createFlagGrid(size, size);
    state.hintTiles = createFlagGrid(state.maze.tileCount, state.maze.tileCount);
    state.discovered = createFlagGrid(size, size);
    state.items = createItemGrid(size);
    state.teleportLinks = new int[size * size];
    for (int i = 0; i < size * size; ++i)
    {
        state.teleportLinks[i] = -1;
    }
    state.visitedCells[0][0] = true;
    state.lives = clamped.lives;
    state.maxLives = clamped.lives > 5 ? clamped.lives : 5;
    state.rng.seed(clamped.seed ^ 0x9E3779B9u);
}

void finishRunSetup(GameState& state)
{
    // The reference route ignores doors: it is what a player without keys must walk.
    state.optimalMoves = 0;
    Path route;
    if (findShortestPath(state.maze, 0, 0, false, route))
    {
        state.optimalMoves = route.length - 1;
    }
    destroyPath(route);

    state.gemsTotal = 0;
    for (int row = 0; row < state.maze.size; ++row)
    {
        for (int col = 0; col < state.maze.size; ++col)
        {
            state.gemsTotal += state.items[row][col] == Item::Gem ? 1 : 0;
        }
    }
    if (state.gemsTotal == 0)
    {
        state.config.requireAllGems = false;
    }
    state.tourMoves = computeTourMoves(state, 0, 0);

    state.facing = isPassable(state.maze, 0, 0, Direction::Right, false) ? Direction::Right : Direction::Down;
    revealAround(state);
}

bool startRun(GameState& state, const RunConfig& config)
{
    endGame(state);
    const RunConfig clamped = clampConfig(config);
    if (!isValidMazeSize(config.size) || !createMaze(state.maze, clamped.size, clamped.seed))
    {
        return false;
    }
    initRunGrids(state, clamped);

    const int size = state.maze.size;
    int* startDistances = new int[size * size];
    computeDistances(state.maze, 0, 0, false, startDistances);
    placeItems(state, startDistances);
    spawnMinotaurs(state, startDistances);
    delete[] startDistances;

    finishRunSetup(state);
    return true;
}

bool startGame(GameState& state, int size, unsigned int seed)
{
    RunConfig config;
    config.size = size;
    config.seed = seed;
    return startRun(state, config);
}

void endGame(GameState& state)
{
    destroyFlagGrid(state.visitedCells, state.maze.size);
    destroyFlagGrid(state.hintTiles, state.maze.tileCount);
    destroyFlagGrid(state.discovered, state.maze.size);
    destroyItemGrid(state.items, state.maze.size);
    delete[] state.teleportLinks;
    delete[] state.minotaurs;
    destroyMaze(state.maze);
    state = GameState();
}

unsigned int generateRandomSeed()
{
    std::random_device device;
    const long long now = static_cast<long long>(
        std::chrono::steady_clock::now().time_since_epoch().count());
    return device() ^ static_cast<unsigned int>(now);
}

// ---------------------------------------------------------------------------
// Rules
// ---------------------------------------------------------------------------

bool isExitCell(const Maze& maze, int row, int col)
{
    return row == maze.size - 1 && col == maze.size - 1;
}

bool isExitUnlocked(const GameState& state)
{
    return !state.config.requireAllGems || state.gemsCollected >= state.gemsTotal;
}

bool isMoveSuccessful(MoveResult result)
{
    switch (result)
    {
    case MoveResult::Moved:
    case MoveResult::PickedUpKey:
    case MoveResult::PickedUpItem:
    case MoveResult::OpenedDoor:
    case MoveResult::Teleported:
    case MoveResult::Hurt:
    case MoveResult::Caught:
    case MoveResult::ExitLocked:
    case MoveResult::ReachedExit:
        return true;
    default:
        return false;
    }
}

Item getItem(const GameState& state, int row, int col)
{
    if (state.items == nullptr || !isInsideMaze(state.maze, row, col))
    {
        return Item::None;
    }
    return state.items[row][col];
}

int getTeleportTarget(const GameState& state, int row, int col)
{
    if (state.teleportLinks == nullptr || !isInsideMaze(state.maze, row, col))
    {
        return -1;
    }
    return state.teleportLinks[cellIndex(state, row, col)];
}

namespace
{
// Where a trap is in its up/down cycle, in seconds. Every trap has its own
// rhythm so the player can time the jump.
double spikePhase(const GameState& state, int row, int col)
{
    const double offset = static_cast<double>((row * 7 + col * 13) % 10) * 0.28;
    return std::fmod(state.elapsed + offset, static_cast<double>(SPIKE_CYCLE_SECONDS));
}
} // namespace

bool areSpikesUp(const GameState& state, int row, int col)
{
    return spikePhase(state, row, col) < static_cast<double>(SPIKE_UP_SECONDS);
}

float getSpikeLift(const GameState& state, int row, int col)
{
    const float phase = static_cast<float>(spikePhase(state, row, col));
    if (phase < SPIKE_UP_SECONDS)
    {
        const float rise = phase / 0.08f; // snap up quickly
        return rise < 1.0f ? rise : 1.0f;
    }
    const float fall = 1.0f - (phase - SPIKE_UP_SECONDS) / 0.25f; // sink slowly
    const float warning = (SPIKE_CYCLE_SECONDS - phase) < 0.35f ? 0.15f : 0.0f; // rattle before rising
    return fall > warning ? fall : warning;
}

int getRevealRadius(const GameState& state)
{
    return 1 + state.torchLevel;
}

void revealAround(GameState& state)
{
    if (state.discovered == nullptr)
    {
        return;
    }
    if (!state.config.fogOfWar)
    {
        for (int row = 0; row < state.maze.size; ++row)
        {
            for (int col = 0; col < state.maze.size; ++col)
            {
                state.discovered[row][col] = true;
            }
        }
        return;
    }
    const int radius = getRevealRadius(state);
    for (int row = state.playerRow - radius; row <= state.playerRow + radius; ++row)
    {
        for (int col = state.playerCol - radius; col <= state.playerCol + radius; ++col)
        {
            if (isInsideMaze(state.maze, row, col))
            {
                state.discovered[row][col] = true;
            }
        }
    }
}

MoveResult movePlayer(GameState& state, Direction direction)
{
    if (state.won)
    {
        return MoveResult::ReachedExit;
    }
    if (state.lost)
    {
        return MoveResult::GameOver;
    }

    int rowOffset = 0;
    int colOffset = 0;
    getDirectionOffset(direction, rowOffset, colOffset);
    const int targetRow = state.playerRow + rowOffset;
    const int targetCol = state.playerCol + colOffset;

    if (!isInsideMaze(state.maze, targetRow, targetCol))
    {
        pushEvent(state, EventType::Bump, state.playerRow, state.playerCol);
        return MoveResult::BlockedByEdge;
    }

    const Tile passage = getPassageTile(state.maze, state.playerRow, state.playerCol, direction);
    if (passage == Tile::Wall)
    {
        pushEvent(state, EventType::Bump, state.playerRow, state.playerCol);
        return MoveResult::BlockedByWall;
    }

    bool openedDoor = false;
    if (passage == Tile::Door)
    {
        if (state.keysHeld <= 0)
        {
            pushEvent(state, EventType::Bump, state.playerRow, state.playerCol);
            return MoveResult::BlockedByLockedDoor;
        }
        // Using a key opens the door for good.
        --state.keysHeld;
        ++state.doorsOpened;
        setPassageTile(state.maze, state.playerRow, state.playerCol, direction, Tile::Path);
        pushEvent(state, EventType::DoorOpened, state.playerRow, state.playerCol, static_cast<int>(direction));
        openedDoor = true;
    }

    state.playerRow = targetRow;
    state.playerCol = targetCol;
    ++state.moves;
    state.visitedCells[targetRow][targetCol] = true;
    pushEvent(state, EventType::Step, targetRow, targetCol);

    MoveResult result = openedDoor ? MoveResult::OpenedDoor : MoveResult::Moved;
    if (getCellTile(state.maze, targetRow, targetCol) == Tile::Key)
    {
        setCellTile(state.maze, targetRow, targetCol, Tile::Path);
        ++state.keysHeld;
        pushEvent(state, EventType::KeyPickup, targetRow, targetCol);
        result = MoveResult::PickedUpKey;
    }

    const Item item = getItem(state, targetRow, targetCol);
    switch (item)
    {
    case Item::None:
        break;
    case Item::Gem:
        state.items[targetRow][targetCol] = Item::None;
        ++state.gemsCollected;
        pushEvent(state, EventType::GemPickup, targetRow, targetCol, state.gemsCollected);
        result = MoveResult::PickedUpItem;
        break;
    case Item::Spikes:
        if (areSpikesUp(state, targetRow, targetCol) && state.invulnerableTime <= 0.0f)
        {
            hurtPlayer(state);
            result = state.lost ? MoveResult::GameOver : MoveResult::Hurt;
        }
        break;
    case Item::Teleporter:
    {
        const int target = getTeleportTarget(state, targetRow, targetCol);
        if (target >= 0)
        {
            state.playerRow = target / state.maze.size;
            state.playerCol = target % state.maze.size;
            state.visitedCells[state.playerRow][state.playerCol] = true;
            ++state.teleportsUsed;
            pushEvent(state, EventType::Teleport, state.playerRow, state.playerCol, cellIndex(state, targetRow, targetCol));
            result = MoveResult::Teleported;
        }
        break;
    }
    default:
        state.items[targetRow][targetCol] = Item::None;
        applyPowerUp(state, item);
        result = MoveResult::PickedUpItem;
        break;
    }

    revealAround(state);

    if (!state.lost && isExitCell(state.maze, state.playerRow, state.playerCol))
    {
        if (isExitUnlocked(state))
        {
            state.won = true;
            state.hintVisible = false;
            pushEvent(state, EventType::Won, state.playerRow, state.playerCol);
            result = MoveResult::ReachedExit;
        }
        else
        {
            pushEvent(state, EventType::ExitLocked, state.playerRow, state.playerCol,
                      state.gemsTotal - state.gemsCollected);
            result = MoveResult::ExitLocked;
        }
    }

    if (!state.won && checkMinotaurCollision(state))
    {
        result = state.lost ? MoveResult::GameOver : MoveResult::Caught;
    }

    refreshHint(state);
    return result;
}

void updateRun(GameState& state, float deltaSeconds)
{
    if (state.won || state.lost || deltaSeconds <= 0.0f)
    {
        return;
    }

    state.elapsed += static_cast<double>(deltaSeconds);
    const bool compassWasOn = state.compassTime > 0.0f;
    float* timers[] = {&state.bootsTime, &state.freezeTime, &state.compassTime, &state.invulnerableTime};
    for (float* timer : timers)
    {
        *timer = (*timer > deltaSeconds) ? *timer - deltaSeconds : 0.0f;
    }
    if (compassWasOn && state.compassTime <= 0.0f)
    {
        refreshHint(state); // the free compass route disappears
    }

    // Time limit
    const int secondsLeft = getSecondsLeft(state);
    if (secondsLeft >= 0)
    {
        if (secondsLeft <= 0)
        {
            loseGame(state, LossReason::OutOfTime);
            return;
        }
        if ((secondsLeft <= 10 || secondsLeft == 30) && secondsLeft != state.lastTimeWarning)
        {
            state.lastTimeWarning = secondsLeft;
            pushEvent(state, EventType::TimeWarning, state.playerRow, state.playerCol, secondsLeft);
        }
    }

    // Standing on a trap when the spikes come up hurts.
    if (getItem(state, state.playerRow, state.playerCol) == Item::Spikes &&
        areSpikesUp(state, state.playerRow, state.playerCol))
    {
        hurtPlayer(state);
        if (state.lost)
        {
            return;
        }
    }

    // Minotaurs
    if (state.minotaurCount <= 0 || state.freezeTime > 0.0f)
    {
        return;
    }
    int* playerDistances = nullptr;
    for (int i = 0; i < state.minotaurCount && !state.lost; ++i)
    {
        Minotaur& minotaur = state.minotaurs[i];
        if (!minotaur.released)
        {
            if (state.elapsed < static_cast<double>(minotaur.releaseTime))
            {
                continue;
            }
            minotaur.released = true;
            minotaur.hunting = true;
            minotaur.stepTimer = getMinotaurStepSeconds(state);
            pushEvent(state, EventType::MinotaurReleased, minotaur.row, minotaur.col, i);
            continue;
        }
        minotaur.stepTimer -= deltaSeconds;
        if (minotaur.stepTimer > 0.0f)
        {
            continue;
        }
        if (playerDistances == nullptr)
        {
            playerDistances = new int[state.maze.size * state.maze.size];
            computeDistances(state.maze, state.playerRow, state.playerCol, false, playerDistances);
        }
        stepMinotaur(state, i, playerDistances);
        // Hunting Minotaurs are a little faster than wandering ones.
        const float stepTime = getMinotaurStepSeconds(state) * (minotaur.hunting ? 0.85f : 1.15f);
        if (minotaur.stepTimer <= 0.0f)
        {
            minotaur.stepTimer += stepTime;
            if (minotaur.stepTimer <= 0.0f)
            {
                minotaur.stepTimer = stepTime;
            }
        }
    }
    delete[] playerDistances;
}

bool useHammer(GameState& state, Direction direction)
{
    if (state.hammerCharges <= 0 || state.won || state.lost)
    {
        return false;
    }
    int rowOffset = 0;
    int colOffset = 0;
    getDirectionOffset(direction, rowOffset, colOffset);
    if (!isInsideMaze(state.maze, state.playerRow + rowOffset, state.playerCol + colOffset))
    {
        return false; // the outer wall is unbreakable
    }
    if (getPassageTile(state.maze, state.playerRow, state.playerCol, direction) != Tile::Wall)
    {
        return false;
    }
    setPassageTile(state.maze, state.playerRow, state.playerCol, direction, Tile::Path);
    --state.hammerCharges;
    ++state.wallsBroken;
    pushEvent(state, EventType::WallBroken, state.playerRow, state.playerCol, static_cast<int>(direction));
    refreshHint(state);
    return true;
}

bool toggleHint(GameState& state)
{
    if (!state.config.hintsAllowed && !state.hintVisible)
    {
        return false;
    }
    state.hintVisible = !state.hintVisible;
    if (state.hintVisible)
    {
        ++state.hintsUsed;
    }
    refreshHint(state);
    return true;
}

namespace
{
// Shortest corridor route between two cells (doors closed), via a distance
// field from the target: walk downhill from the start.
bool findRouteBetween(const GameState& state, int fromRow, int fromCol, int toRow, int toCol, Path& path)
{
    const int size = state.maze.size;
    int* distances = new int[size * size];
    computeDistances(state.maze, toRow, toCol, false, distances);
    const int length = distances[cellIndex(state, fromRow, fromCol)];
    if (length < 0)
    {
        delete[] distances;
        return false;
    }
    path.length = length + 1;
    path.rows = new int[path.length];
    path.cols = new int[path.length];
    int row = fromRow;
    int col = fromCol;
    for (int i = 0; i < path.length; ++i)
    {
        path.rows[i] = row;
        path.cols[i] = col;
        const int here = distances[cellIndex(state, row, col)];
        for (const Direction direction : ALL_DIRECTIONS)
        {
            if (!isPassable(state.maze, row, col, direction, false))
            {
                continue;
            }
            int rowOffset = 0;
            int colOffset = 0;
            getDirectionOffset(direction, rowOffset, colOffset);
            if (distances[cellIndex(state, row + rowOffset, col + colOffset)] == here - 1)
            {
                row += rowOffset;
                col += colOffset;
                break;
            }
        }
    }
    delete[] distances;
    return true;
}

// Nearest remaining gem along the corridors from a cell.
bool findNearestGem(const GameState& state, int fromRow, int fromCol, const bool* taken, int& row, int& col)
{
    const int size = state.maze.size;
    int* distances = new int[size * size];
    computeDistances(state.maze, fromRow, fromCol, false, distances);
    int best = -1;
    for (int r = 0; r < size; ++r)
    {
        for (int c = 0; c < size; ++c)
        {
            const int index = r * size + c;
            if (state.items[r][c] == Item::Gem && (taken == nullptr || !taken[index]) && distances[index] >= 0 &&
                (best < 0 || distances[index] < best))
            {
                best = distances[index];
                row = r;
                col = c;
            }
        }
    }
    delete[] distances;
    return best >= 0;
}
} // namespace

bool getGuideTarget(const GameState& state, int& row, int& col)
{
    if (state.items == nullptr || state.won || state.lost)
    {
        return false;
    }
    const bool gemsLeft = state.gemsCollected < state.gemsTotal;
    if (gemsLeft && (state.compassTime > 0.0f || !isExitUnlocked(state)) &&
        findNearestGem(state, state.playerRow, state.playerCol, nullptr, row, col))
    {
        return true;
    }
    row = state.maze.size - 1;
    col = state.maze.size - 1;
    return true;
}

int computeTourMoves(const GameState& state, int fromRow, int fromCol)
{
    const int size = state.maze.size;
    if (state.items == nullptr || size <= 0)
    {
        return 0;
    }
    bool* taken = new bool[size * size];
    for (int i = 0; i < size * size; ++i)
    {
        taken[i] = false;
    }
    int* distances = new int[size * size];
    int total = 0;
    int row = fromRow;
    int col = fromCol;
    int gemRow = 0;
    int gemCol = 0;
    while (findNearestGem(state, row, col, taken, gemRow, gemCol))
    {
        computeDistances(state.maze, row, col, false, distances);
        total += distances[gemRow * size + gemCol];
        taken[gemRow * size + gemCol] = true;
        row = gemRow;
        col = gemCol;
    }
    computeDistances(state.maze, row, col, false, distances);
    const int toExit = distances[size * size - 1];
    total += toExit > 0 ? toExit : 0;
    delete[] distances;
    delete[] taken;
    return total;
}

bool isDeadEnd(const Maze& maze, int row, int col)
{
    int open = 0;
    for (const Direction direction : ALL_DIRECTIONS)
    {
        open += isPassable(maze, row, col, direction, true) ? 1 : 0;
    }
    return open == 1;
}

float getMinotaurStepSeconds(const GameState& state)
{
    return state.config.minotaurStepSeconds * getMinotaurSpeedFactor(state.config.difficulty);
}

int getHammerCost()
{
    return HAMMER_SCORE_COST;
}

const char* getDifficultyName(int difficulty)
{
    static const char* const NAMES[DIFFICULTY_COUNT] = {"Easy", "Normal", "Hard", "Expert"};
    return NAMES[clampInt(difficulty, 0, DIFFICULTY_COUNT - 1)];
}

float getParTimeFactor(int difficulty)
{
    static const float FACTORS[DIFFICULTY_COUNT] = {1.4f, 1.0f, 0.78f, 0.62f};
    return FACTORS[clampInt(difficulty, 0, DIFFICULTY_COUNT - 1)];
}

float getStepParFactor(int difficulty)
{
    static const float FACTORS[DIFFICULTY_COUNT] = {0.0f, 1.6f, 1.3f, 1.12f};
    return FACTORS[clampInt(difficulty, 0, DIFFICULTY_COUNT - 1)];
}

float getMinotaurSpeedFactor(int difficulty)
{
    static const float FACTORS[DIFFICULTY_COUNT] = {1.3f, 1.0f, 0.85f, 0.72f};
    return FACTORS[clampInt(difficulty, 0, DIFFICULTY_COUNT - 1)];
}

float getScoreFactor(int difficulty)
{
    static const float FACTORS[DIFFICULTY_COUNT] = {0.75f, 1.0f, 1.25f, 1.5f};
    return FACTORS[clampInt(difficulty, 0, DIFFICULTY_COUNT - 1)];
}

// Recomputes the hint overlay: the shortest corridor route from the player
// to the exit, marking both the cells and the passages between them.
void refreshHint(GameState& state)
{
    clearFlagGrid(state.hintTiles, state.maze.tileCount, state.maze.tileCount);
    if ((!state.hintVisible && state.compassTime <= 0.0f) || state.won || state.lost)
    {
        return;
    }

    int targetRow = 0;
    int targetCol = 0;
    if (!getGuideTarget(state, targetRow, targetCol))
    {
        return;
    }
    Path route;
    if (!findRouteBetween(state, state.playerRow, state.playerCol, targetRow, targetCol, route))
    {
        return;
    }

    for (int i = 0; i < route.length; ++i)
    {
        const int tileRow = cellToTile(route.rows[i]);
        const int tileCol = cellToTile(route.cols[i]);
        state.hintTiles[tileRow][tileCol] = true;
        if (i > 0)
        {
            const int previousRow = cellToTile(route.rows[i - 1]);
            const int previousCol = cellToTile(route.cols[i - 1]);
            state.hintTiles[(tileRow + previousRow) / 2][(tileCol + previousCol) / 2] = true;
        }
    }
    destroyPath(route);
}

int countVisitedCells(const GameState& state)
{
    int visited = 0;
    for (int row = 0; row < state.maze.size; ++row)
    {
        for (int col = 0; col < state.maze.size; ++col)
        {
            if (state.visitedCells[row][col])
            {
                ++visited;
            }
        }
    }
    return visited;
}

int countDiscoveredCells(const GameState& state)
{
    int count = 0;
    for (int row = 0; row < state.maze.size; ++row)
    {
        for (int col = 0; col < state.maze.size; ++col)
        {
            if (state.discovered[row][col])
            {
                ++count;
            }
        }
    }
    return count;
}

int getSecondsLeft(const GameState& state)
{
    if (state.config.timeLimitSeconds <= 0)
    {
        return -1;
    }
    const double left = static_cast<double>(state.config.timeLimitSeconds) - state.elapsed;
    return left <= 0.0 ? 0 : static_cast<int>(std::ceil(left));
}

int distanceToNearestMinotaur(const GameState& state)
{
    int best = -1;
    for (int i = 0; i < state.minotaurCount; ++i)
    {
        if (!state.minotaurs[i].released)
        {
            continue;
        }
        const int rowDelta = std::abs(state.minotaurs[i].row - state.playerRow);
        const int colDelta = std::abs(state.minotaurs[i].col - state.playerCol);
        const int distance = rowDelta + colDelta;
        if (best < 0 || distance < best)
        {
            best = distance;
        }
    }
    return best;
}

void clearEvents(GameState& state)
{
    state.eventCount = 0;
}

// Score = 100 per row of maze size, +2 for every step saved compared to the
// corridor route (or -2 per extra step), -1 per second, -100 per hint,
// +25 per door used, +50 per gem, +40 per life left and +3 per second left
// on the clock. Never negative.
int calculateScore(const GameState& state, long long elapsedSeconds)
{
    if (!state.won)
    {
        return 0;
    }

    long long score = static_cast<long long>(state.maze.size) * 100;
    score += static_cast<long long>(state.optimalMoves - state.moves) * 2;
    score -= elapsedSeconds;
    score -= static_cast<long long>(state.hintsUsed) * 100;
    score += static_cast<long long>(state.doorsOpened) * 25;
    score += static_cast<long long>(state.gemsCollected) * 50;
    score += static_cast<long long>(state.lives) * 40;
    score -= static_cast<long long>(state.wallsBroken) * HAMMER_SCORE_COST;
    if (state.config.timeLimitSeconds > 0)
    {
        const long long left = static_cast<long long>(state.config.timeLimitSeconds) - elapsedSeconds;
        score += left > 0 ? left * 3 : 0;
    }

    score = static_cast<long long>(static_cast<double>(score) * getScoreFactor(state.config.difficulty));
    if (score < 0)
    {
        score = 0;
    }
    return static_cast<int>(score);
}

// ---------------------------------------------------------------------------
// Camera
// ---------------------------------------------------------------------------

int computeViewportStart(int focus, int total, int window)
{
    if (total <= window)
    {
        return 0;
    }
    int start = focus - window / 2;
    if (start < 0)
    {
        start = 0;
    }
    if (start > total - window)
    {
        start = total - window;
    }
    return start;
}

// ---------------------------------------------------------------------------
// Leaderboard
// ---------------------------------------------------------------------------

bool qualifiesForLeaderboard(const Leaderboard& board, int score)
{
    if (score <= 0)
    {
        return false;
    }
    return board.count < LEADERBOARD_SIZE || score > board.entries[board.count - 1].score;
}

bool addToLeaderboard(Leaderboard& board, const ScoreEntry& entry)
{
    if (!qualifiesForLeaderboard(board, entry.score))
    {
        return false;
    }

    // Start at the first free slot (or the worst entry when the board is full)
    // and slide lower scores down until the new entry fits.
    int position = (board.count < LEADERBOARD_SIZE) ? board.count : LEADERBOARD_SIZE - 1;
    if (board.count < LEADERBOARD_SIZE)
    {
        ++board.count;
    }
    while (position > 0 && board.entries[position - 1].score < entry.score)
    {
        board.entries[position] = board.entries[position - 1];
        --position;
    }
    board.entries[position] = entry;
    return true;
}
