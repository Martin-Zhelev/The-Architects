// custom_map.cpp
// Hand-made mazes from the map maker. Pure logic.
//
// File layout (.lmap):
//   LABYRINTH_MAP 1
//   name=<map name>
//   author=<profile name>
//   theme <0-4>
//   config <size> <lives> <minotaur ms per step> <fog> <time limit> <hints> <sealed exit> <gauntlet>
//   tiles            (tileCount lines: # wall, . path, D door, K key)
//   items            (size lines, one letter per cell)
//   links <count>    (pairs of cell indexes)
//   minotaurs <count> <cell index> ...
//   verified <0/1> <checksum> <moves> <seconds>

#include "custom_map.h"

#include <cctype>
#include <cmath>
#include <sstream>

namespace
{

const char* const MAP_HEADER = "LABYRINTH_MAP 1";
const char ITEM_CODES[ITEM_TYPE_COUNT] = {'.', 'g', 't', 'b', 'h', 'o', 'l', 'c', 's', 'p'};

int clampInt(int value, int low, int high)
{
    return value < low ? low : (value > high ? high : value);
}

int tileCountFor(int size)
{
    return 2 * size + 1;
}

bool isCellInside(const CustomMap& map, int row, int col)
{
    return row >= 0 && col >= 0 && row < map.config.size && col < map.config.size;
}

bool isStartOrExit(const CustomMap& map, int row, int col)
{
    return (row == 0 && col == 0) || (row == map.config.size - 1 && col == map.config.size - 1);
}

int cellIndexOf(const CustomMap& map, int row, int col)
{
    return row * map.config.size + col;
}

Tile& tileAt(CustomMap& map, int tileRow, int tileCol)
{
    return map.tiles[static_cast<std::size_t>(tileRow * getMapTileCount(map) + tileCol)];
}

bool isShapeValid(const CustomMap& map)
{
    const int size = map.config.size;
    const int tiles = tileCountFor(size);
    return isValidMazeSize(size) && static_cast<int>(map.tiles.size()) == tiles * tiles &&
           static_cast<int>(map.items.size()) == size * size && static_cast<int>(map.links.size()) == size * size;
}

void resetContents(CustomMap& map, int size, Tile inside)
{
    map.config.size = size;
    const int tiles = tileCountFor(size);
    map.tiles.assign(static_cast<std::size_t>(tiles * tiles), Tile::Wall);
    for (int row = 1; row < tiles - 1; ++row)
    {
        for (int col = 1; col < tiles - 1; ++col)
        {
            const bool cell = (row % 2 == 1) && (col % 2 == 1);
            const bool pillar = (row % 2 == 0) && (col % 2 == 0);
            if (cell)
            {
                tileAt(map, row, col) = Tile::Path;
            }
            else if (!pillar)
            {
                tileAt(map, row, col) = inside;
            }
        }
    }
    tileAt(map, 0, 1) = Tile::Path;                 // entrance
    tileAt(map, tiles - 1, tiles - 2) = Tile::Path; // exit
    map.items.assign(static_cast<std::size_t>(size * size), Item::None);
    map.links.assign(static_cast<std::size_t>(size * size), -1);
    map.minotaurCells.clear();
    markMapChanged(map);
}

// Builds a real Maze from the map so the BFS of maze.cpp can check it.
bool buildMaze(const CustomMap& map, Maze& maze)
{
    if (!isShapeValid(map) || !allocateMaze(maze, map.config.size))
    {
        return false;
    }
    for (int row = 0; row < maze.tileCount; ++row)
    {
        for (int col = 0; col < maze.tileCount; ++col)
        {
            const Tile tile = getMapTile(map, row, col);
            maze.tiles[row][col] = tile;
            maze.doorCount += tile == Tile::Door ? 1 : 0;
            maze.keyCount += tile == Tile::Key ? 1 : 0;
        }
    }
    return true;
}

// Removes a pad and leaves its twin waiting for a new partner.
void unlinkPad(CustomMap& map, int index)
{
    const int twin = map.links[static_cast<std::size_t>(index)];
    if (twin >= 0)
    {
        map.links[static_cast<std::size_t>(twin)] = -1;
    }
    map.links[static_cast<std::size_t>(index)] = -1;
}

void appendFnv(unsigned int& hash, const std::string& text)
{
    for (const char symbol : text)
    {
        hash ^= static_cast<unsigned char>(symbol);
        hash *= 16777619u;
    }
}

int stepMilliseconds(const CustomMap& map)
{
    return static_cast<int>(std::lround(map.config.minotaurStepSeconds * 1000.0f));
}

// Everything that changes how the map plays (not its name or look).
std::string gameplayText(const CustomMap& map)
{
    const RunConfig& c = map.config;
    std::ostringstream out;
    out << "config " << c.size << ' ' << c.lives << ' ' << stepMilliseconds(map) << ' ' << c.fogOfWar << ' '
        << c.timeLimitSeconds << ' ' << c.hintsAllowed << ' ' << c.requireAllGems << ' ' << c.minotaurChase << '\n';
    const int tiles = getMapTileCount(map);
    out << "tiles\n";
    for (int row = 0; row < tiles; ++row)
    {
        for (int col = 0; col < tiles; ++col)
        {
            switch (getMapTile(map, row, col))
            {
            case Tile::Wall:
                out << '#';
                break;
            case Tile::Path:
                out << '.';
                break;
            case Tile::Door:
                out << 'D';
                break;
            case Tile::Key:
                out << 'K';
                break;
            }
        }
        out << '\n';
    }
    out << "items\n";
    for (int row = 0; row < c.size; ++row)
    {
        for (int col = 0; col < c.size; ++col)
        {
            out << ITEM_CODES[static_cast<int>(getMapItem(map, row, col))];
        }
        out << '\n';
    }
    int linkCount = 0;
    for (const int link : map.links)
    {
        linkCount += link >= 0 ? 1 : 0;
    }
    out << "links " << linkCount << '\n';
    for (std::size_t i = 0; i < map.links.size(); ++i)
    {
        if (map.links[i] >= 0)
        {
            out << i << ' ' << map.links[i] << '\n';
        }
    }
    out << "minotaurs " << map.minotaurCells.size();
    for (const int cell : map.minotaurCells)
    {
        out << ' ' << cell;
    }
    out << '\n';
    return out.str();
}

bool expectWord(std::istream& in, const char* word)
{
    std::string read;
    return (in >> read) && read == word;
}

bool readKeyValue(std::istream& in, const char* key, std::string& value)
{
    std::string line;
    if (!std::getline(in, line))
    {
        return false;
    }
    if (!line.empty() && line.back() == '\r')
    {
        line.pop_back(); // files edited on Windows
    }
    const std::string prefix = std::string(key) + "=";
    if (line.compare(0, prefix.size(), prefix) != 0)
    {
        return false;
    }
    value = line.substr(prefix.size());
    return true;
}

bool readMap(std::istream& in, CustomMap& map)
{
    std::string header;
    std::getline(in, header);
    if (header.find(MAP_HEADER) != 0)
    {
        return false;
    }
    if (!readKeyValue(in, "name", map.name) || !isValidMapName(map.name) ||
        !readKeyValue(in, "author", map.author) || map.author.size() > 40)
    {
        return false;
    }

    RunConfig& c = map.config;
    int fog = 0;
    int hints = 0;
    int sealed = 0;
    int chase = 0;
    int stepMs = 0;
    if (!expectWord(in, "theme") || !(in >> map.theme) || map.theme < 0 || map.theme > 4 ||
        !expectWord(in, "config") ||
        !(in >> c.size >> c.lives >> stepMs >> fog >> c.timeLimitSeconds >> hints >> sealed >> chase) ||
        !isValidMazeSize(c.size) || c.lives < 1 || c.lives > 5 || stepMs < 150 || stepMs > 3000 ||
        c.timeLimitSeconds < 0 || c.timeLimitSeconds > 3600)
    {
        return false;
    }
    c.minotaurStepSeconds = static_cast<float>(stepMs) / 1000.0f;
    c.fogOfWar = fog != 0;
    c.hintsAllowed = hints != 0;
    c.requireAllGems = sealed != 0;
    c.minotaurChase = chase != 0;

    const int size = c.size;
    const int tiles = tileCountFor(size);
    map.tiles.assign(static_cast<std::size_t>(tiles * tiles), Tile::Wall);
    map.items.assign(static_cast<std::size_t>(size * size), Item::None);
    map.links.assign(static_cast<std::size_t>(size * size), -1);
    map.minotaurCells.clear();

    if (!expectWord(in, "tiles"))
    {
        return false;
    }
    for (int row = 0; row < tiles; ++row)
    {
        std::string line;
        if (!(in >> line) || static_cast<int>(line.size()) != tiles)
        {
            return false;
        }
        for (int col = 0; col < tiles; ++col)
        {
            Tile tile = Tile::Wall;
            switch (line[static_cast<std::size_t>(col)])
            {
            case '#':
                tile = Tile::Wall;
                break;
            case '.':
                tile = Tile::Path;
                break;
            case 'D':
                tile = Tile::Door;
                break;
            case 'K':
                tile = Tile::Key;
                break;
            default:
                return false;
            }
            tileAt(map, row, col) = tile;
        }
    }
    // The outer wall must be closed except for the entrance and the exit,
    // pillars are always walls and cells are never walls.
    for (int row = 0; row < tiles; ++row)
    {
        for (int col = 0; col < tiles; ++col)
        {
            const Tile tile = getMapTile(map, row, col);
            const bool border = row == 0 || col == 0 || row == tiles - 1 || col == tiles - 1;
            const bool opening = (row == 0 && col == 1) || (row == tiles - 1 && col == tiles - 2);
            const bool cell = row % 2 == 1 && col % 2 == 1;
            const bool pillar = row % 2 == 0 && col % 2 == 0;
            if (opening)
            {
                if (tile != Tile::Path)
                {
                    return false;
                }
            }
            else if ((border || pillar) && tile != Tile::Wall)
            {
                return false;
            }
            else if (cell && (tile == Tile::Wall || tile == Tile::Door))
            {
                return false;
            }
            else if (!cell && tile == Tile::Key)
            {
                return false;
            }
        }
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
            bool known = false;
            for (int i = 0; i < ITEM_TYPE_COUNT; ++i)
            {
                if (ITEM_CODES[i] == line[static_cast<std::size_t>(col)])
                {
                    map.items[static_cast<std::size_t>(row * size + col)] = static_cast<Item>(i);
                    known = true;
                }
            }
            if (!known || (map.items[static_cast<std::size_t>(row * size + col)] != Item::None &&
                           isStartOrExit(map, row, col)))
            {
                return false;
            }
        }
    }

