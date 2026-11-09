// maze.cpp
// Maze storage, generation and path-finding.
// Pure logic: nothing in this file reads input or prints output.

#include "maze.h"

#include <cstdlib>
#include <random>
#include <utility>

namespace
{

const int DIRECTION_COUNT = 4;
const Direction ALL_DIRECTIONS[DIRECTION_COUNT] = {
    Direction::Up, Direction::Down, Direction::Left, Direction::Right};

// Converts a (row, col) cell position into an index for flat arrays.
int toCellIndex(const Maze& maze, int row, int col)
{
    return row * maze.size + col;
}

bool canPass(const Maze& maze, int row, int col, Direction direction, bool allowDoors)
{
    return isPassable(maze, row, col, direction, allowDoors);
}

// Allocates the 2D tile grid and fills it with walls.
void allocateTiles(Maze& maze)
{
    maze.tiles = new Tile*[maze.tileCount];
    for (int row = 0; row < maze.tileCount; ++row)
    {
        maze.tiles[row] = new Tile[maze.tileCount];
        for (int col = 0; col < maze.tileCount; ++col)
        {
            maze.tiles[row][col] = Tile::Wall;
        }
    }
}

// Carves corridors with a randomised depth-first search (the "recursive
// backtracker"). An explicit dynamic stack is used instead of recursion so a
// 50x50 maze (2500 cells) can never overflow the call stack.
// The result is a "perfect" maze: exactly one route between any two cells.
void carvePassages(Maze& maze, std::mt19937& rng)
{
    const int cellTotal = maze.size * maze.size;
    bool* visited = new bool[cellTotal]();
    int* stack = new int[cellTotal];
    int stackSize = 0;

    stack[stackSize++] = 0;
    visited[0] = true;
    setCellTile(maze, 0, 0, Tile::Path);

    while (stackSize > 0)
    {
        const int current = stack[stackSize - 1];
        const int row = current / maze.size;
        const int col = current % maze.size;

        // Collect the neighbours that have not been carved yet.
        Direction options[DIRECTION_COUNT];
        int optionCount = 0;
        for (const Direction direction : ALL_DIRECTIONS)
        {
            int rowOffset = 0;
            int colOffset = 0;
            getDirectionOffset(direction, rowOffset, colOffset);
            const int nextRow = row + rowOffset;
            const int nextCol = col + colOffset;
            if (isInsideMaze(maze, nextRow, nextCol) && !visited[toCellIndex(maze, nextRow, nextCol)])
            {
                options[optionCount++] = direction;
            }
        }

        if (optionCount == 0)
        {
            --stackSize; // dead end: backtrack
            continue;
        }

        std::uniform_int_distribution<int> pickOption(0, optionCount - 1);
        const Direction chosen = options[pickOption(rng)];
        int rowOffset = 0;
        int colOffset = 0;
        getDirectionOffset(chosen, rowOffset, colOffset);
        const int nextRow = row + rowOffset;
        const int nextCol = col + colOffset;

        setPassageTile(maze, row, col, chosen, Tile::Path);
        setCellTile(maze, nextRow, nextCol, Tile::Path);
        visited[toCellIndex(maze, nextRow, nextCol)] = true;
        stack[stackSize++] = toCellIndex(maze, nextRow, nextCol);
    }

    delete[] stack;
    delete[] visited;
}

// Turns a few walls into special doors. A wall is a good candidate when the
// two cells on either side of it are far apart along the corridors, so the
// door really shortens the way to the exit.
void addDoors(Maze& maze, std::mt19937& rng)
{
    const int cellTotal = maze.size * maze.size;
    int* distances = new int[cellTotal];
    computeDistances(maze, 0, 0, false, distances);

    // Each candidate is encoded as cellIndex * 2 + side,
    // where side 0 = wall to the right of the cell, side 1 = wall below it.
    int* candidates = new int[cellTotal * 2];
    int candidateCount = 0;
    const int minimumShortcut = maze.size;

    for (int row = 0; row < maze.size; ++row)
    {
        for (int col = 0; col < maze.size; ++col)
        {
            for (int side = 0; side < 2; ++side)
            {
                const Direction direction = (side == 0) ? Direction::Right : Direction::Down;
                int rowOffset = 0;
                int colOffset = 0;
                getDirectionOffset(direction, rowOffset, colOffset);
                const int nextRow = row + rowOffset;
                const int nextCol = col + colOffset;
                if (!isInsideMaze(maze, nextRow, nextCol) ||
                    getPassageTile(maze, row, col, direction) != Tile::Wall)
                {
                    continue;
                }

                const int gap = std::abs(distances[toCellIndex(maze, row, col)] -
                                         distances[toCellIndex(maze, nextRow, nextCol)]);
                if (gap > minimumShortcut)
                {
                    candidates[candidateCount++] = toCellIndex(maze, row, col) * 2 + side;
                }
            }
        }
    }

    // Fisher-Yates shuffle so the chosen doors are spread randomly.
    for (int i = candidateCount - 1; i > 0; --i)
    {
        std::uniform_int_distribution<int> pickIndex(0, i);
        std::swap(candidates[i], candidates[pickIndex(rng)]);
    }

    const int wantedDoors = maze.size / 5;
    maze.doorCount = 0;
    for (int i = 0; i < candidateCount && maze.doorCount < wantedDoors; ++i)
    {
        const int cell = candidates[i] / 2;
        const Direction direction = (candidates[i] % 2 == 0) ? Direction::Right : Direction::Down;
        setPassageTile(maze, cell / maze.size, cell % maze.size, direction, Tile::Door);
        ++maze.doorCount;
    }

    delete[] candidates;
    delete[] distances;
}

// Scatters keys on random cells. There are fewer keys than doors,
// so the player has to choose which shortcut to use.
void placeKeys(Maze& maze, std::mt19937& rng)
{
    const int wantedKeys = (maze.doorCount + 1) / 2;
    std::uniform_int_distribution<int> pickCell(0, maze.size - 1);

    maze.keyCount = 0;
    while (maze.keyCount < wantedKeys)
    {
        const int row = pickCell(rng);
        const int col = pickCell(rng);
        const bool isStart = (row == 0 && col == 0);
        const bool isExit = (row == maze.size - 1 && col == maze.size - 1);
        if (!isStart && !isExit && getCellTile(maze, row, col) == Tile::Path)
        {
            setCellTile(maze, row, col, Tile::Key);
            ++maze.keyCount;
        }
    }
}

} // namespace

