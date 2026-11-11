// savegame.cpp
// Saving and loading a running labyrinth. Pure logic.
//
// File layout (one section per line or block):
//   LABYRINTH_SAVE 2
//   meta <mode> <index> <theme>
//   config ...            player ...
//   tiles  (tileCount lines: # wall, . path, D door, K key)
//   items  (size lines, one letter per cell)
//   visited / discovered (size lines of 0 and 1)
//   links, minotaurs, rng

#include "savegame.h"

#include <sstream>

namespace
{

const char* const SAVE_HEADER = "LABYRINTH_SAVE 2";
const char ITEM_CODES[ITEM_TYPE_COUNT] = {'.', 'g', 't', 'b', 'h', 'o', 'l', 'c', 's', 'p'};

char tileToChar(Tile tile)
{
    switch (tile)
    {
    case Tile::Wall:
        return '#';
    case Tile::Path:
        return '.';
    case Tile::Door:
        return 'D';
    case Tile::Key:
        return 'K';
    }
    return '#';
}

bool charToTile(char symbol, Tile& tile)
{
    switch (symbol)
    {
    case '#':
        tile = Tile::Wall;
        return true;
    case '.':
        tile = Tile::Path;
        return true;
    case 'D':
        tile = Tile::Door;
        return true;
    case 'K':
        tile = Tile::Key;
        return true;
    default:
        return false;
    }
}

bool charToItem(char symbol, Item& item)
{
    for (int i = 0; i < ITEM_TYPE_COUNT; ++i)
    {
        if (ITEM_CODES[i] == symbol)
        {
            item = static_cast<Item>(i);
            return true;
        }
    }
    return false;
}

void writeFlagGrid(std::ostringstream& out, bool** grid, int size)
{
    for (int row = 0; row < size; ++row)
    {
        for (int col = 0; col < size; ++col)
        {
            out << (grid[row][col] ? '1' : '0');
        }
        out << '\n';
    }
}

bool readFlagGrid(std::istream& in, bool** grid, int size)
{
    for (int row = 0; row < size; ++row)
    {
        std::string line;
        if (!(in >> line) || static_cast<int>(line.size()) != size)
        {
            return false;
        }
        for (int col = 0; col < size; ++col)
        {
            grid[row][col] = line[static_cast<std::size_t>(col)] == '1';
        }
    }
    return true;
}

bool expectWord(std::istream& in, const char* word)
{
    std::string read;
    return (in >> read) && read == word;
}

// The body of deserializeRun; any "false" makes the caller clean up.
bool readRun(std::istream& in, GameState& state, SaveMeta& meta)
{
    std::string header;
    std::getline(in, header);
    if (header.find(SAVE_HEADER) != 0)
    {
        return false;
    }

    if (!expectWord(in, "meta") || !(in >> meta.mode >> meta.index >> meta.theme))
    {
        return false;
    }

    RunConfig& c = state.config;
    int fog = 0;
    int powerUps = 0;
    int traps = 0;
    int requireGems = 0;
    int hints = 0;
    int chase = 0;
    if (!expectWord(in, "config") ||
        !(in >> c.size >> c.seed >> c.lives >> c.minotaurCount >> c.minotaurStepSeconds >> fog >> powerUps >>
          traps >> c.teleporterPairs >> c.gemCount >> requireGems >> c.timeLimitSeconds >> hints >>
          c.difficulty >> chase))
    {
        return false;
    }
    c.minotaurChase = chase != 0;
    if (c.difficulty < 0 || c.difficulty >= DIFFICULTY_COUNT)
    {
        return false;
    }
    c.fogOfWar = fog != 0;
    c.powerUps = powerUps != 0;
    c.traps = traps != 0;
    c.requireAllGems = requireGems != 0;
    c.hintsAllowed = hints != 0;
    if (!isValidMazeSize(c.size))
    {
        return false;
    }

    int facing = 0;
    int hintVisible = 0;
    if (!expectWord(in, "player") ||
        !(in >> state.playerRow >> state.playerCol >> facing >> state.moves >> state.keysHeld >>
          state.doorsOpened >> state.hintsUsed >> state.optimalMoves >> state.lives >> state.maxLives >>
          state.gemsCollected >> state.gemsTotal >> state.torchLevel >> state.hammerCharges >> state.bootsTime >>
          state.freezeTime >> state.compassTime >> state.invulnerableTime >> state.elapsed >>
          state.lastTimeWarning >> state.wallsBroken >> state.teleportsUsed >> state.timesCaught >>
          state.damageTaken >> state.powerUpsCollected >> hintVisible >> state.tourMoves))
    {
        return false;
    }
    if (facing < 0 || facing > 3)
    {
        return false;
    }
    state.facing = static_cast<Direction>(facing);
    state.hintVisible = hintVisible != 0;

    // Maze tiles
    if (!allocateMaze(state.maze, c.size) || !expectWord(in, "tiles"))
    {
        return false;
    }
    state.maze.seed = c.seed;
    for (int row = 0; row < state.maze.tileCount; ++row)
    {
        std::string line;
        if (!(in >> line) || static_cast<int>(line.size()) != state.maze.tileCount)
        {
            return false;
        }
        for (int col = 0; col < state.maze.tileCount; ++col)
        {
            Tile tile = Tile::Wall;
            if (!charToTile(line[static_cast<std::size_t>(col)], tile))
            {
                return false;
            }
            state.maze.tiles[row][col] = tile;
            state.maze.doorCount += tile == Tile::Door ? 1 : 0;
            state.maze.keyCount += tile == Tile::Key ? 1 : 0;
        }
    }

    const int size = c.size;
    if (!isInsideMaze(state.maze, state.playerRow, state.playerCol))
    {
        return false;
    }

    // Grids
    state.visitedCells = createFlagGrid(size, size);
    state.discovered = createFlagGrid(size, size);
    state.hintTiles = createFlagGrid(state.maze.tileCount, state.maze.tileCount);
    state.items = new Item*[size];
    for (int row = 0; row < size; ++row)
    {
        state.items[row] = new Item[size];
        for (int col = 0; col < size; ++col)
        {
            state.items[row][col] = Item::None;
        }
    }
    state.teleportLinks = new int[size * size];
    for (int i = 0; i < size * size; ++i)
    {
        state.teleportLinks[i] = -1;
    }

    if (!expectWord(in, "items"))
    {
        return false;
    }
    for (int row = 0; row < size; ++row)
    {
        std::string line;
        if (!(in >> line) || static_cast<int>(line.size()) != size)
        {
            return false;
        }
        for (int col = 0; col < size; ++col)
        {
            if (!charToItem(line[static_cast<std::size_t>(col)], state.items[row][col]))
            {
                return false;
            }
        }
    }

    if (!expectWord(in, "visited") || !readFlagGrid(in, state.visitedCells, size) ||
        !expectWord(in, "discovered") || !readFlagGrid(in, state.discovered, size))
    {
        return false;
    }

    int linkCount = 0;
    if (!expectWord(in, "links") || !(in >> linkCount) || linkCount < 0 || linkCount > size * size)
    {
        return false;
    }
    for (int i = 0; i < linkCount; ++i)
    {
        int from = 0;
        int to = 0;
        if (!(in >> from >> to) || from < 0 || to < 0 || from >= size * size || to >= size * size)
        {
            return false;
        }
        state.teleportLinks[from] = to;
    }

    int minotaurCount = 0;
    if (!expectWord(in, "minotaurs") || !(in >> minotaurCount) || minotaurCount < 0 || minotaurCount > 3)
    {
        return false;
    }
    state.minotaurCount = minotaurCount;
    state.minotaurs = minotaurCount > 0 ? new Minotaur[minotaurCount] : nullptr;
    for (int i = 0; i < minotaurCount; ++i)
    {
        Minotaur& m = state.minotaurs[i];
        int hunting = 0;
        int released = 0;
        if (!(in >> m.row >> m.col >> m.prevRow >> m.prevCol >> m.spawnRow >> m.spawnCol >> hunting >>
              m.stepTimer >> m.releaseTime >> released) ||
            !isInsideMaze(state.maze, m.row, m.col) || !isInsideMaze(state.maze, m.prevRow, m.prevCol) ||
            !isInsideMaze(state.maze, m.spawnRow, m.spawnCol))
        {
            return false;
        }
        m.hunting = hunting != 0;
        m.released = released != 0;
    }

    if (!expectWord(in, "rng") || !(in >> state.rng))
    {
        return false;
    }

    refreshHint(state);
    return true;
}

} // namespace