    int linkCount = 0;
    if (!expectWord(in, "links") || !(in >> linkCount) || linkCount < 0 || linkCount > 2 * MAX_MAP_TELEPORTER_PAIRS)
    {
        return false;
    }
    for (int i = 0; i < linkCount; ++i)
    {
        int from = 0;
        int to = 0;
        if (!(in >> from >> to) || from < 0 || to < 0 || from >= size * size || to >= size * size || from == to ||
            map.items[static_cast<std::size_t>(from)] != Item::Teleporter ||
            map.items[static_cast<std::size_t>(to)] != Item::Teleporter)
        {
            return false;
        }
        map.links[static_cast<std::size_t>(from)] = to;
    }
    for (int i = 0; i < size * size; ++i) // links must go both ways
    {
        const int twin = map.links[static_cast<std::size_t>(i)];
        if (twin >= 0 && map.links[static_cast<std::size_t>(twin)] != i)
        {
            return false;
        }
    }

    int minotaurCount = 0;
    if (!expectWord(in, "minotaurs") || !(in >> minotaurCount) || minotaurCount < 0 ||
        minotaurCount > MAX_MAP_MINOTAURS)
    {
        return false;
    }
    for (int i = 0; i < minotaurCount; ++i)
    {
        int cell = 0;
        if (!(in >> cell) || cell < 0 || cell >= size * size || isStartOrExit(map, cell / size, cell % size))
        {
            return false;
        }
        map.minotaurCells.push_back(cell);
    }