// ---------------------------------------------------------------------------
// Creating and destroying
// ---------------------------------------------------------------------------

bool isValidMazeSize(int size)
{
    return size >= MIN_MAZE_SIZE && size <= MAX_MAZE_SIZE;
}

bool allocateMaze(Maze& maze, int size)
{
    destroyMaze(maze);
    if (!isValidMazeSize(size))
    {
        return false;
    }
    maze.size = size;
    maze.tileCount = 2 * size + 1;
    allocateTiles(maze);
    return true;
}

bool createMaze(Maze& maze, int size, unsigned int seed)
{
    if (!allocateMaze(maze, size))
    {
        return false;
    }
    maze.seed = seed;

    std::mt19937 rng(seed);
    carvePassages(maze, rng);
    addDoors(maze, rng);
    placeKeys(maze, rng);

    // Openings in the outer wall: entrance (top-left) and exit (bottom-right).
    maze.tiles[0][1] = Tile::Path;
    maze.tiles[maze.tileCount - 1][maze.tileCount - 2] = Tile::Path;
    return true;
}

void destroyMaze(Maze& maze)
{
    if (maze.tiles != nullptr)
    {
        for (int row = 0; row < maze.tileCount; ++row)
        {
            delete[] maze.tiles[row];
        }
        delete[] maze.tiles;
    }
    maze = Maze();
}

// ---------------------------------------------------------------------------
// Coordinates and tile access
// ---------------------------------------------------------------------------

int cellToTile(int cell)
{
    return cell * 2 + 1;
}

bool isInsideMaze(const Maze& maze, int row, int col)
{
    return row >= 0 && col >= 0 && row < maze.size && col < maze.size;
}

void getDirectionOffset(Direction direction, int& rowOffset, int& colOffset)
{
    rowOffset = 0;
    colOffset = 0;
    switch (direction)
    {
    case Direction::Up:
        rowOffset = -1;
        break;
    case Direction::Down:
        rowOffset = 1;
        break;
    case Direction::Left:
        colOffset = -1;
        break;
    case Direction::Right:
        colOffset = 1;
        break;
    }
}

Tile getCellTile(const Maze& maze, int row, int col)
{
    return maze.tiles[cellToTile(row)][cellToTile(col)];
}

void setCellTile(Maze& maze, int row, int col, Tile tile)
{
    maze.tiles[cellToTile(row)][cellToTile(col)] = tile;
}

Tile getPassageTile(const Maze& maze, int row, int col, Direction direction)
{
    int rowOffset = 0;
    int colOffset = 0;
    getDirectionOffset(direction, rowOffset, colOffset);
    return maze.tiles[cellToTile(row) + rowOffset][cellToTile(col) + colOffset];
}