std::string serializeRun(const GameState& state, const SaveMeta& meta)
{
    const RunConfig& c = state.config;
    std::ostringstream out;
    out.precision(9);
    out << SAVE_HEADER << '\n';
    out << "meta " << meta.mode << ' ' << meta.index << ' ' << meta.theme << '\n';
    out << "config " << c.size << ' ' << c.seed << ' ' << c.lives << ' ' << c.minotaurCount << ' '
        << c.minotaurStepSeconds << ' ' << c.fogOfWar << ' ' << c.powerUps << ' ' << c.traps << ' '
        << c.teleporterPairs << ' ' << c.gemCount << ' ' << c.requireAllGems << ' ' << c.timeLimitSeconds << ' '
        << c.hintsAllowed << ' ' << c.difficulty << ' ' << c.minotaurChase << '\n';
    out << "player " << state.playerRow << ' ' << state.playerCol << ' ' << static_cast<int>(state.facing) << ' '
        << state.moves << ' ' << state.keysHeld << ' ' << state.doorsOpened << ' ' << state.hintsUsed << ' '
        << state.optimalMoves << ' ' << state.lives << ' ' << state.maxLives << ' ' << state.gemsCollected << ' '
        << state.gemsTotal << ' ' << state.torchLevel << ' ' << state.hammerCharges << ' ' << state.bootsTime
        << ' ' << state.freezeTime << ' ' << state.compassTime << ' ' << state.invulnerableTime << ' '
        << state.elapsed << ' ' << state.lastTimeWarning << ' ' << state.wallsBroken << ' '
        << state.teleportsUsed << ' ' << state.timesCaught << ' ' << state.damageTaken << ' '
        << state.powerUpsCollected << ' ' << state.hintVisible << ' ' << state.tourMoves << '\n';

    out << "tiles\n";
    for (int row = 0; row < state.maze.tileCount; ++row)
    {
        for (int col = 0; col < state.maze.tileCount; ++col)
        {
            out << tileToChar(state.maze.tiles[row][col]);
        }
        out << '\n';
    }

    const int size = state.maze.size;
    out << "items\n";
    for (int row = 0; row < size; ++row)
    {
        for (int col = 0; col < size; ++col)
        {
            out << ITEM_CODES[static_cast<int>(state.items[row][col])];
        }
        out << '\n';
    }
    out << "visited\n";
    writeFlagGrid(out, state.visitedCells, size);
    out << "discovered\n";
    writeFlagGrid(out, state.discovered, size);

    int linkCount = 0;
    for (int i = 0; i < size * size; ++i)
    {
        linkCount += state.teleportLinks[i] >= 0 ? 1 : 0;
    }
    out << "links " << linkCount << '\n';
    for (int i = 0; i < size * size; ++i)
    {
        if (state.teleportLinks[i] >= 0)
        {
            out << i << ' ' << state.teleportLinks[i] << '\n';
        }
    }

    out << "minotaurs " << state.minotaurCount << '\n';
    for (int i = 0; i < state.minotaurCount; ++i)
    {
        const Minotaur& m = state.minotaurs[i];
        out << m.row << ' ' << m.col << ' ' << m.prevRow << ' ' << m.prevCol << ' ' << m.spawnRow << ' '
            << m.spawnCol << ' ' << m.hunting << ' ' << m.stepTimer << ' ' << m.releaseTime << ' ' << m.released
            << '\n';
    }
    out << "rng " << state.rng << '\n';
    return out.str();
}

bool deserializeRun(const std::string& text, GameState& state, SaveMeta& meta)
{
    endGame(state);
    std::istringstream in(text);
    if (!readRun(in, state, meta))
    {
        endGame(state);
        return false;
    }
    return true;
}