    int verified = 0;
    if (!expectWord(in, "verified") ||
        !(in >> verified >> map.checksum >> map.verifiedMoves >> map.verifiedSeconds))
    {
        return false;
    }
    map.verified = verified != 0;
    return countMapItems(map, Item::Gem) <= MAX_MAP_GEMS;
}

} // namespace

// ---------------------------------------------------------------------------
// Creating
// ---------------------------------------------------------------------------

bool createBlankMap(CustomMap& map, int size, bool open)
{
    if (!isValidMazeSize(size))
    {
        return false;
    }
    resetContents(map, size, open ? Tile::Path : Tile::Wall);
    return true;
}

bool createGeneratedMap(CustomMap& map, int size, unsigned int seed)
{
    Maze maze;
    if (!createMaze(maze, size, seed))
    {
        return false;
    }
    resetContents(map, size, Tile::Wall);
    for (int row = 0; row < maze.tileCount; ++row)
    {
        for (int col = 0; col < maze.tileCount; ++col)
        {
            tileAt(map, row, col) = maze.tiles[row][col];
        }
    }
    destroyMaze(maze);
    return true;
}

// ---------------------------------------------------------------------------
// Looking at a map
// ---------------------------------------------------------------------------

int getMapTileCount(const CustomMap& map)
{
    return tileCountFor(map.config.size);
}

