// test_main.cpp
// QA: automated tests for the game logic (maze, rules, number trail, validation).
//
// The tests use only the pure-logic modules, which is possible because those
// modules never touch cin / cout. Build and run with:  make test

#include "../src/campaign.h"
#include "../src/custom_map.h"
#include "../src/game.h"
#include "../src/maze.h"
#include "../src/number_trail.h"
#include "../src/profile.h"
#include "../src/savegame.h"
#include "../src/validation.h"

#include <iostream>
#include <string>

namespace
{

int checksRun = 0;
int checksFailed = 0;

void check(bool condition, const char* expression, const char* file, int line)
{
    ++checksRun;
    if (!condition)
    {
        ++checksFailed;
        std::cout << "    FAILED: " << expression << "  (" << file << ":" << line << ")\n";
    }
}

#define CHECK(condition) check((condition), #condition, __FILE__, __LINE__)

const unsigned int TEST_SEEDS[] = {1u, 7u, 42u, 2026u, 123456789u};
const int TEST_SIZES[] = {10, 17, 25, 50};

// ===========================================================================
// Maze generation
// ===========================================================================

void testMazeSizeValidation()
{
    CHECK(!isValidMazeSize(9));
    CHECK(isValidMazeSize(10));
    CHECK(isValidMazeSize(33));
    CHECK(isValidMazeSize(50));
    CHECK(!isValidMazeSize(51));
    CHECK(!isValidMazeSize(-10));
}

void testCreateMazeRejectsInvalidSize()
{
    Maze maze;
    CHECK(!createMaze(maze, 5, 1));
    CHECK(maze.tiles == nullptr);
    CHECK(!createMaze(maze, 100, 1));
    CHECK(maze.tiles == nullptr);
}

void testMazeAllocationAndCleanup()
{
    Maze maze;
    CHECK(createMaze(maze, 10, 42));
    CHECK(maze.size == 10);
    CHECK(maze.tileCount == 21);
    CHECK(maze.tiles != nullptr);
    destroyMaze(maze);
    CHECK(maze.tiles == nullptr);
    CHECK(maze.size == 0);
    destroyMaze(maze); // destroying twice must be safe
    CHECK(maze.tiles == nullptr);
}

void testEveryCellIsReachable()
{
    for (const int size : TEST_SIZES)
    {
        for (const unsigned int seed : TEST_SEEDS)
        {
            Maze maze;
            createMaze(maze, size, seed);
            int* distances = new int[size * size];
            computeDistances(maze, 0, 0, false, distances);
            bool allReachable = true;
            for (int i = 0; i < size * size; ++i)
            {
                allReachable = allReachable && distances[i] >= 0;
            }
            CHECK(allReachable);
            delete[] distances;
            destroyMaze(maze);
        }
    }
}

// A perfect maze over N*N cells has exactly N*N - 1 open passages (a tree).
void testMazeIsPerfectBeforeDoors()
{
    for (const int size : TEST_SIZES)
    {
        Maze maze;
        createMaze(maze, size, 99);
        int passages = 0;
        for (int row = 0; row < size; ++row)
        {
            for (int col = 0; col < size; ++col)
            {
                if (col + 1 < size && getPassageTile(maze, row, col, Direction::Right) == Tile::Path)
                {
                    ++passages;
                }
                if (row + 1 < size && getPassageTile(maze, row, col, Direction::Down) == Tile::Path)
                {
                    ++passages;
                }
            }
        }
        CHECK(passages == size * size - 1);
        destroyMaze(maze);
    }
}

void testOuterWallIsClosed()
{
    Maze maze;
    createMaze(maze, 15, 3);
    const int last = maze.tileCount - 1;
    bool closed = true;
    for (int i = 0; i < maze.tileCount; ++i)
    {
        const bool topIsEntrance = (i == 1);
        const bool bottomIsExit = (i == last - 1);
        closed = closed && (topIsEntrance || maze.tiles[0][i] == Tile::Wall);
        closed = closed && (bottomIsExit || maze.tiles[last][i] == Tile::Wall);
        closed = closed && maze.tiles[i][0] == Tile::Wall;
        closed = closed && maze.tiles[i][last] == Tile::Wall;
    }
    CHECK(closed);
    CHECK(maze.tiles[0][1] == Tile::Path);
    CHECK(maze.tiles[last][last - 1] == Tile::Path);
    destroyMaze(maze);
}

void testSameSeedGivesSameMaze()
{
    Maze first;
    Maze second;
    createMaze(first, 20, 2026);
    createMaze(second, 20, 2026);
    bool identical = true;
    for (int row = 0; row < first.tileCount; ++row)
    {
        for (int col = 0; col < first.tileCount; ++col)
        {
            identical = identical && first.tiles[row][col] == second.tiles[row][col];
        }
    }
    CHECK(identical);
    destroyMaze(first);
    destroyMaze(second);
}

void testDifferentSeedsGiveDifferentMazes()
{
    Maze first;
    Maze second;
    createMaze(first, 20, 1);
    createMaze(second, 20, 2);
    int differences = 0;
    for (int row = 0; row < first.tileCount; ++row)
    {
        for (int col = 0; col < first.tileCount; ++col)
        {
            if (first.tiles[row][col] != second.tiles[row][col])
            {
                ++differences;
            }
        }
    }
    CHECK(differences > 0);
    destroyMaze(first);
    destroyMaze(second);
}

void testDoorsAreShortcuts()
{
    for (const int size : TEST_SIZES)
    {
        for (const unsigned int seed : TEST_SEEDS)
        {
            Maze maze;
            createMaze(maze, size, seed);

            int doorTiles = 0;
            for (int row = 0; row < maze.tileCount; ++row)
            {
                for (int col = 0; col < maze.tileCount; ++col)
                {
                    if (maze.tiles[row][col] == Tile::Door)
                    {
                        ++doorTiles;
                    }
                }
            }
            CHECK(doorTiles == maze.doorCount);
            CHECK(maze.doorCount <= size / 5);

            Path withoutDoors;
            Path withDoors;
            CHECK(findShortestPath(maze, 0, 0, false, withoutDoors));
            CHECK(findShortestPath(maze, 0, 0, true, withDoors));
            CHECK(withDoors.length <= withoutDoors.length);
            destroyPath(withoutDoors);
            destroyPath(withDoors);
            destroyMaze(maze);
        }
    }
}

void testKeyPlacement()
{
    for (const unsigned int seed : TEST_SEEDS)
    {
        Maze maze;
        createMaze(maze, 30, seed);
        int keys = 0;
        for (int row = 0; row < maze.size; ++row)
        {
            for (int col = 0; col < maze.size; ++col)
            {
                if (getCellTile(maze, row, col) == Tile::Key)
                {
                    ++keys;
                }
            }
        }
        CHECK(keys == maze.keyCount);
        CHECK(maze.keyCount == (maze.doorCount + 1) / 2);
        CHECK(getCellTile(maze, 0, 0) != Tile::Key);
        CHECK(getCellTile(maze, maze.size - 1, maze.size - 1) != Tile::Key);
        destroyMaze(maze);
    }
}

void testShortestPathIsValid()
{
    Maze maze;
    createMaze(maze, 25, 77);
    Path path;
    CHECK(findShortestPath(maze, 0, 0, false, path));
    CHECK(path.length > 0);
    CHECK(path.rows[0] == 0 && path.cols[0] == 0);
    CHECK(path.rows[path.length - 1] == 24 && path.cols[path.length - 1] == 24);

    bool stepsAreAdjacent = true;
    for (int i = 1; i < path.length; ++i)
    {
        const int rowDelta = path.rows[i] - path.rows[i - 1];
        const int colDelta = path.cols[i] - path.cols[i - 1];
        const int distance = (rowDelta < 0 ? -rowDelta : rowDelta) + (colDelta < 0 ? -colDelta : colDelta);
        stepsAreAdjacent = stepsAreAdjacent && distance == 1;
    }
    CHECK(stepsAreAdjacent);
    destroyPath(path);
    CHECK(path.rows == nullptr && path.length == 0);
    destroyMaze(maze);
}

// ===========================================================================
// Game rules
// ===========================================================================

void testStartGameState()
{
    GameState state;
    CHECK(startGame(state, 12, 5));
    CHECK(state.playerRow == 0 && state.playerCol == 0);
    CHECK(state.moves == 0);
    CHECK(state.visitedCells[0][0]);
    CHECK(state.optimalMoves >= 2 * (12 - 1)); // at least the Manhattan distance
    CHECK(!state.won);
    endGame(state);
    CHECK(state.visitedCells == nullptr && state.hintTiles == nullptr && state.maze.tiles == nullptr);
}

void testMovementBlockedByWall()
{
    GameState state;
    startGame(state, 10, 1);
    setPassageTile(state.maze, 0, 0, Direction::Right, Tile::Wall);
    CHECK(movePlayer(state, Direction::Right) == MoveResult::BlockedByWall);
    CHECK(state.playerRow == 0 && state.playerCol == 0);
    CHECK(state.moves == 0);
    endGame(state);
}

void testMovementThroughCorridor()
{
    GameState state;
    startGame(state, 10, 1);
    setPassageTile(state.maze, 0, 0, Direction::Right, Tile::Path);
    setCellTile(state.maze, 0, 1, Tile::Path);
    CHECK(movePlayer(state, Direction::Right) == MoveResult::Moved);
    CHECK(state.playerCol == 1);
    CHECK(state.moves == 1);
    CHECK(state.visitedCells[0][1]);
    CHECK(countVisitedCells(state) == 2);
    endGame(state);
}

void testCannotLeaveMaze()
{
    GameState state;
    startGame(state, 10, 1);
    CHECK(movePlayer(state, Direction::Up) == MoveResult::BlockedByEdge);   // even through the entrance
    CHECK(movePlayer(state, Direction::Left) == MoveResult::BlockedByEdge);
    CHECK(state.playerRow == 0 && state.playerCol == 0);
    endGame(state);
}

void testLockedDoorNeedsKey()
{
    GameState state;
    startGame(state, 10, 1);
    setPassageTile(state.maze, 0, 0, Direction::Down, Tile::Door);
    setCellTile(state.maze, 1, 0, Tile::Path);

    state.keysHeld = 0;
    CHECK(movePlayer(state, Direction::Down) == MoveResult::BlockedByLockedDoor);
    CHECK(state.playerRow == 0);

    state.keysHeld = 1;
    CHECK(movePlayer(state, Direction::Down) == MoveResult::OpenedDoor);
    CHECK(state.playerRow == 1);
    CHECK(state.keysHeld == 0);
    CHECK(state.doorsOpened == 1);
    CHECK(getPassageTile(state.maze, 0, 0, Direction::Down) == Tile::Path); // stays open
    endGame(state);
}

void testKeyPickup()
{
    GameState state;
    startGame(state, 10, 1);
    setPassageTile(state.maze, 0, 0, Direction::Right, Tile::Path);
    setCellTile(state.maze, 0, 1, Tile::Key);
    CHECK(movePlayer(state, Direction::Right) == MoveResult::PickedUpKey);
    CHECK(state.keysHeld == 1);
    CHECK(getCellTile(state.maze, 0, 1) == Tile::Path);
    endGame(state);
}

void testReachingExitWins()
{
    GameState state;
    startGame(state, 10, 1);
    state.playerRow = 9;
    state.playerCol = 8;
    setPassageTile(state.maze, 9, 8, Direction::Right, Tile::Path);
    CHECK(!isExitCell(state.maze, 9, 8));
    CHECK(isExitCell(state.maze, 9, 9));
    CHECK(movePlayer(state, Direction::Right) == MoveResult::ReachedExit);
    CHECK(state.won);
    endGame(state);
}

void testWalkingTheHintRouteWins()
{
    GameState state;
    startGame(state, 30, 2026);
    Path route;
    findShortestPath(state.maze, 0, 0, false, route);
    for (int i = 1; i < route.length; ++i)
    {
        const int rowDelta = route.rows[i] - route.rows[i - 1];
        const int colDelta = route.cols[i] - route.cols[i - 1];
        Direction direction = Direction::Right;
        if (rowDelta == 1)
        {
            direction = Direction::Down;
        }
        else if (rowDelta == -1)
        {
            direction = Direction::Up;
        }
        else if (colDelta == -1)
        {
            direction = Direction::Left;
        }
        CHECK(isMoveSuccessful(movePlayer(state, direction)));
    }
    CHECK(state.won);
    CHECK(state.moves == state.optimalMoves);
    destroyPath(route);
    endGame(state);
}

void testHintOverlay()
{
    GameState state;
    startGame(state, 15, 8);
    toggleHint(state);
    CHECK(state.hintVisible);
    CHECK(state.hintsUsed == 1);
    const int exitTile = cellToTile(14);
    CHECK(state.hintTiles[1][1]);               // starts at the player
    CHECK(state.hintTiles[exitTile][exitTile]); // ends at the exit
    toggleHint(state);
    CHECK(!state.hintVisible);
    CHECK(!state.hintTiles[1][1]);
    CHECK(state.hintsUsed == 1); // hiding is free
    endGame(state);
}

void testScoreRules()
{
    GameState state;
    startGame(state, 20, 4);
    CHECK(calculateScore(state, 10) == 0); // no score before winning

    state.won = true;
    state.moves = state.optimalMoves;
    const int fast = calculateScore(state, 10);
    const int slow = calculateScore(state, 100);
    CHECK(fast > slow);
    state.hintsUsed = 1;
    CHECK(calculateScore(state, 10) == fast - 100);
    state.moves = 1000000;
    CHECK(calculateScore(state, 10) == 0); // never negative
    endGame(state);
}

void testViewportCalculation()
{
    CHECK(computeViewportStart(5, 21, 41) == 0);    // small maze: no scrolling
    CHECK(computeViewportStart(0, 101, 41) == 0);   // clamped at the top
    CHECK(computeViewportStart(50, 101, 41) == 30); // centred on the player
    CHECK(computeViewportStart(100, 101, 41) == 60); // clamped at the bottom
}

void testLeaderboardOrdering()
{
    Leaderboard board;
    ScoreEntry entry;
    const int scores[] = {100, 300, 200, 50, 400};
    for (const int score : scores)
    {
        entry.score = score;
        CHECK(addToLeaderboard(board, entry));
    }
    CHECK(board.count == 5);
    CHECK(board.entries[0].score == 400);
    CHECK(board.entries[4].score == 50);

    entry.score = 10;
    CHECK(!addToLeaderboard(board, entry)); // too low for a full board
    entry.score = 250;
    CHECK(addToLeaderboard(board, entry));
    CHECK(board.count == 5);
    CHECK(board.entries[2].score == 250);
    CHECK(board.entries[4].score == 100); // 50 dropped off
    CHECK(!qualifiesForLeaderboard(board, 0));
}

// ===========================================================================
// Input validation
// ===========================================================================

void testParseInt()
{
    int value = 0;
    CHECK(parseInt("42", value) && value == 42);
    CHECK(parseInt("  7  ", value) && value == 7);
    CHECK(parseInt("-3", value) && value == -3);
    CHECK(parseInt("+5", value) && value == 5);
    CHECK(!parseInt("", value));
    CHECK(!parseInt("   ", value));
    CHECK(!parseInt("abc", value));
    CHECK(!parseInt("4x", value));
    CHECK(!parseInt("1 2", value));
    CHECK(!parseInt("-", value));
    CHECK(!parseInt("3.5", value));
    CHECK(!parseInt("99999999999", value));
}

void testParseUnsigned()
{
    unsigned int value = 0;
    CHECK(parseUnsigned("0", value) && value == 0);
    CHECK(parseUnsigned("4294967295", value) && value == 4294967295u);
    CHECK(!parseUnsigned("4294967296", value));
    CHECK(!parseUnsigned("-1", value));
    CHECK(!parseUnsigned("12a", value));
    CHECK(!parseUnsigned("", value));
}

void testRangeAndTrim()
{
    CHECK(isInRange(10, 10, 50));
    CHECK(isInRange(50, 10, 50));
    CHECK(!isInRange(9, 10, 50));
    CHECK(trim("  hello \t") == "hello");
    CHECK(trim("   ").empty());
}

void testPlayerNameValidation()
{
    CHECK(isValidPlayerName("Maria"));
    CHECK(isValidPlayerName("Team_4 QA"));
    CHECK(!isValidPlayerName(""));
    CHECK(!isValidPlayerName("   "));
    CHECK(!isValidPlayerName("___"));
    CHECK(!isValidPlayerName("ThisNameIsTooLong"));
    CHECK(!isValidPlayerName("bad!name"));
}

// ===========================================================================
// Number Trail
// ===========================================================================

void testTrailCreation()
{
    NumberTrail trail;
    CHECK(!createNumberTrail(trail, 3, 1));
    CHECK(createNumberTrail(trail, 12, 1));
    CHECK(trail.values[0][0] == 0);
    CHECK(trail.visited[0][0]);

    bool* seen = new bool[trail.maxValue + 1]();
    bool distinct = true;
    for (int row = 0; row < trail.size; ++row)
    {
        for (int col = 0; col < trail.size; ++col)
        {
            const int value = trail.values[row][col];
            distinct = distinct && !seen[value];
            seen[value] = true;
        }
    }
    CHECK(distinct);
    delete[] seen;

    destroyNumberTrail(trail);
    CHECK(trail.values == nullptr && trail.visited == nullptr);
}

void testTrailBestStepIsSmallest()
{
    NumberTrail trail;
    createNumberTrail(trail, 10, 5);
    TrailStep best = TrailStep::Right;
    CHECK(findBestTrailStep(trail, best));
    int rowOffset = 0;
    int colOffset = 0;
    getTrailStepOffset(best, rowOffset, colOffset);
    const int bestValue = trail.values[rowOffset][colOffset];
    CHECK(bestValue <= trail.values[0][1]);
    CHECK(bestValue <= trail.values[1][0]);
    CHECK(bestValue <= trail.values[1][1]);
    destroyNumberTrail(trail);
}

void testTrailWrongStepAndGameOver()
{
    NumberTrail trail;
    createNumberTrail(trail, 10, 5);
    TrailStep best = TrailStep::Right;
    findBestTrailStep(trail, best);
    const TrailStep wrong = (best == TrailStep::Right) ? TrailStep::Down : TrailStep::Right;

    CHECK(makeTrailStep(trail, wrong) == TrailResult::Wrong);
    CHECK(trail.mistakes == 1);
    CHECK(trail.row == 0 && trail.col == 0); // a wrong step does not move you
    CHECK(makeTrailStep(trail, wrong) == TrailResult::Wrong);
    CHECK(makeTrailStep(trail, wrong) == TrailResult::GameOver);
    CHECK(trail.lost);
    CHECK(makeTrailStep(trail, best) == TrailResult::GameOver); // game stays over
    destroyNumberTrail(trail);
}

void testTrailEdgeRestrictions()
{
    NumberTrail trail;
    createNumberTrail(trail, 10, 9);
    trail.row = 9;
    trail.col = 0;
    CHECK(isTrailStepPossible(trail, TrailStep::Right));
    CHECK(!isTrailStepPossible(trail, TrailStep::Down));
    CHECK(!isTrailStepPossible(trail, TrailStep::Diagonal));
    CHECK(makeTrailStep(trail, TrailStep::Down) == TrailResult::OutOfBounds);
    CHECK(isReachableFromPlayer(trail, 9, 1));
    CHECK(!isReachableFromPlayer(trail, 8, 1));
    destroyNumberTrail(trail);
}

void testFollowingBestStepsFinishes()
{
    for (const unsigned int seed : TEST_SEEDS)
    {
        NumberTrail trail;
        createNumberTrail(trail, 20, seed);
        TrailStep best = TrailStep::Right;
        int guard = 0;
        while (!trail.finished && findBestTrailStep(trail, best) && guard < 100)
        {
            makeTrailStep(trail, best);
            ++guard;
        }
        CHECK(trail.finished);
        CHECK(trail.mistakes == 0);
        CHECK(trail.moves <= 2 * (trail.size - 1));
        CHECK(calculateTrailScore(trail, 0) == trail.size * 50);
        destroyNumberTrail(trail);
    }
}

void testCountDigits()
{
    CHECK(countDigits(0) == 1);
    CHECK(countDigits(9) == 1);
    CHECK(countDigits(10) == 2);
    CHECK(countDigits(5000) == 4);
    CHECK(countDigits(-42) == 2);
}

// ===========================================================================
// Adventure rules: items, traps, teleporters, Minotaur, fog, time
// ===========================================================================

// A run with every feature switched on.
RunConfig makeFullConfig(int size, unsigned int seed)
{
    RunConfig config;
    config.size = size;
    config.seed = seed;
    config.lives = 3;
    config.minotaurCount = 2;
    config.fogOfWar = true;
    config.powerUps = true;
    config.traps = true;
    config.teleporterPairs = 2;
    config.gemCount = 12;
    return config;
}

// Opens the corridor to the right of (0, 0) so tests can step onto (0, 1).
void openFirstCorridor(GameState& state)
{
    setPassageTile(state.maze, 0, 0, Direction::Right, Tile::Path);
    setCellTile(state.maze, 0, 1, Tile::Path);
    state.items[0][1] = Item::None;
}

bool hasEvent(const GameState& state, EventType type)
{
    for (int i = 0; i < state.eventCount; ++i)
    {
        if (state.events[i].type == type)
        {
            return true;
        }
    }
    return false;
}

void testConfigClamping()
{
    RunConfig config;
    config.size = 99;
    config.lives = 9;
    config.minotaurCount = 7;
    config.teleporterPairs = -2;
    config.gemCount = 500;
    config.minotaurStepSeconds = 0.0f;
    const RunConfig clamped = clampConfig(config);
    CHECK(clamped.size == 50);
    CHECK(clamped.lives == 5);
    CHECK(clamped.minotaurCount == 3);
    CHECK(clamped.teleporterPairs == 0);
    CHECK(clamped.gemCount == 60);
    CHECK(clamped.minotaurStepSeconds >= 0.15f);

    GameState state;
    config.size = 5;
    CHECK(!startRun(state, config)); // an invalid size is rejected, not silently fixed
    CHECK(state.maze.tiles == nullptr);
}

void testItemPlacement()
{
    for (const unsigned int seed : TEST_SEEDS)
    {
        GameState state;
        CHECK(startRun(state, makeFullConfig(30, seed)));
        CHECK(state.gemsTotal == 12);
        CHECK(state.minotaurCount == 2);
        CHECK(getItem(state, 0, 0) == Item::None);
        CHECK(getItem(state, 29, 29) == Item::None);

        int gems = 0;
        int pads = 0;
        bool linksMatch = true;
        for (int row = 0; row < 30; ++row)
        {
            for (int col = 0; col < 30; ++col)
            {
                const Item item = state.items[row][col];
                gems += item == Item::Gem ? 1 : 0;
                if (item == Item::Teleporter)
                {
                    ++pads;
                    const int target = getTeleportTarget(state, row, col);
                    linksMatch = linksMatch && target >= 0 &&
                                 state.teleportLinks[target] == row * 30 + col &&
                                 state.items[target / 30][target % 30] == Item::Teleporter;
                }
                // Items never sit on a key.
                linksMatch = linksMatch && !(item != Item::None && getCellTile(state.maze, row, col) == Tile::Key);
            }
        }
        CHECK(gems == 12);
        CHECK(pads == 4);
        CHECK(linksMatch);
        CHECK(!state.minotaurs[0].hunting);
        endGame(state);
        CHECK(state.items == nullptr && state.minotaurs == nullptr && state.teleportLinks == nullptr);
    }
}

void testPowerUpEffects()
{
    GameState state;
    startRun(state, makeFullConfig(15, 3));
    openFirstCorridor(state);
    setPassageTile(state.maze, 0, 1, Direction::Left, Tile::Path);

    const Item powerUps[] = {Item::Torch, Item::Boots, Item::Hammer, Item::Hourglass, Item::Compass};
    for (const Item item : powerUps)
    {
        state.playerRow = 0;
        state.playerCol = 0;
        state.invulnerableTime = 10.0f; // keep Minotaurs out of this test
        state.items[0][1] = item;
        CHECK(movePlayer(state, Direction::Right) == MoveResult::PickedUpItem);
        CHECK(getItem(state, 0, 1) == Item::None); // picked up
    }
    CHECK(state.torchLevel == 1);
    CHECK(state.bootsTime > 0.0f);
    CHECK(state.hammerCharges == 1);
    CHECK(state.freezeTime > 0.0f);
    CHECK(state.compassTime > 0.0f);
    CHECK(state.powerUpsCollected == 5);
    CHECK(getRevealRadius(state) == 2);

    // Hearts never go above the maximum.
    state.lives = state.maxLives;
    state.playerCol = 0;
    state.items[0][1] = Item::Heart;
    movePlayer(state, Direction::Right);
    CHECK(state.lives == state.maxLives);
    endGame(state);
}

void testGemsAndSealedExit()
{
    RunConfig config;
    config.size = 10;
    config.seed = 11;
    config.gemCount = 5;
    config.requireAllGems = true;
    GameState state;
    startRun(state, config);
    CHECK(!isExitUnlocked(state));

    state.playerRow = 9;
    state.playerCol = 8;
    setPassageTile(state.maze, 9, 8, Direction::Right, Tile::Path);
    state.items[9][8] = Item::None;
    CHECK(movePlayer(state, Direction::Right) == MoveResult::ExitLocked);
    CHECK(!state.won);
    CHECK(hasEvent(state, EventType::ExitLocked));

    state.gemsCollected = state.gemsTotal;
    state.playerCol = 8;
    CHECK(movePlayer(state, Direction::Right) == MoveResult::ReachedExit);
    CHECK(state.won);
    CHECK(calculateScore(state, 10) > 0);
    endGame(state);
}

void testHammerBreaksWalls()
{
    GameState state;
    startGame(state, 10, 1);
    setPassageTile(state.maze, 0, 0, Direction::Right, Tile::Wall);
    CHECK(!useHammer(state, Direction::Right)); // no charges yet

    state.hammerCharges = 1;
    CHECK(!useHammer(state, Direction::Up)); // outer wall is unbreakable
    CHECK(useHammer(state, Direction::Right));
    CHECK(getPassageTile(state.maze, 0, 0, Direction::Right) == Tile::Path);
    CHECK(state.hammerCharges == 0);
    CHECK(state.wallsBroken == 1);
    CHECK(hasEvent(state, EventType::WallBroken));
    CHECK(movePlayer(state, Direction::Right) != MoveResult::BlockedByWall);
    endGame(state);
}

void testTeleporter()
{
    GameState state;
    startGame(state, 10, 1);
    openFirstCorridor(state);
    state.items[0][1] = Item::Teleporter;
    state.items[5][5] = Item::Teleporter;
    state.teleportLinks[1] = 55;
    state.teleportLinks[55] = 1;

    CHECK(movePlayer(state, Direction::Right) == MoveResult::Teleported);
    CHECK(state.playerRow == 5 && state.playerCol == 5);
    CHECK(state.teleportsUsed == 1);
    CHECK(state.visitedCells[5][5]);
    CHECK(hasEvent(state, EventType::Teleport));
    endGame(state);
}

void testSpikeTraps()
{
    GameState state;
    startGame(state, 10, 1);
    openFirstCorridor(state);
    state.items[0][1] = Item::Spikes;

    // Find a moment when the spikes on (0, 1) are up.
    while (!areSpikesUp(state, 0, 1))
    {
        state.elapsed += 0.05;
    }
    CHECK(movePlayer(state, Direction::Right) == MoveResult::Hurt);
    CHECK(state.lives == 2);
    CHECK(state.invulnerableTime > 0.0f);

    updateRun(state, 0.1f); // still invulnerable: no second hit
    CHECK(state.lives == 2);

    // Spikes are up and down in turns.
    bool sawDown = false;
    for (int i = 0; i < 100 && !sawDown; ++i)
    {
        state.elapsed += 0.05;
        sawDown = !areSpikesUp(state, 0, 1);
    }
    CHECK(sawDown);
    endGame(state);
}

void testLosingAllLives()
{
    GameState state;
    startGame(state, 10, 1);
    openFirstCorridor(state);
    state.lives = 1;
    state.items[0][1] = Item::Spikes;
    while (!areSpikesUp(state, 0, 1))
    {
        state.elapsed += 0.05;
    }
    CHECK(movePlayer(state, Direction::Right) == MoveResult::GameOver);
    CHECK(state.lost);
    CHECK(state.lossReason == LossReason::OutOfLives);
    CHECK(hasEvent(state, EventType::Lost));
    CHECK(movePlayer(state, Direction::Left) == MoveResult::GameOver); // no moves after losing
    CHECK(calculateScore(state, 1) == 0);
    endGame(state);
}

void testTimeLimit()
{
    RunConfig config;
    config.size = 10;
    config.timeLimitSeconds = 30;
    GameState state;
    startRun(state, config);
    CHECK(getSecondsLeft(state) == 30);
    updateRun(state, 25.0f);
    CHECK(getSecondsLeft(state) == 5);
    CHECK(hasEvent(state, EventType::TimeWarning));
    CHECK(!state.lost);
    updateRun(state, 6.0f);
    CHECK(state.lost);
    CHECK(state.lossReason == LossReason::OutOfTime);

    GameState untimed;
    startGame(untimed, 10, 1);
    CHECK(getSecondsLeft(untimed) == -1);
    endGame(state);
    endGame(untimed);
}

void testMinotaurHuntsPlayer()
{
    RunConfig config;
    config.size = 12;
    config.seed = 21;
    config.minotaurCount = 1;
    config.minotaurStepSeconds = 0.2f;
    GameState state;
    startRun(state, config);
    const int startDistance = distanceToNearestMinotaur(state);
    CHECK(startDistance > 0);

    // The player waits at the start; sooner or later the Minotaur arrives.
    for (int tick = 0; tick < 5000 && state.timesCaught == 0; ++tick)
    {
        updateRun(state, 0.1f);
    }
    CHECK(state.timesCaught == 1);
    CHECK(state.lives == 2);
    CHECK(hasEvent(state, EventType::Caught) || state.timesCaught == 1);
    // After catching you it returns to its lair.
    CHECK(state.minotaurs[0].row == state.minotaurs[0].spawnRow);
    CHECK(state.minotaurs[0].col == state.minotaurs[0].spawnCol);
    endGame(state);
}

void testMinotaurMovesOnlyThroughCorridors()
{
    RunConfig config = makeFullConfig(20, 8);
    GameState state;
    startRun(state, config);
    state.invulnerableTime = 1000.0f;
    bool legal = true;
    for (int tick = 0; tick < 400; ++tick)
    {
        int beforeRow[3];
        int beforeCol[3];
        for (int i = 0; i < state.minotaurCount; ++i)
        {
            beforeRow[i] = state.minotaurs[i].row;
            beforeCol[i] = state.minotaurs[i].col;
        }
        updateRun(state, 0.1f);
        for (int i = 0; i < state.minotaurCount; ++i)
        {
            const int rowDelta = state.minotaurs[i].row - beforeRow[i];
            const int colDelta = state.minotaurs[i].col - beforeCol[i];
            const int steps = (rowDelta < 0 ? -rowDelta : rowDelta) + (colDelta < 0 ? -colDelta : colDelta);
            if (steps == 1)
            {
                const Direction d = rowDelta == 1 ? Direction::Down
                                    : rowDelta == -1 ? Direction::Up
                                    : colDelta == 1 ? Direction::Right
                                                    : Direction::Left;
                legal = legal && isPassable(state.maze, beforeRow[i], beforeCol[i], d, false);
            }
            legal = legal && steps <= 1;
        }
    }
    CHECK(legal);
    endGame(state);
}

void testHourglassFreezesMinotaur()
{
    RunConfig config;
    config.size = 15;
    config.minotaurCount = 1;
    GameState state;
    startRun(state, config);
    state.freezeTime = 5.0f;
    const int row = state.minotaurs[0].row;
    const int col = state.minotaurs[0].col;
    for (int i = 0; i < 40; ++i)
    {
        updateRun(state, 0.1f); // 4 seconds, still frozen
    }
    CHECK(state.minotaurs[0].row == row && state.minotaurs[0].col == col);
    CHECK(state.freezeTime > 0.0f);
    endGame(state);
}

void testFogOfWar()
{
    RunConfig config;
    config.size = 20;
    config.fogOfWar = true;
    GameState foggy;
    startRun(foggy, config);
    CHECK(countDiscoveredCells(foggy) == 4); // a 2x2 corner around (0, 0)
    foggy.torchLevel = 2;
    revealAround(foggy);
    CHECK(countDiscoveredCells(foggy) == 16);

    config.fogOfWar = false;
    GameState clear;
    startRun(clear, config);
    CHECK(countDiscoveredCells(clear) == 400);
    endGame(foggy);
    endGame(clear);
}

void testHintsCanBeDisabled()
{
    RunConfig config;
    config.size = 10;
    config.hintsAllowed = false;
    GameState state;
    startRun(state, config);
    CHECK(!toggleHint(state));
    CHECK(!state.hintVisible);
    CHECK(state.hintsUsed == 0);
    endGame(state);
}

void testEventsAreReported()
{
    GameState state;
    startGame(state, 10, 1);
    setPassageTile(state.maze, 0, 0, Direction::Right, Tile::Wall);
    movePlayer(state, Direction::Right);
    CHECK(state.eventCount == 1);
    CHECK(state.events[0].type == EventType::Bump);
    clearEvents(state);
    CHECK(state.eventCount == 0);
    for (int i = 0; i < MAX_EVENTS + 10; ++i)
    {
        movePlayer(state, Direction::Right); // never overflows the list
    }
    CHECK(state.eventCount == MAX_EVENTS);

    // Even with a full list, winning is always reported.
    state.playerRow = 9;
    state.playerCol = 8;
    setPassageTile(state.maze, 9, 8, Direction::Right, Tile::Path);
    movePlayer(state, Direction::Right);
    CHECK(state.won);
    CHECK(state.events[MAX_EVENTS - 1].type == EventType::Won);
    endGame(state);
}

// ===========================================================================
// Campaign, challenges, achievements, profiles, saves
// ===========================================================================

void testCampaignLevels()
{
    int previousSize = 0;
    bool allValid = true;
    for (int i = 0; i < LEVEL_COUNT; ++i)
    {
        const LevelDef& level = getLevel(i);
        GameState state;
        allValid = allValid && startRun(state, level.config) && computeParSeconds(state) > 0;
        allValid = allValid && level.config.size >= previousSize; // levels get bigger
        previousSize = level.config.size;
        Path route;
        allValid = allValid && findShortestPath(state.maze, 0, 0, false, route); // always solvable
        destroyPath(route);
        endGame(state);
    }
    CHECK(allValid);
    CHECK(getLevel(LEVEL_COUNT - 1).config.size == 50);
    CHECK(getLevel(-5).name == getLevel(0).name);
    CHECK(std::string(getWorldName(0)) == "Neon Grid");
}

void testChallenges()
{
    bool allValid = true;
    for (int i = 0; i < CHALLENGE_COUNT; ++i)
    {
        GameState state;
        allValid = allValid && startRun(state, makeChallengeConfig(static_cast<ChallengeId>(i), 77));
        endGame(state);
    }
    CHECK(allValid);
    CHECK(makeChallengeConfig(ChallengeId::IronRunner, 1).lives == 1);
    CHECK(!makeChallengeConfig(ChallengeId::IronRunner, 1).hintsAllowed);
    CHECK(makeChallengeConfig(ChallengeId::TimeAttack, 1).timeLimitSeconds == 90);
    CHECK(makeDailySeed(2026, 11, 6) == makeDailySeed(2026, 11, 6));
    CHECK(makeDailySeed(2026, 11, 6) != makeDailySeed(2026, 11, 7));
    CHECK(makeDateNumber(2026, 11, 6) == 20261106);
}

void testStarRating()
{
    RunResult result;
    result.parSeconds = 60;
    CHECK(computeStars(result) == 0);
    result.won = true;
    result.seconds = 61;
    CHECK(computeStars(result) == 1);
    result.seconds = 60;
    result.gemsTotal = 3;
    result.gemsCollected = 3;
    CHECK(computeStars(result) == 3);
    result.hintsUsed = 1;
    CHECK(computeStars(result) == 2);
    result.hintsUsed = 0;
    result.livesLost = 1;
    CHECK(computeStars(result) == 2);
    result.livesLost = 0;
    result.gemsCollected = 2;
    CHECK(computeStars(result) == 2);
}

void testStatisticsAndAchievements()
{
    Profile profile;
    profile.name = "Tester";
    int earned[ACHIEVEMENT_COUNT];
    CHECK(updateAchievements(profile, earned) == 0);

    RunResult run;
    run.won = true;
    run.size = 50;
    run.moves = 100;
    run.optimalMoves = 100;
    run.seconds = 50;
    run.minotaurCount = 1;
    recordRun(profile.stats, run, RunMode::Challenge, static_cast<int>(ChallengeId::TimeAttack), ThemeId::Lava);
    CHECK(profile.stats.mazesCompleted == 1);
    CHECK(profile.stats.perfectRuns == 1);
    CHECK(profile.stats.biggestMaze == 50);
    CHECK(profile.stats.fastestBigMaze == 50);
    CHECK(profile.stats.timeAttackWins == 1);
    CHECK(profile.stats.minotaurEscapes == 1);

    const int count = updateAchievements(profile, earned);
    CHECK(count >= 6); // First Steps, Perfect Path, Speed Demon, Untouchable, Giant Slayer, Clear Mind, Beat the Clock
    CHECK(profile.achievements[0] && profile.achievements[10] && profile.achievements[13]);
    CHECK(updateAchievements(profile, earned) == 0); // each one is earned only once

    run.won = false;
    recordRun(profile.stats, run, RunMode::FreePlay, 0, ThemeId::Ice);
    CHECK(profile.stats.mazesLost == 1);
    CHECK(profile.stats.mazesCompleted == 1);

    int current = 0;
    int target = 0;
    getAchievementProgress(18, profile.stats, 0, current, target);
    CHECK(current == 2 && target == 5); // two themes played

    recordTrail(profile.stats, true, 0, 30, 16);
    CHECK(profile.stats.trailsPerfect == 1 && profile.stats.bestTrailCombo == 16);
    CHECK(updateAchievements(profile, earned) == 2); // Trail Blazer + Combo King
}

void testCampaignUnlocking()
{
    Profile profile;
    CHECK(isLevelUnlocked(profile, 0));
    CHECK(!isLevelUnlocked(profile, 1));
    CHECK(countUnlockedLevels(profile) == 1);
    profile.levelStars[0] = 2;
    profile.levelStars[1] = 1;
    CHECK(countUnlockedLevels(profile) == 3);
    CHECK(countTotalStars(profile) == 3);
    CHECK(!isLevelUnlocked(profile, LEVEL_COUNT));
}

void testProfileRoundTrip()
{
    Profile profile;
    profile.name = "Ana QA";
    profile.stats.mazesCompleted = 12;
    profile.stats.gemsCollected = 345;
    profile.stats.themesPlayedMask = 0x15;
    profile.achievements[3] = true;
    profile.achievements[19] = true;
    profile.levelStars[0] = 3;
    profile.levelStars[7] = 2;
    profile.levelBestScore[7] = 4321;
    profile.challengeBest[2] = 999;
    profile.dailyBestDate = 20261106;
    profile.settings.theme = 3;
    profile.settings.musicVolume = 17;
    profile.settings.crtEffect = false;
    profile.settings.reduceMotion = true;

    Profile loaded;
    CHECK(deserializeProfile(serializeProfile(profile), loaded));
    CHECK(loaded.name == "Ana QA");
    CHECK(loaded.stats.mazesCompleted == 12 && loaded.stats.gemsCollected == 345);
    CHECK(loaded.stats.themesPlayedMask == 0x15);
    CHECK(loaded.achievements[3] && loaded.achievements[19] && !loaded.achievements[4]);
    CHECK(loaded.levelStars[0] == 3 && loaded.levelStars[7] == 2 && loaded.levelBestScore[7] == 4321);
    CHECK(loaded.challengeBest[2] == 999 && loaded.dailyBestDate == 20261106);
    CHECK(loaded.settings.theme == 3 && loaded.settings.musicVolume == 17);
    CHECK(!loaded.settings.crtEffect && loaded.settings.reduceMotion);
    CHECK(profileFileName("Ana QA") == "Ana_QA.profile");
}

void testProfileRejectsBadFiles()
{
    Profile profile;
    profile.name = "Keep";
    CHECK(!deserializeProfile("", profile));
    CHECK(!deserializeProfile("not a profile\nname=X\n", profile));
    CHECK(!deserializeProfile("LABYRINTH_PROFILE 1\nname=bad!name\n", profile));
    CHECK(profile.name == "Keep"); // unchanged after a failed load

    // Out-of-range numbers are clamped, unknown lines ignored.
    CHECK(deserializeProfile(
        "LABYRINTH_PROFILE 1\nname=Zed\nsetting.musicVolume=900\nsetting.fieldOfView=5\nwhat=ever\nlevelStars=9 -1\n",
        profile));
    CHECK(profile.settings.musicVolume == 100);
    CHECK(profile.settings.fieldOfView == 60);
    CHECK(profile.levelStars[0] == 3 && profile.levelStars[1] == 0);
}

void testLeaderboardsRoundTrip()
{
    LeaderboardSet set;
    ScoreEntry entry;
    entry.name = "Max Power";
    entry.mazeSize = 25;
    entry.moves = 300;
    entry.seconds = 95;
    entry.score = 2500;
    addToLeaderboard(set.boards[static_cast<int>(BoardId::Daily)], entry);
    entry.name = "Lee";
    entry.score = 1200;
    addToLeaderboard(set.boards[static_cast<int>(BoardId::NumberTrail)], entry);

    LeaderboardSet loaded;
    CHECK(deserializeLeaderboards(serializeLeaderboards(set), loaded));
    const Leaderboard& daily = loaded.boards[static_cast<int>(BoardId::Daily)];
    CHECK(daily.count == 1 && daily.entries[0].name == "Max Power" && daily.entries[0].score == 2500);
    CHECK(daily.entries[0].seconds == 95 && daily.entries[0].mazeSize == 25);
    CHECK(loaded.boards[static_cast<int>(BoardId::NumberTrail)].count == 1);
    CHECK(loaded.boards[static_cast<int>(BoardId::FreePlay)].count == 0);
    CHECK(!deserializeLeaderboards("garbage", loaded));
    CHECK(boardForChallenge(ChallengeId::GemRush) == BoardId::GemRush);
}

// Two states are the same game if every tile, item and number matches.
bool sameGame(const GameState& a, const GameState& b)
{
    if (a.maze.size != b.maze.size || a.playerRow != b.playerRow || a.playerCol != b.playerCol ||
        a.moves != b.moves || a.lives != b.lives || a.gemsCollected != b.gemsCollected ||
        a.keysHeld != b.keysHeld || a.minotaurCount != b.minotaurCount || a.won != b.won ||
        a.lost != b.lost || a.facing != b.facing || a.optimalMoves != b.optimalMoves)
    {
        return false;
    }
    for (int row = 0; row < a.maze.tileCount; ++row)
    {
        for (int col = 0; col < a.maze.tileCount; ++col)
        {
            if (a.maze.tiles[row][col] != b.maze.tiles[row][col])
            {
                return false;
            }
        }
    }
    for (int row = 0; row < a.maze.size; ++row)
    {
        for (int col = 0; col < a.maze.size; ++col)
        {
            if (a.items[row][col] != b.items[row][col] || a.discovered[row][col] != b.discovered[row][col] ||
                a.visitedCells[row][col] != b.visitedCells[row][col])
            {
                return false;
            }
        }
    }
    for (int i = 0; i < a.minotaurCount; ++i)
    {
        if (a.minotaurs[i].row != b.minotaurs[i].row || a.minotaurs[i].col != b.minotaurs[i].col)
        {
            return false;
        }
    }
    return true;
}

void testSaveGameRoundTrip()
{
    GameState original;
    startRun(original, makeFullConfig(25, 4242));
    original.invulnerableTime = 1000.0f;
    // Play a little: walk along the hint route and let time pass.
    Path route;
    findShortestPath(original.maze, 0, 0, false, route);
    for (int i = 1; i < route.length && i < 15; ++i)
    {
        const int rowDelta = route.rows[i] - original.playerRow;
        const int colDelta = route.cols[i] - original.playerCol;
        const Direction d = rowDelta == 1 ? Direction::Down
                            : rowDelta == -1 ? Direction::Up
                            : colDelta == 1 ? Direction::Right
                                            : Direction::Left;
        movePlayer(original, d);
        updateRun(original, 0.13f);
    }
    destroyPath(route);

    SaveMeta meta;
    meta.mode = 1;
    meta.index = 4;
    meta.theme = 2;
    const std::string text = serializeRun(original, meta);

    GameState loaded;
    SaveMeta loadedMeta;
    CHECK(deserializeRun(text, loaded, loadedMeta));
    CHECK(loadedMeta.mode == 1 && loadedMeta.index == 4 && loadedMeta.theme == 2);
    CHECK(sameGame(original, loaded));
    CHECK(serializeRun(loaded, loadedMeta) == text); // saving again gives the same file

    // Both games continue identically - even the Minotaurs' random choices.
    for (int i = 0; i < 200; ++i)
    {
        updateRun(original, 0.1f);
        updateRun(loaded, 0.1f);
    }
    CHECK(sameGame(original, loaded));
    endGame(original);
    endGame(loaded);
}

void testSaveGameRejectsCorruptFiles()
{
    GameState state;
    SaveMeta meta;
    CHECK(!deserializeRun("", state, meta));
    CHECK(!deserializeRun("LABYRINTH_SAVE 1\nmeta 0 0 0\nconfig 7 1 3 0 0.5 0 0 0 0 0 0 0 1\n", state, meta));
    CHECK(state.maze.tiles == nullptr);

    GameState good;
    startRun(good, makeFullConfig(12, 5));
    std::string text = serializeRun(good, meta);
    const std::size_t tiles = text.find("tiles\n");
    text[tiles + 6] = 'X'; // corrupt one tile
    CHECK(!deserializeRun(text, state, meta));
    CHECK(state.maze.tiles == nullptr && state.items == nullptr);
    endGame(good);
}

void testTrailCombos()
{
    NumberTrail trail;
    createNumberTrail(trail, 10, 5);
    registerTrailCombo(trail, TrailResult::Correct, 0.5f);
    CHECK(trail.combo == 1 && trail.bonusPoints == 0);
    registerTrailCombo(trail, TrailResult::Correct, 0.5f);
    registerTrailCombo(trail, TrailResult::Correct, 1.0f);
    CHECK(trail.combo == 3);
    CHECK(trail.bonusPoints == 10 + 15);
    registerTrailCombo(trail, TrailResult::Correct, 5.0f); // too slow: combo restarts
    CHECK(trail.combo == 1 && trail.bestCombo == 3);
    registerTrailCombo(trail, TrailResult::OutOfBounds, 0.1f);
    CHECK(trail.combo == 1);
    registerTrailCombo(trail, TrailResult::Wrong, 0.1f);
    CHECK(trail.combo == 0);
    trail.finished = true;
    CHECK(calculateTrailScore(trail, 0) == 10 * 50 + 25);
    destroyNumberTrail(trail);
}


// ===========================================================================
// Version 2.1: difficulty, balance and the gauntlet (T60 - T68)
// ===========================================================================

void testDifficultyLevels()
{
    CHECK(std::string(getDifficultyName(0)) == "Easy");
    CHECK(std::string(getDifficultyName(3)) == "Expert");
    CHECK(std::string(getDifficultyName(99)) == "Expert"); // clamped

    int previousPar = 1 << 30;
    int previousStepPar = 1 << 30;
    float previousStep = 100.0f;
    for (int difficulty = 0; difficulty < DIFFICULTY_COUNT; ++difficulty)
    {
        RunConfig config = makeFullConfig(20, 77);
        config.difficulty = difficulty;
        GameState state;
        CHECK(startRun(state, config));
        CHECK(state.config.difficulty == difficulty);
        const int par = computeParSeconds(state);
        const int stepPar = computeStepPar(state);
        CHECK(par < previousPar); // harder = less time
        if (difficulty == 0)
        {
            CHECK(stepPar == 0); // Easy has no step limit
        }
        else
        {
            CHECK(stepPar >= state.tourMoves); // always reachable
            CHECK(stepPar < previousStepPar); // harder = fewer steps
            previousStepPar = stepPar;
        }
        CHECK(getMinotaurStepSeconds(state) < previousStep); // harder = faster Minotaur
        previousStep = getMinotaurStepSeconds(state);
        previousPar = par;
        endGame(state);
    }

    // Same maze for every difficulty: only the targets change.
    RunConfig easy = makeFullConfig(15, 5);
    RunConfig expert = easy;
    easy.difficulty = 0;
    expert.difficulty = 3;
    GameState a;
    GameState b;
    startRun(a, easy);
    startRun(b, expert);
    bool sameTiles = true;
    for (int row = 0; row < a.maze.tileCount; ++row)
    {
        for (int col = 0; col < a.maze.tileCount; ++col)
        {
            sameTiles = sameTiles && a.maze.tiles[row][col] == b.maze.tiles[row][col];
        }
    }
    CHECK(sameTiles);
    a.won = b.won = true;
    CHECK(calculateScore(b, 30) > calculateScore(a, 30)); // Expert scores more
    endGame(a);
    endGame(b);
}

void testStepParAndStars()
{
    RunResult result;
    result.won = true;
    result.seconds = 50;
    result.parSeconds = 60;
    result.moves = 120;
    result.stepPar = 100;
    CHECK(computeStars(result) == 2); // too many steps for 3 stars
    result.moves = 100;
    CHECK(computeStars(result) == 3);
    result.stepPar = 0; // Easy: no step limit
    result.moves = 999;
    CHECK(computeStars(result) == 3);
}

void testTourMoves()
{
    RunConfig config;
    config.size = 20;
    config.seed = 9;
    GameState state;
    startRun(state, config);
    CHECK(state.tourMoves == state.optimalMoves); // no gems: the tour is the route
    endGame(state);

    config.gemCount = 10;
    startRun(state, config);
    CHECK(state.gemsTotal == 10);
    CHECK(state.tourMoves > state.optimalMoves);
    CHECK(computeTourMoves(state, 0, 0) == state.tourMoves);
    endGame(state);
}

void testHammerIsRare()
{
    for (const int size : TEST_SIZES)
    {
        for (const unsigned int seed : TEST_SEEDS)
        {
            GameState state;
            startRun(state, makeFullConfig(size, seed));
            int hammers = 0;
            for (int row = 0; row < size; ++row)
            {
                for (int col = 0; col < size; ++col)
                {
                    hammers += state.items[row][col] == Item::Hammer ? 1 : 0;
                }
            }
            CHECK(hammers <= 1 + size / 25);
            endGame(state);
        }
    }

    // Every broken wall costs points.
    GameState state;
    startRun(state, makeFullConfig(15, 3));
    state.won = true;
    const int before = calculateScore(state, 20);
    state.wallsBroken = 2;
    CHECK(calculateScore(state, 20) == before - 2 * getHammerCost());
    endGame(state);
}

void testNoHeartsWithOneLife()
{
    const RunConfig configs[] = {getLevel(11).config, makeChallengeConfig(ChallengeId::IronRunner, 1),
                                 makeChallengeConfig(ChallengeId::IronRunner, 2)};
    for (const RunConfig& config : configs)
    {
        CHECK(config.lives == 1);
        for (const unsigned int seed : TEST_SEEDS)
        {
            RunConfig seeded = config;
            seeded.seed = seed;
            GameState state;
            startRun(state, seeded);
            bool heart = false;
            for (int row = 0; row < state.maze.size; ++row)
            {
                for (int col = 0; col < state.maze.size; ++col)
                {
                    heart = heart || state.items[row][col] == Item::Heart;
                }
            }
            CHECK(!heart);
            endGame(state);
        }
    }
    // Crystal Rush asks for fewer crystals now.
    CHECK(getLevel(10).config.gemCount == 6 && getLevel(10).config.requireAllGems);
}

void testTeleporterPadsInDeadEnds()
{
    for (const unsigned int seed : TEST_SEEDS)
    {
        GameState state;
        startRun(state, makeFullConfig(30, seed));
        for (int row = 0; row < 30; ++row)
        {
            for (int col = 0; col < 30; ++col)
            {
                if (state.items[row][col] == Item::Teleporter)
                {
                    CHECK(isDeadEnd(state.maze, row, col));
                }
            }
        }
        endGame(state);
    }

    // Teleporting puts the player ON the twin pad, and stepping off it leads
    // back into the maze, not onto another pad.
    GameState state;
    startRun(state, makeFullConfig(30, 42));
    int padRow = -1;
    int padCol = -1;
    for (int row = 0; row < 30 && padRow < 0; ++row)
    {
        for (int col = 0; col < 30; ++col)
        {
            if (state.items[row][col] == Item::Teleporter)
            {
                padRow = row;
                padCol = col;
                break;
            }
        }
    }
    CHECK(padRow >= 0);
    // Stand next to the pad, on its only open side, and step in.
    for (const Direction direction : {Direction::Up, Direction::Down, Direction::Left, Direction::Right})
    {
        if (!isPassable(state.maze, padRow, padCol, direction, true))
        {
            continue;
        }
        int rowOffset = 0;
        int colOffset = 0;
        getDirectionOffset(direction, rowOffset, colOffset);
        state.playerRow = padRow + rowOffset;
        state.playerCol = padCol + colOffset;
        state.items[state.playerRow][state.playerCol] = Item::None;
        const Direction back = direction == Direction::Up     ? Direction::Down
                               : direction == Direction::Down ? Direction::Up
                               : direction == Direction::Left ? Direction::Right
                                                              : Direction::Left;
        state.invulnerableTime = 100.0f;
        if (getPassageTile(state.maze, state.playerRow, state.playerCol, back) == Tile::Door)
        {
            setPassageTile(state.maze, state.playerRow, state.playerCol, back, Tile::Path);
        }
        CHECK(movePlayer(state, back) == MoveResult::Teleported);
        const int target = getTeleportTarget(state, padRow, padCol);
        CHECK(state.playerRow * 30 + state.playerCol == target);
        CHECK(getItem(state, state.playerRow, state.playerCol) == Item::Teleporter);
        break;
    }
    endGame(state);
}

void testCompassShowsFreeRoute()
{
    RunConfig config;
    config.size = 15;
    config.seed = 11;
    config.gemCount = 5;
    config.powerUps = true;
    GameState state;
    startRun(state, config);
    openFirstCorridor(state);
    state.items[0][1] = Item::Compass;
    movePlayer(state, Direction::Right);
    CHECK(state.compassTime > 0.0f);
    CHECK(state.hintsUsed == 0); // the compass is free

    int marked = 0;
    for (int row = 0; row < state.maze.tileCount; ++row)
    {
        for (int col = 0; col < state.maze.tileCount; ++col)
        {
            marked += state.hintTiles[row][col] ? 1 : 0;
        }
    }
    CHECK(marked > 0);

    // It leads to the nearest gem while gems are left.
    int row = 0;
    int col = 0;
    CHECK(getGuideTarget(state, row, col));
    CHECK(getItem(state, row, col) == Item::Gem);
    CHECK(state.hintTiles[cellToTile(row)][cellToTile(col)]);

    // When it runs out, the route disappears.
    updateRun(state, 30.0f);
    bool anyMarked = false;
    for (int r = 0; r < state.maze.tileCount; ++r)
    {
        for (int c = 0; c < state.maze.tileCount; ++c)
        {
            anyMarked = anyMarked || state.hintTiles[r][c];
        }
    }
    CHECK(!anyMarked);
    CHECK(getGuideTarget(state, row, col) && row == 14 && col == 14); // the exit
    endGame(state);
}

void testMinotaurGauntlet()
{
    const RunConfig config = makeChallengeConfig(ChallengeId::MinotaurHunt, 5);
    CHECK(config.minotaurChase && config.minotaurCount == 3);
    GameState state;
    CHECK(startRun(state, config));
    for (int i = 0; i < state.minotaurCount; ++i)
    {
        CHECK(state.minotaurs[i].row == 0 && state.minotaurs[i].col == 0);
        CHECK(!state.minotaurs[i].released);
    }
    // Waiting at the entrance with sleeping Minotaurs is safe.
    CHECK(distanceToNearestMinotaur(state) == -1);
    updateRun(state, 0.5f);
    CHECK(state.lives == 3);

    // Walk ahead a few steps along the route, then the first one is let loose.
    Path route;
    findShortestPath(state.maze, 0, 0, false, route);
    for (int i = 1; i < 6 && i < route.length; ++i)
    {
        state.playerRow = route.rows[i];
        state.playerCol = route.cols[i];
    }
    destroyPath(route);
    clearEvents(state);
    updateRun(state, 4.0f);
    bool releasedEvent = false;
    for (int i = 0; i < state.eventCount; ++i)
    {
        releasedEvent = releasedEvent || state.events[i].type == EventType::MinotaurReleased;
    }
    CHECK(releasedEvent);
    CHECK(state.minotaurs[0].released && state.minotaurs[0].hunting);
    CHECK(!state.minotaurs[1].released);

    // It chases from behind and catches a player who stands still.
    bool caught = false;
    for (int i = 0; i < 200 && !caught; ++i)
    {
        updateRun(state, 0.1f);
        caught = state.timesCaught > 0;
    }
    CHECK(caught);
    endGame(state);
}

void testAdminUnlock()
{
    CHECK(isAdminCode("MINOTAUR"));
    CHECK(isAdminCode("minotaur"));
    CHECK(!isAdminCode("minotaurs"));
    CHECK(!isAdminCode(""));

    Profile profile;
    profile.name = "Admin";
    CHECK(!isLevelUnlocked(profile, LEVEL_COUNT - 1));
    unlockEverything(profile);
    CHECK(profile.admin);
    CHECK(countTotalStars(profile) == LEVEL_COUNT * 3);
    CHECK(isLevelUnlocked(profile, LEVEL_COUNT - 1));
    for (int i = 0; i < ACHIEVEMENT_COUNT; ++i)
    {
        CHECK(profile.achievements[i]);
    }

    // Star Collector needs every star.
    int current = 0;
    int target = 0;
    getAchievementProgress(19, PlayerStats(), 30, current, target);
    CHECK(target == LEVEL_COUNT * 3 && current == 30);
    CHECK(!isAchievementEarned(19, PlayerStats(), 44));
    CHECK(isAchievementEarned(19, PlayerStats(), 45));

    Profile loaded;
    CHECK(deserializeProfile(serializeProfile(profile), loaded));
    CHECK(loaded.admin && countTotalStars(loaded) == 45);
}


// ===========================================================================
// Map maker (T69 - T74)
// ===========================================================================

// A small hand-made map: a straight corridor along the top row, then down
// the right column to the exit.
CustomMap makeCorridorMap()
{
    CustomMap map;
    createBlankMap(map, 10, false);
    map.name = "Test Corridor";
    map.author = "QA";
    for (int col = 0; col < 9; ++col)
    {
        setMapPassage(map, 1, cellToTile(col) + 1, Tile::Path);
    }
    for (int row = 0; row < 9; ++row)
    {
        setMapPassage(map, cellToTile(row) + 1, cellToTile(9), Tile::Path);
    }
    return map;
}

void testMapEditingRules()
{
    CustomMap map;
    CHECK(!createBlankMap(map, 9, false));
    CHECK(createBlankMap(map, 12, false));
    CHECK(getMapTileCount(map) == 25);
    CHECK(getMapTile(map, 0, 1) == Tile::Path);   // entrance
    CHECK(getMapTile(map, 24, 23) == Tile::Path); // exit
    CHECK(getMapTile(map, 1, 1) == Tile::Path);   // a cell
    CHECK(getMapTile(map, 1, 2) == Tile::Wall);   // a passage

    CHECK(!setMapPassage(map, 0, 3, Tile::Path)); // outer wall
    CHECK(!setMapPassage(map, 2, 2, Tile::Path)); // pillar
    CHECK(!setMapPassage(map, 1, 1, Tile::Wall)); // a cell is not a passage
    CHECK(setMapPassage(map, 1, 2, Tile::Door));
    CHECK(!setMapPassage(map, 1, 2, Tile::Key));

    CHECK(!setMapItem(map, 0, 0, Item::Gem)); // start
    CHECK(!setMapItem(map, 11, 11, Item::Gem)); // exit
    CHECK(setMapItem(map, 3, 3, Item::Gem));
    CHECK(setMapKey(map, 3, 3, true)); // replaces the gem
    CHECK(getMapItem(map, 3, 3) == Item::None && getMapTile(map, 7, 7) == Tile::Key);
    CHECK(setMapItem(map, 3, 3, Item::Hammer)); // replaces the key
    CHECK(getMapTile(map, 7, 7) == Tile::Path);

    // Minotaurs: at most three, never on the start.
    CHECK(!toggleMapMinotaur(map, 0, 0));
    CHECK(toggleMapMinotaur(map, 5, 5) && toggleMapMinotaur(map, 6, 6) && toggleMapMinotaur(map, 7, 7));
    CHECK(!toggleMapMinotaur(map, 8, 8));
    CHECK(toggleMapMinotaur(map, 5, 5)); // removed again
    CHECK(map.minotaurCells.size() == 2);
}

void testMapTeleporterPairing()
{
    CustomMap map;
    createBlankMap(map, 10, true);
    CHECK(setMapItem(map, 2, 2, Item::Teleporter));
    CHECK(countUnpairedPads(map) == 1);
    CHECK(setMapItem(map, 7, 7, Item::Teleporter));
    CHECK(countUnpairedPads(map) == 0);
    CHECK(map.links[2 * 10 + 2] == 7 * 10 + 7 && map.links[7 * 10 + 7] == 2 * 10 + 2);
    CHECK(setMapItem(map, 7, 7, Item::None)); // removing one leaves its twin waiting
    CHECK(countUnpairedPads(map) == 1 && map.links[2 * 10 + 2] == -1);
    CHECK(setMapItem(map, 4, 4, Item::Teleporter));
    CHECK(map.links[2 * 10 + 2] == 4 * 10 + 4);
    for (int i = 0; i < 4; ++i)
    {
        setMapItem(map, 8, i, Item::Teleporter);
    }
    CHECK(countMapItems(map, Item::Teleporter) == 6);
    CHECK(!setMapItem(map, 8, 5, Item::Teleporter)); // at most three pairs
}

void testMapProblems()
{
    CustomMap map;
    createBlankMap(map, 10, false); // every cell walled in
    CHECK(!findMapProblems(map).empty());
    GameState state;
    CHECK(!startCustomRun(state, map)); // an impossible map cannot even be tested

    map = makeCorridorMap();
    CHECK(findMapProblems(map).empty());

    // A door on the only route needs a reachable key.
    setMapPassage(map, 1, cellToTile(4) + 1, Tile::Door);
    CHECK(findMapProblems(map).size() == 1);
    setMapKey(map, 0, 2, true);
    CHECK(findMapProblems(map).empty());

    setMapItem(map, 0, 6, Item::Teleporter);
    CHECK(findMapProblems(map).size() == 1); // pad without a twin
    setMapItem(map, 0, 6, Item::None);

    map.config.requireAllGems = true;
    CHECK(findMapProblems(map).size() == 1); // sealed, but no gems
    setMapItem(map, 5, 5, Item::Gem);        // walled in: unreachable
    CHECK(findMapProblems(map).size() == 1);
    setMapItem(map, 5, 5, Item::None);
    setMapItem(map, 0, 8, Item::Gem);
    CHECK(findMapProblems(map).empty());
}

void testMapVerification()
{
    CustomMap map = makeCorridorMap();
    CHECK(!isMapVerified(map));

    // Play the map like the test play does, and beat it.
    GameState state;
    CHECK(startCustomRun(state, map));
    CHECK(state.optimalMoves == 18);
    for (int i = 0; i < 9; ++i)
    {
        movePlayer(state, Direction::Right);
    }
    for (int i = 0; i < 9; ++i)
    {
        movePlayer(state, Direction::Down);
    }
    CHECK(state.won);
    markMapVerified(map, state.moves, static_cast<int>(state.elapsed));
    endGame(state);
    CHECK(isMapVerified(map));

    // Renaming is fine, changing the maze is not.
    map.name = "Renamed";
    CHECK(isMapVerified(map));
    setMapItem(map, 3, 3, Item::Gem);
    CHECK(!isMapVerified(map));
}

void testMapFileRoundTrip()
{
    CustomMap map = makeCorridorMap();
    map.theme = 3;
    map.config.lives = 2;
    map.config.fogOfWar = true;
    map.config.minotaurStepSeconds = 0.45f;
    map.config.timeLimitSeconds = 120;
    setMapItem(map, 0, 3, Item::Gem);
    setMapItem(map, 0, 5, Item::Spikes);
    setMapItem(map, 2, 2, Item::Teleporter);
    setMapItem(map, 0, 7, Item::Teleporter);
    setMapKey(map, 4, 4, true);
    toggleMapMinotaur(map, 9, 2);
    markMapVerified(map, 40, 33);

    const std::string text = serializeMap(map);
    CustomMap loaded;
    CHECK(deserializeMap(text, loaded));
    CHECK(loaded.name == map.name && loaded.author == "QA" && loaded.theme == 3);
    CHECK(loaded.config.lives == 2 && loaded.config.fogOfWar && loaded.config.timeLimitSeconds == 120);
    CHECK(loaded.tiles == map.tiles && loaded.items == map.items && loaded.links == map.links);
    CHECK(loaded.minotaurCells == map.minotaurCells);
    CHECK(isMapVerified(loaded) && loaded.verifiedMoves == 40);
    CHECK(serializeMap(loaded) == text);

    // The run built from the map has the same contents.
    GameState state;
    CHECK(startCustomRun(state, loaded));
    CHECK(state.gemsTotal == 1 && state.minotaurCount == 1 && state.config.traps);
    CHECK(getItem(state, 0, 5) == Item::Spikes);
    CHECK(getTeleportTarget(state, 2, 2) == 7);
    CHECK(state.minotaurs[0].row == 9 && state.minotaurs[0].col == 2);
    CHECK(getCellTile(state.maze, 4, 4) == Tile::Key);
    endGame(state);

    CHECK(mapFileName("Spiral of Doom") == "Spiral_of_Doom.lmap");
    CHECK(isValidMapName("Spiral of Doom-2"));
    CHECK(!isValidMapName(""));
    CHECK(!isValidMapName(" lead"));
    CHECK(!isValidMapName("bad/name"));
    CHECK(!isValidMapName("abcdefghijklmnopqrstuvwxy")); // 25 characters
}

void testMapLoaderRejectsBadFiles()
{
    CustomMap map;
    CHECK(!deserializeMap("", map));
    CHECK(!deserializeMap("LABYRINTH_SAVE 2\n", map));

    const std::string good = serializeMap(makeCorridorMap());
    CHECK(deserializeMap(good, map));

    std::string text = good;
    text[text.find("tiles\n") + 6] = '.'; // opens the outer wall
    CHECK(!deserializeMap(text, map));

    text = good;
    text.replace(text.find("config 10"), 9, "config 99");
    CHECK(!deserializeMap(text, map));

    text = good;
    text.replace(text.find("links 0"), 7, "links 1\n0 5");
    CHECK(!deserializeMap(text, map)); // a link between cells that are no pads

    // Editing the file by hand removes the "beaten" mark.
    CustomMap verified = makeCorridorMap();
    markMapVerified(verified, 18, 10);
    text = serializeMap(verified);
    const std::size_t items = text.find("items\n") + 6;
    text[items + 4] = 'g';
    CHECK(deserializeMap(text, map));
    CHECK(!isMapVerified(map));
}

// ===========================================================================
// Test runner
// ===========================================================================

struct TestCase
{
    const char* name;
    void (*function)();
};

const TestCase TEST_CASES[] = {
    {"Maze size validation", testMazeSizeValidation},
    {"createMaze rejects invalid sizes", testCreateMazeRejectsInvalidSize},
    {"Maze memory is allocated and released", testMazeAllocationAndCleanup},
    {"Every cell is reachable", testEveryCellIsReachable},
    {"Maze is perfect before doors (N*N-1 passages)", testMazeIsPerfectBeforeDoors},
    {"Outer wall is closed except entrance/exit", testOuterWallIsClosed},
    {"Same seed gives the same maze", testSameSeedGivesSameMaze},
    {"Different seeds give different mazes", testDifferentSeedsGiveDifferentMazes},
    {"Special doors are shortcuts", testDoorsAreShortcuts},
    {"Keys are placed correctly", testKeyPlacement},
    {"Shortest path is valid", testShortestPathIsValid},
    {"New game starts at (0, 0)", testStartGameState},
    {"Walls block movement", testMovementBlockedByWall},
    {"Player moves through corridors", testMovementThroughCorridor},
    {"Player cannot leave the maze", testCannotLeaveMaze},
    {"Locked door needs a key", testLockedDoorNeedsKey},
    {"Keys are picked up", testKeyPickup},
    {"Reaching the exit wins", testReachingExitWins},
    {"Walking the hint route wins", testWalkingTheHintRouteWins},
    {"Hint overlay on/off", testHintOverlay},
    {"Score rules", testScoreRules},
    {"Viewport (camera) calculation", testViewportCalculation},
    {"Leaderboard ordering", testLeaderboardOrdering},
    {"parseInt validation", testParseInt},
    {"parseUnsigned validation", testParseUnsigned},
    {"Range check and trim", testRangeAndTrim},
    {"Player name validation", testPlayerNameValidation},
    {"Number trail creation", testTrailCreation},
    {"Number trail: best step is the smallest", testTrailBestStepIsSmallest},
    {"Number trail: wrong steps and game over", testTrailWrongStepAndGameOver},
    {"Number trail: edge restrictions", testTrailEdgeRestrictions},
    {"Number trail: following the smallest numbers finishes", testFollowingBestStepsFinishes},
    {"countDigits", testCountDigits},
    {"Run settings are clamped", testConfigClamping},
    {"Gems, power-ups, traps and teleporters are placed", testItemPlacement},
    {"Power-up effects", testPowerUpEffects},
    {"Gems and the sealed exit", testGemsAndSealedExit},
    {"Hammer breaks inner walls only", testHammerBreaksWalls},
    {"Teleporters move the player", testTeleporter},
    {"Spike traps hurt only when up", testSpikeTraps},
    {"Losing all lives ends the game", testLosingAllLives},
    {"Time limit", testTimeLimit},
    {"Minotaur hunts the player", testMinotaurHuntsPlayer},
    {"Minotaurs never walk through walls", testMinotaurMovesOnlyThroughCorridors},
    {"Hourglass freezes the Minotaur", testHourglassFreezesMinotaur},
    {"Fog of war", testFogOfWar},
    {"Hints can be disabled", testHintsCanBeDisabled},
    {"Events are reported to the interface", testEventsAreReported},
    {"All 15 campaign levels are valid", testCampaignLevels},
    {"Challenges and daily seeds", testChallenges},
    {"Star rating", testStarRating},
    {"Statistics and achievements", testStatisticsAndAchievements},
    {"Campaign unlocking", testCampaignUnlocking},
    {"Profile save/load round trip", testProfileRoundTrip},
    {"Profile loader rejects bad files", testProfileRejectsBadFiles},
    {"Leaderboards save/load round trip", testLeaderboardsRoundTrip},
    {"Save game round trip (and identical future)", testSaveGameRoundTrip},
    {"Save game loader rejects corrupt files", testSaveGameRejectsCorruptFiles},
    {"Number Trail combos", testTrailCombos},
    {"Difficulty levels", testDifficultyLevels},
    {"Step par and 3 stars", testStepParAndStars},
    {"Gem tour length", testTourMoves},
    {"Hammers are rare and cost points", testHammerIsRare},
    {"No extra hearts in one-life runs", testNoHeartsWithOneLife},
    {"Teleporter pads sit in dead ends", testTeleporterPadsInDeadEnds},
    {"Compass shows a free route to the nearest gem", testCompassShowsFreeRoute},
    {"Minotaur gauntlet", testMinotaurGauntlet},
    {"Admin unlock and Star Collector", testAdminUnlock},
    {"Map maker: editing rules", testMapEditingRules},
    {"Map maker: teleporters pair up", testMapTeleporterPairing},
    {"Map maker: problems block test plays", testMapProblems},
    {"Map maker: only beaten maps are verified", testMapVerification},
    {"Map maker: .lmap round trip", testMapFileRoundTrip},
    {"Map maker: bad map files are rejected", testMapLoaderRejectsBadFiles},
};

} // namespace

int main()
{
    std::cout << "Labyrinth - automated tests\n\n";
    int testsFailed = 0;
    for (const TestCase& test : TEST_CASES)
    {
        const int failedBefore = checksFailed;
        test.function();
        const bool passed = (checksFailed == failedBefore);
        if (!passed)
        {
            ++testsFailed;
        }
        std::cout << (passed ? "  [PASS] " : "  [FAIL] ") << test.name << '\n';
    }

    const int testCount = static_cast<int>(sizeof(TEST_CASES) / sizeof(TEST_CASES[0]));
    std::cout << "\n" << testCount - testsFailed << "/" << testCount << " tests passed (" << checksRun - checksFailed
              << "/" << checksRun << " checks).\n";
    return testsFailed == 0 ? 0 : 1;
}