void setPassageTile(Maze& maze, int row, int col, Direction direction, Tile tile)
{
    int rowOffset = 0;
    int colOffset = 0;
    getDirectionOffset(direction, rowOffset, colOffset);
    maze.tiles[cellToTile(row) + rowOffset][cellToTile(col) + colOffset] = tile;
}

// ---------------------------------------------------------------------------
// Path-finding
// ---------------------------------------------------------------------------

bool isPassable(const Maze& maze, int row, int col, Direction direction, bool allowDoors)
{
    int rowOffset = 0;
    int colOffset = 0;
    getDirectionOffset(direction, rowOffset, colOffset);
    if (!isInsideMaze(maze, row, col) || !isInsideMaze(maze, row + rowOffset, col + colOffset))
    {
        return false;
    }

    const Tile passage = getPassageTile(maze, row, col, direction);
    return passage == Tile::Path || passage == Tile::Key || (allowDoors && passage == Tile::Door);
}

void computeDistances(const Maze& maze, int fromRow, int fromCol, bool allowDoors, int* distances)
{
    const int cellTotal = maze.size * maze.size;
    for (int i = 0; i < cellTotal; ++i)
    {
        distances[i] = -1;
    }
    if (!isInsideMaze(maze, fromRow, fromCol))
    {
        return;
    }

    // Breadth-first search with a dynamic queue: every cell enters it once.
    int* queue = new int[cellTotal];
    int head = 0;
    int tail = 0;
    const int start = toCellIndex(maze, fromRow, fromCol);
    distances[start] = 0;
    queue[tail++] = start;

    while (head < tail)
    {
        const int current = queue[head++];
        const int row = current / maze.size;
        const int col = current % maze.size;

        for (const Direction direction : ALL_DIRECTIONS)
        {
            if (!canPass(maze, row, col, direction, allowDoors))
            {
                continue;
            }
            int rowOffset = 0;
            int colOffset = 0;
            getDirectionOffset(direction, rowOffset, colOffset);
            const int next = toCellIndex(maze, row + rowOffset, col + colOffset);
            if (distances[next] == -1)
            {
                distances[next] = distances[current] + 1;
                queue[tail++] = next;
            }
        }
    }

    delete[] queue;
}

bool findShortestPath(const Maze& maze, int fromRow, int fromCol, bool allowDoors, Path& path)
{
    destroyPath(path);
    if (!isInsideMaze(maze, fromRow, fromCol))
    {
        return false;
    }

    // Distances are measured FROM the exit, so walking "downhill" from the
    // start cell always leads to the exit along a shortest route.
    int* distances = new int[maze.size * maze.size];
    computeDistances(maze, maze.size - 1, maze.size - 1, allowDoors, distances);

    const int steps = distances[toCellIndex(maze, fromRow, fromCol)];
    if (steps < 0)
    {
        delete[] distances;
        return false;
    }

    path.length = steps + 1;
    path.rows = new int[path.length];
    path.cols = new int[path.length];

    int row = fromRow;
    int col = fromCol;
    for (int i = 0; i < path.length; ++i)
    {
        path.rows[i] = row;
        path.cols[i] = col;

        const int currentDistance = distances[toCellIndex(maze, row, col)];
        for (const Direction direction : ALL_DIRECTIONS)
        {
            if (!canPass(maze, row, col, direction, allowDoors))
            {
                continue;
            }
            int rowOffset = 0;
            int colOffset = 0;
            getDirectionOffset(direction, rowOffset, colOffset);
            if (distances[toCellIndex(maze, row + rowOffset, col + colOffset)] == currentDistance - 1)
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

void destroyPath(Path& path)
{
    delete[] path.rows;
    delete[] path.cols;
    path = Path();
}

// ---------------------------------------------------------------------------
// Dynamic boolean grids
// ---------------------------------------------------------------------------

bool** createFlagGrid(int rows, int cols)
{
    bool** grid = new bool*[rows];
    for (int row = 0; row < rows; ++row)
    {
        grid[row] = new bool[cols](); // () zero-initialises to false
    }
    return grid;
}

void clearFlagGrid(bool** grid, int rows, int cols)
{
    if (grid == nullptr)
    {
        return;
    }
    for (int row = 0; row < rows; ++row)
    {
        for (int col = 0; col < cols; ++col)
        {
            grid[row][col] = false;
        }
    }
}

void destroyFlagGrid(bool**& grid, int rows)
{
    if (grid == nullptr)
    {
        return;
    }
    for (int row = 0; row < rows; ++row)
    {
        delete[] grid[row];
    }
    delete[] grid;
    grid = nullptr;
}