Tile getMapTile(const CustomMap& map, int tileRow, int tileCol)
{
    const int tiles = getMapTileCount(map);
    if (tileRow < 0 || tileCol < 0 || tileRow >= tiles || tileCol >= tiles ||
        static_cast<int>(map.tiles.size()) != tiles * tiles)
    {
        return Tile::Wall;
    }
    return map.tiles[static_cast<std::size_t>(tileRow * tiles + tileCol)];
}

Item getMapItem(const CustomMap& map, int row, int col)
{
    if (!isCellInside(map, row, col) || static_cast<int>(map.items.size()) != map.config.size * map.config.size)
    {
        return Item::None;
    }
    return map.items[static_cast<std::size_t>(cellIndexOf(map, row, col))];
}

bool hasMapMinotaur(const CustomMap& map, int row, int col)
{
    for (const int cell : map.minotaurCells)
    {
        if (cell == cellIndexOf(map, row, col))
        {
            return true;
        }
    }
    return false;
}

int countMapItems(const CustomMap& map, Item item)
{
    int count = 0;
    for (const Item placed : map.items)
    {
        count += placed == item ? 1 : 0;
    }
    return count;
}

int countUnpairedPads(const CustomMap& map)
{
    int count = 0;
    for (std::size_t i = 0; i < map.items.size(); ++i)
    {
        count += (map.items[i] == Item::Teleporter && map.links[i] < 0) ? 1 : 0;
    }
    return count;
}

// ---------------------------------------------------------------------------
// Editing
// ---------------------------------------------------------------------------

void markMapChanged(CustomMap& map)
{
    map.verified = false;
}

bool isEditablePassage(const CustomMap& map, int tileRow, int tileCol)
{
    const int tiles = getMapTileCount(map);
    if (tileRow <= 0 || tileCol <= 0 || tileRow >= tiles - 1 || tileCol >= tiles - 1)
    {
        return false; // outer wall
    }
    return (tileRow % 2) != (tileCol % 2); // between two cells (not a cell, not a pillar)
}

bool setMapPassage(CustomMap& map, int tileRow, int tileCol, Tile tile)
{
    if (!isShapeValid(map) || !isEditablePassage(map, tileRow, tileCol) || tile == Tile::Key)
    {
        return false;
    }
    if (getMapTile(map, tileRow, tileCol) == tile)
    {
        return false;
    }
    tileAt(map, tileRow, tileCol) = tile;
    markMapChanged(map);
    return true;
}

bool setMapKey(CustomMap& map, int row, int col, bool key)
{
    if (!isShapeValid(map) || !isCellInside(map, row, col) || isStartOrExit(map, row, col))
    {
        return false;
    }
    Tile& tile = tileAt(map, cellToTile(row), cellToTile(col));
    const Tile wanted = key ? Tile::Key : Tile::Path;
    if (tile == wanted)
    {
        return false;
    }
    if (key && getMapItem(map, row, col) != Item::None)
    {
        setMapItem(map, row, col, Item::None); // a key needs an empty cell
    }
    tile = wanted;
    markMapChanged(map);
    return true;
}

