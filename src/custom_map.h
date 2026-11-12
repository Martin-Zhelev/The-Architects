// custom_map.h
// Hand-made mazes from the map maker. Each map is one text file (.lmap)
// that can be copied to a friend. Pure logic: no input, output or drawing.
//
// A map may only be published once its maker has beaten it in a test play,
// so every shared map is known to be solvable. The file stores a checksum of
// the map at the moment it was beaten: change anything (in the editor or by
// hand in the file) and the map has to be beaten again.

#ifndef CUSTOM_MAP_H
#define CUSTOM_MAP_H

#include "game.h"

#include <string>
#include <vector>

const int MAX_MAP_NAME_LENGTH = 24;
const int MAX_MAP_MINOTAURS = 3;
const int MAX_MAP_TELEPORTER_PAIRS = 3;
const int MAX_MAP_GEMS = 60;

struct CustomMap
{
    std::string name;
    std::string author;
    int theme = 0;                  // ThemeId
    RunConfig config;               // size, lives, Minotaur speed, fog, time limit, hints, sealed exit, gauntlet
    std::vector<Tile> tiles;        // tileCount x tileCount, row by row
    std::vector<Item> items;        // size x size
    std::vector<int> links;         // size x size: twin pad cell index or -1
    std::vector<int> minotaurCells; // cell indexes of Minotaur lairs
    bool verified = false;          // beaten by its maker
    unsigned int checksum = 0;      // checksum when it was beaten
    int verifiedMoves = 0;          // the maker's winning run
    int verifiedSeconds = 0;
};

// ---- Creating ----
// Every cell open, every passage a wall (open = false), or one big open room (open = true).
bool createBlankMap(CustomMap& map, int size, bool open);
// Starts from a generated maze (with its doors and keys) so makers can remix it.
bool createGeneratedMap(CustomMap& map, int size, unsigned int seed);

// ---- Looking at a map ----
int getMapTileCount(const CustomMap& map);
Tile getMapTile(const CustomMap& map, int tileRow, int tileCol);
Item getMapItem(const CustomMap& map, int row, int col);
bool hasMapMinotaur(const CustomMap& map, int row, int col);
int countMapItems(const CustomMap& map, Item item);
int countUnpairedPads(const CustomMap& map);

// ---- Editing (every function refuses illegal edits and returns false) ----
// Tiles between two cells: Wall, Path or Door. The outer wall, the pillars
// and the entrance/exit openings cannot be changed.
bool isEditablePassage(const CustomMap& map, int tileRow, int tileCol);
bool setMapPassage(CustomMap& map, int tileRow, int tileCol, Tile tile);
// A key on a cell (true) or none (false). Not on the start or the exit.
bool setMapKey(CustomMap& map, int row, int col, bool key);
// Places an item (Item::None clears the cell, keys and Minotaurs included).
// Teleporter pads pair up automatically: the first pad waits for a twin.
bool setMapItem(CustomMap& map, int row, int col, Item item);
bool toggleMapMinotaur(CustomMap& map, int row, int col);
// Any change to a map makes it unverified again.
void markMapChanged(CustomMap& map);

// ---- Checking ----
// Problems that stop a test play (an empty list means the map can be tested).
std::vector<std::string> findMapProblems(const CustomMap& map);
unsigned int computeMapChecksum(const CustomMap& map);
// True only if the map was beaten AND has not changed since.
bool isMapVerified(const CustomMap& map);
// Called when the maker wins the test play.
void markMapVerified(CustomMap& map, int moves, int seconds);

// ---- Playing ----
// Builds a running game from the map (items, pads, Minotaurs as placed).
bool startCustomRun(GameState& state, const CustomMap& map);

// ---- Files ----
bool isValidMapName(const std::string& name);
std::string mapFileName(const std::string& name); // "Spiral of Doom" -> "Spiral_of_Doom.lmap"
std::string serializeMap(const CustomMap& map);
bool deserializeMap(const std::string& text, CustomMap& map);

#endif // CUSTOM_MAP_H
