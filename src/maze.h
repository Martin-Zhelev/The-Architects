// maze.h
// Maze storage, generation and path-finding.
//
// Everything declared here is pure game logic: none of these functions
// read from the keyboard or print to the console.

#ifndef MAZE_H
#define MAZE_H

const int MIN_MAZE_SIZE = 10;
const int MAX_MAZE_SIZE = 50;

// One square of the drawn maze.
enum class Tile : unsigned char
{
    Wall,
    Path,
    Door,  // special door: a shortcut towards the exit that needs a key
    Key    // a key lying on a cell, waiting to be picked up
};

enum class Direction
{
    Up,
    Down,
    Left,
    Right
};

// A maze of size x size cells.
//
// It is stored as a (2 * size + 1) x (2 * size + 1) grid of tiles, allocated
// dynamically. Cell (row, col) lives at tile (2 * row + 1, 2 * col + 1); the
// tiles between two cells are either walls, open passages or special doors.
struct Maze
{
    int size = 0;           // cells per side (10 - 50)
    int tileCount = 0;      // tiles per side (2 * size + 1)
    Tile** tiles = nullptr; // tiles[row][col], owned by the maze
    unsigned int seed = 0;  // the same seed always produces the same maze
    int doorCount = 0;
    int keyCount = 0;
};

// An ordered list of cells, e.g. the shortest route to the exit.
struct Path
{
    int length = 0;
    int* rows = nullptr;
    int* cols = nullptr;
};

// ---- Creating and destroying ----
bool isValidMazeSize(int size);
bool createMaze(Maze& maze, int size, unsigned int seed);
// Allocates an all-wall maze without carving it (used when loading a saved game).
bool allocateMaze(Maze& maze, int size);
void destroyMaze(Maze& maze);

// ---- Coordinates and tile access ----
int cellToTile(int cell);
bool isInsideMaze(const Maze& maze, int row, int col);
void getDirectionOffset(Direction direction, int& rowOffset, int& colOffset);
Tile getCellTile(const Maze& maze, int row, int col);
void setCellTile(Maze& maze, int row, int col, Tile tile);
Tile getPassageTile(const Maze& maze, int row, int col, Direction direction);
void setPassageTile(Maze& maze, int row, int col, Direction direction, Tile tile);

// True if a walker could step from the cell in the given direction.
// Special doors only count when allowDoors is true.
bool isPassable(const Maze& maze, int row, int col, Direction direction, bool allowDoors);

// ---- Path-finding (breadth-first search) ----
// Fills distances[row * size + col] with the number of steps from the given
// cell, or -1 for cells that cannot be reached. The array must hold size*size ints.
void computeDistances(const Maze& maze, int fromRow, int fromCol, bool allowDoors, int* distances);
// Finds the shortest route from a cell to the exit. The caller owns the result
// and must release it with destroyPath().
bool findShortestPath(const Maze& maze, int fromRow, int fromCol, bool allowDoors, Path& path);
void destroyPath(Path& path);

// ---- Dynamic boolean grids (visited cells, hint overlay, ...) ----
bool** createFlagGrid(int rows, int cols);
void clearFlagGrid(bool** grid, int rows, int cols);
void destroyFlagGrid(bool**& grid, int rows);

#endif // MAZE_H