bool setMapItem(CustomMap& map, int row, int col, Item item)
{
    if (!isShapeValid(map) || !isCellInside(map, row, col) || isStartOrExit(map, row, col))
    {
        return false;
    }
    const int index = cellIndexOf(map, row, col);
    Item& placed = map.items[static_cast<std::size_t>(index)];
    if (item == Item::None)
    {
        bool changed = placed != Item::None;
        if (placed == Item::Teleporter)
        {
            unlinkPad(map, index);
        }
        placed = Item::None;
        if (getMapTile(map, cellToTile(row), cellToTile(col)) == Tile::Key)
        {
            tileAt(map, cellToTile(row), cellToTile(col)) = Tile::Path;
            changed = true;
        }
        for (std::size_t i = 0; i < map.minotaurCells.size(); ++i)
        {
            if (map.minotaurCells[i] == index)
            {
                map.minotaurCells.erase(map.minotaurCells.begin() + static_cast<std::ptrdiff_t>(i));
                changed = true;
                break;
            }
        }
        if (changed)
        {
            markMapChanged(map);
        }
        return changed;
    }
    if (placed == item)
    {
        return false;
    }
    if (item == Item::Gem && countMapItems(map, Item::Gem) >= MAX_MAP_GEMS)
    {
        return false;
    }
    if (item == Item::Teleporter && countMapItems(map, Item::Teleporter) >= 2 * MAX_MAP_TELEPORTER_PAIRS)
    {
        return false;
    }
    if (placed == Item::Teleporter)
    {
        unlinkPad(map, index);
    }
    if (getMapTile(map, cellToTile(row), cellToTile(col)) == Tile::Key)
    {
        tileAt(map, cellToTile(row), cellToTile(col)) = Tile::Path; // items and keys never share a cell
    }
    placed = item;
    if (item == Item::Teleporter)
    {
        // Pair with the first pad that is still waiting for a twin.
        for (int other = 0; other < static_cast<int>(map.items.size()); ++other)
        {
            if (other != index && map.items[static_cast<std::size_t>(other)] == Item::Teleporter &&
                map.links[static_cast<std::size_t>(other)] < 0)
            {
                map.links[static_cast<std::size_t>(other)] = index;
                map.links[static_cast<std::size_t>(index)] = other;
                break;
            }
        }
    }
    markMapChanged(map);
    return true;
}

bool toggleMapMinotaur(CustomMap& map, int row, int col)
{
    if (!isShapeValid(map) || !isCellInside(map, row, col) || isStartOrExit(map, row, col))
    {
        return false;
    }
    const int index = cellIndexOf(map, row, col);
    for (std::size_t i = 0; i < map.minotaurCells.size(); ++i)
    {
        if (map.minotaurCells[i] == index)
        {
            map.minotaurCells.erase(map.minotaurCells.begin() + static_cast<std::ptrdiff_t>(i));
            markMapChanged(map);
            return true;
        }
    }
    if (static_cast<int>(map.minotaurCells.size()) >= MAX_MAP_MINOTAURS)
    {
        return false;
    }
    map.minotaurCells.push_back(index);
    markMapChanged(map);
    return true;
}

// ---------------------------------------------------------------------------
// Checking
// ---------------------------------------------------------------------------

std::vector<std::string> findMapProblems(const CustomMap& map)
{
    std::vector<std::string> problems;
    Maze maze;
    if (!buildMaze(map, maze))
    {
        problems.push_back("The map data is broken.");
        return problems;
    }
    const int size = maze.size;
    int* withDoors = new int[size * size];
    int* withoutDoors = new int[size * size];
    computeDistances(maze, 0, 0, true, withDoors);
    computeDistances(maze, 0, 0, false, withoutDoors);
    const int exit = size * size - 1;

    if (withDoors[exit] < 0)
    {
        problems.push_back("The exit can't be reached from the start - open a path.");
    }
    else if (withoutDoors[exit] < 0)
    {
        // Only doors lead to the exit: there must be a key the player can reach
        // before the first door.
        bool reachableKey = false;
        for (int i = 0; i < size * size; ++i)
        {
            if (withoutDoors[i] >= 0 && getCellTile(maze, i / size, i % size) == Tile::Key)
            {
                reachableKey = true;
            }
        }
        if (!reachableKey)
        {
            problems.push_back("The exit is behind a door, but no key can be reached first.");
        }
    }
    if (countUnpairedPads(map) > 0)
    {
        problems.push_back("A teleporter pad has no twin - place another pad.");
    }
    if (map.config.requireAllGems)
    {
        if (countMapItems(map, Item::Gem) == 0)
        {
            problems.push_back("The exit is sealed until all gems are found, but there are no gems.");
        }
        for (int i = 0; i < size * size; ++i)
        {
            if (map.items[static_cast<std::size_t>(i)] == Item::Gem && withDoors[i] < 0)
            {
                problems.push_back("A gem can't be reached, so the sealed exit could never open.");
                break;
            }
        }
    }
    delete[] withDoors;
    delete[] withoutDoors;
    destroyMaze(maze);
    return problems;
}

unsigned int computeMapChecksum(const CustomMap& map)
{
    unsigned int hash = 2166136261u; // FNV-1a
    appendFnv(hash, gameplayText(map));
    return hash;
}

bool isMapVerified(const CustomMap& map)
{
    return map.verified && map.checksum == computeMapChecksum(map);
}

void markMapVerified(CustomMap& map, int moves, int seconds)
{
    map.verified = true;
    map.checksum = computeMapChecksum(map);
    map.verifiedMoves = moves;
    map.verifiedSeconds = seconds;
}

// ---------------------------------------------------------------------------
// Playing
// ---------------------------------------------------------------------------

bool startCustomRun(GameState& state, const CustomMap& map)
{
    endGame(state);
    if (!findMapProblems(map).empty() || !buildMaze(map, state.maze))
    {
        destroyMaze(state.maze);
        return false;
    }

    RunConfig config = map.config;
    config.seed = computeMapChecksum(map);
    config.minotaurCount = static_cast<int>(map.minotaurCells.size());
    config.gemCount = countMapItems(map, Item::Gem);
    config.teleporterPairs = countMapItems(map, Item::Teleporter) / 2;
    config.traps = countMapItems(map, Item::Spikes) > 0;
    config.powerUps = false;
    for (const Item item : map.items)
    {
        config.powerUps = config.powerUps || (item != Item::None && item != Item::Gem && item != Item::Spikes &&
                                              item != Item::Teleporter);
    }
    state.maze.seed = config.seed;
    initRunGrids(state, clampConfig(config));

    const int size = state.maze.size;
    for (int row = 0; row < size; ++row)
    {
        for (int col = 0; col < size; ++col)
        {
            state.items[row][col] = map.items[static_cast<std::size_t>(row * size + col)];
        }
    }
    for (int i = 0; i < size * size; ++i)
    {
        state.teleportLinks[i] = map.links[static_cast<std::size_t>(i)];
    }
    state.minotaurCount = clampInt(config.minotaurCount, 0, MAX_MAP_MINOTAURS);
    state.minotaurs = state.minotaurCount > 0 ? new Minotaur[state.minotaurCount] : nullptr;
    for (int i = 0; i < state.minotaurCount; ++i)
    {
        const int cell = map.minotaurCells[static_cast<std::size_t>(i)];
        placeMinotaur(state, i, cell / size, cell % size);
    }
    finishRunSetup(state);
    return true;
}

// ---------------------------------------------------------------------------
// Files
// ---------------------------------------------------------------------------

bool isValidMapName(const std::string& name)
{
    if (name.empty() || static_cast<int>(name.size()) > MAX_MAP_NAME_LENGTH)
    {
        return false;
    }
    if (name.front() == ' ' || name.back() == ' ')
    {
        return false;
    }
    bool hasLetterOrDigit = false;
    for (const char symbol : name)
    {
        const unsigned char character = static_cast<unsigned char>(symbol);
        if (std::isalnum(character))
        {
            hasLetterOrDigit = true;
        }
        else if (symbol != ' ' && symbol != '-' && symbol != '_')
        {
            return false;
        }
    }
    return hasLetterOrDigit;
}

std::string mapFileName(const std::string& name)
{
    std::string file = name;
    for (char& symbol : file)
    {
        if (symbol == ' ')
        {
            symbol = '_';
        }
    }
    return file + ".lmap";
}

std::string serializeMap(const CustomMap& map)
{
    std::ostringstream out;
    out << MAP_HEADER << '\n';
    out << "name=" << map.name << '\n';
    out << "author=" << map.author << '\n';
    out << "theme " << map.theme << '\n';
    out << gameplayText(map);
    out << "verified " << (map.verified ? 1 : 0) << ' ' << map.checksum << ' ' << map.verifiedMoves << ' '
        << map.verifiedSeconds << '\n';
    return out.str();
}

bool deserializeMap(const std::string& text, CustomMap& map)
{
    std::istringstream in(text);
    CustomMap loaded;
    if (!readMap(in, loaded))
    {
        return false;
    }
    map = loaded;
    return true;
}
