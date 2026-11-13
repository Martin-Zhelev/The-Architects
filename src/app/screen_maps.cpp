// screen_maps.cpp
// The map maker: a list of hand-made maps (each one its own .lmap file in
// the maps/ folder next to the game) and the editor.
//
// Publishing rule: a map can only be played from the list once its maker
// has beaten it in a TEST PLAY. The file remembers a checksum of the map at
// that moment, so changing it later (or editing the file by hand) turns it
// back into a draft that has to be beaten again. No unbeatable maps!

#include "app.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <filesystem>

namespace
{

// ---------------------------------------------------------------------------
// The map list
// ---------------------------------------------------------------------------

struct MapEntry
{
    std::string path;
    std::string fileName;
    CustomMap map;
    bool readable = false;
};

std::vector<MapEntry> entries;
int listScroll = 0;
int confirmDelete = -1;
int newMapSize = 15;
int newMapStyle = 0; // 0 walls everywhere, 1 open room, 2 generated maze
bool listDirty = true;

const char* const STYLE_NAMES[3] = {"All walls (carve it)", "Open room (build it)", "Random maze (remix it)"};
const char* const STYLE_SHORT[3] = {"Walls", "Open", "Maze"};
const char* const THEME_NAMES[THEME_COUNT] = {"Neon", "Dungeon", "Lava", "Ice", "Candy"};

void reloadList()
{
    entries.clear();
    const std::string folder = mapsDirectory();
    FilePathList files = LoadDirectoryFilesEx(folder.c_str(), ".lmap", false);
    for (unsigned int i = 0; i < files.count; ++i)
    {
        MapEntry entry;
        entry.path = files.paths[i];
        entry.fileName = GetFileName(files.paths[i]);
        std::string text;
        entry.readable = readTextFile(entry.path, text) && deserializeMap(text, entry.map);
        entries.push_back(entry);
    }
    UnloadDirectoryFiles(files);
    std::sort(entries.begin(), entries.end(),
              [](const MapEntry& a, const MapEntry& b) { return a.fileName < b.fileName; });
    listDirty = false;
}

// ---------------------------------------------------------------------------
// The editor
// ---------------------------------------------------------------------------

enum class Tool
{
    Open,  // carve passages
    Wall,  // build walls
    Door,
    Key,
    Gem,
    Spikes,
    Teleporter,
    Minotaur,
    Torch,
    Boots,
    Hammer,
    Hourglass,
    Heart,
    Compass,
    Eraser
};

const int TOOL_COUNT = 15;
const char* const TOOL_NAMES[TOOL_COUNT] = {"OPEN", "WALL", "DOOR", "KEY", "GEM", "SPIKES", "PORTAL", "MINOTAUR",
                                            "TORCH", "BOOTS", "HAMMER", "HOURGLASS", "HEART", "COMPASS", "ERASE"};
const char* const TOOL_HELP[TOOL_COUNT] = {
    "Drag across cells to carve corridors between them.",
    "Drag across cells to build walls between them.",
    "Click a wall between two cells to put a locked door there.",
    "Click a cell to drop a key. Each key opens one door.",
    "Click a cell to place a gem (+50 points).",
    "Click a cell to place a spike trap.",
    "Click two cells: each pair of portals gets its own colour.",
    "Click a cell to make a Minotaur lair (max 3).",
    "Torch: see further in fog of war.",
    "Speed Boots: run twice as fast for a while.",
    "Hammer: smash one wall (-40 points).",
    "Hourglass: freezes the Minotaurs.",
    "Heart: one extra life.",
    "Compass: shows the way to the nearest gem.",
    "Click or drag to clear cells (right mouse button erases too)."};

Tool tool = Tool::Open;
bool rulesTab = false;
float zoom = 0.0f; // tile size in pixels, 0 = fit to the canvas
Vector2 pan{0.0f, 0.0f};
int lastCell = -1;          // cell the drag came from (for carving)
bool dragging = false;
std::vector<CustomMap> undoStack;
std::string statusText;
Color statusColor = WHITE;
float statusTimer = 0.0f;

void setStatus(const std::string& text, Color color)
{
    statusText = text;
    statusColor = color;
    statusTimer = 4.0f;
}

Item itemForTool(Tool t)
{
    switch (t)
    {
    case Tool::Gem:
        return Item::Gem;
    case Tool::Spikes:
        return Item::Spikes;
    case Tool::Teleporter:
        return Item::Teleporter;
    case Tool::Torch:
        return Item::Torch;
    case Tool::Boots:
        return Item::Boots;
    case Tool::Hammer:
        return Item::Hammer;
    case Tool::Hourglass:
        return Item::Hourglass;
    case Tool::Heart:
        return Item::Heart;
    case Tool::Compass:
        return Item::Compass;
    default:
        return Item::None;
    }
}

void pushUndo()
{
    undoStack.push_back(app.editorMap);
    if (undoStack.size() > 40)
    {
        undoStack.erase(undoStack.begin());
    }
}

// Same numbering as the game (render3d.cpp teleporterPairIndex), but for the map.
int mapPairIndex(const CustomMap& map, int cell)
{
    const int twin = map.links[static_cast<std::size_t>(cell)];
    if (twin < 0)
    {
        return -1;
    }
    const int key = std::min(cell, twin);
    int index = 0;
    for (int i = 0; i < key; ++i)
    {
        if (map.items[static_cast<std::size_t>(i)] == Item::Teleporter && map.links[static_cast<std::size_t>(i)] > i)
        {
            ++index;
        }
    }
    return index;
}

struct CanvasLayout
{
    Rectangle area;
    float tile;
    float ox;
    float oy;
};

CanvasLayout layoutCanvas(Rectangle area)
{
    const int tiles = getMapTileCount(app.editorMap);
    const float fit = std::fmin(area.width, area.height) / static_cast<float>(tiles);
    CanvasLayout layout;
    layout.area = area;
    layout.tile = zoom > 0.0f ? zoom : fit;
    layout.ox = area.x + (area.width - layout.tile * tiles) * 0.5f + pan.x;
    layout.oy = area.y + (area.height - layout.tile * tiles) * 0.5f + pan.y;
    return layout;
}

void applyToolAt(int tileRow, int tileCol, bool erase)
{
    CustomMap& map = app.editorMap;
    const int size = map.config.size;
    const bool isCell = tileRow % 2 == 1 && tileCol % 2 == 1;
    const int row = tileRow / 2;
    const int col = tileCol / 2;
    const Tool used = erase ? Tool::Eraser : tool;

    if (used == Tool::Open || used == Tool::Wall)
    {
        const Tile tile = used == Tool::Open ? Tile::Path : Tile::Wall;
        if (isCell)
        {
            const int cell = row * size + col;
            if (lastCell >= 0 && lastCell != cell)
            {
                // A fast drag can jump several cells in one frame: walk from the
                // last cell to this one, one neighbour at a time, so no cell in
                // between is skipped.
                int fromRow = lastCell / size;
                int fromCol = lastCell % size;
                while (fromRow != row || fromCol != col)
                {
                    const int dr = row - fromRow;
                    const int dc = col - fromCol;
                    int stepRow = 0;
                    int stepCol = 0;
                    if (std::abs(dr) >= std::abs(dc))
                    {
                        stepRow = dr > 0 ? 1 : -1;
                    }
                    else
                    {
                        stepCol = dc > 0 ? 1 : -1;
                    }
                    setMapPassage(map, cellToTile(fromRow) + stepRow, cellToTile(fromCol) + stepCol, tile);
                    fromRow += stepRow;
                    fromCol += stepCol;
                }
            }
            lastCell = cell;
        }
        else
        {
            setMapPassage(map, tileRow, tileCol, tile);
        }
        return;
    }
    if (used == Tool::Door)
    {
        if (!isCell && isEditablePassage(map, tileRow, tileCol) && !dragging)
        {
            const Tile now = getMapTile(map, tileRow, tileCol);
            setMapPassage(map, tileRow, tileCol, now == Tile::Door ? Tile::Path : Tile::Door);
        }
        return;
    }
    if (!isCell)
    {
        if (used == Tool::Eraser)
        {
            setMapPassage(map, tileRow, tileCol, Tile::Path);
        }
        return;
    }
    const bool startOrExit = (row == 0 && col == 0) || (row == size - 1 && col == size - 1);
    if (startOrExit)
    {
        if (!dragging)
        {
            setStatus("The start and the exit must stay empty.", Color{255, 140, 120, 255});
        }
        return;
    }
    switch (used)
    {
    case Tool::Key:
        if (!dragging)
        {
            setMapKey(map, row, col, getMapTile(map, tileRow, tileCol) != Tile::Key);
        }
        break;
    case Tool::Minotaur:
        if (!dragging && !toggleMapMinotaur(map, row, col))
        {
            setStatus("At most 3 Minotaurs.", Color{255, 140, 120, 255});
        }
        break;
    case Tool::Eraser:
        setMapItem(map, row, col, Item::None);
        break;
    default:
    {
        const Item item = itemForTool(used);
        if (dragging && item == Item::Teleporter)
        {
            break; // portals are placed one click at a time
        }
        if (getMapItem(map, row, col) == item && !dragging)
        {
            setMapItem(map, row, col, Item::None); // click again to remove
        }
        else if (!setMapItem(map, row, col, item) && item == Item::Teleporter)
        {
            setStatus("At most 3 portal pairs.", Color{255, 140, 120, 255});
        }
        break;
    }
    }
}

void drawCanvas(const CanvasLayout& layout)
{
    const CustomMap& map = app.editorMap;
    const Theme& t = getThemeData(static_cast<ThemeId>(map.theme));
    const int tiles = getMapTileCount(map);
    const int size = map.config.size;
    DrawRectangleRec(layout.area, Color{6, 6, 14, 255});
    BeginScissorMode(static_cast<int>(layout.area.x), static_cast<int>(layout.area.y), static_cast<int>(layout.area.width),
                     static_cast<int>(layout.area.height));
    const float s = layout.tile;
    for (int r = 0; r < tiles; ++r)
    {
        for (int c = 0; c < tiles; ++c)
        {
            const Rectangle rect{layout.ox + static_cast<float>(c) * s, layout.oy + static_cast<float>(r) * s, s + 0.5f,
                                 s + 0.5f};
            if (rect.x > layout.area.x + layout.area.width || rect.y > layout.area.y + layout.area.height ||
                rect.x + s < layout.area.x || rect.y + s < layout.area.y)
            {
                continue;
            }
            const Tile tile = getMapTile(map, r, c);
            Color color{24, 22, 40, 255};
            if (tile == Tile::Wall)
            {
                color = lerpColor(t.wallTop, Color{60, 60, 80, 255}, 0.45f);
            }
            else if (tile == Tile::Door)
            {
                color = t.door;
            }
            DrawRectangleRec(rect, color);
        }
    }
    // Cell contents
    for (int row = 0; row < size; ++row)
    {
        for (int col = 0; col < size; ++col)
        {
            const Vector2 center{layout.ox + (static_cast<float>(cellToTile(col)) + 0.5f) * s,
                                 layout.oy + (static_cast<float>(cellToTile(row)) + 0.5f) * s};
            const int cell = row * size + col;
            if (getMapTile(map, cellToTile(row), cellToTile(col)) == Tile::Key)
            {
                drawKeyIcon(center, s * 1.3f, t.key);
            }
            const Item item = map.items[static_cast<std::size_t>(cell)];
            if (item == Item::Teleporter)
            {
                const int pair = mapPairIndex(map, cell);
                const Color pad = pair < 0 ? Color{140, 140, 150, 255} : teleporterPairColor(pair);
                DrawCircleV(center, s * 0.62f, pad);
                DrawCircleV(center, s * 0.3f, Color{20, 10, 30, 255});
                const std::string label = pair < 0 ? "?" : std::to_string(pair + 1);
                if (s >= 9.0f)
                {
                    drawTextCentered(FontKind::Display, label.c_str(), center.x, center.y - s * 0.4f, s * 0.8f, WHITE);
                }
            }
            else if (item == Item::Spikes)
            {
                DrawRectangleRec(Rectangle{center.x - s * 0.45f, center.y - s * 0.45f, s * 0.9f, s * 0.9f},
                                 Color{220, 50, 40, 255});
                DrawTriangle(Vector2{center.x, center.y - s * 0.35f}, Vector2{center.x - s * 0.3f, center.y + s * 0.3f},
                             Vector2{center.x + s * 0.3f, center.y + s * 0.3f}, Color{255, 230, 230, 255});
            }
            else if (item != Item::None)
            {
                drawItemIcon(item, center, s * 1.4f, app.time);
            }
            if (hasMapMinotaur(map, row, col))
            {
                drawMinotaurIcon(center, s * 1.6f, Color{255, 70, 50, 255});
            }
        }
    }
    // Start and exit
    const Vector2 start{layout.ox + 1.5f * s, layout.oy + 1.5f * s};
    DrawRing(start, s * 0.5f, s * 0.75f, 0, 360, 24, t.player);
    const Vector2 exit{layout.ox + (static_cast<float>(cellToTile(size - 1)) + 0.5f) * s,
                       layout.oy + (static_cast<float>(cellToTile(size - 1)) + 0.5f) * s};
    drawExitIcon(exit, s * 1.5f, t.exit, app.time);

    // Hovered tile
    const Vector2 mouse = GetMousePosition();
    if (CheckCollisionPointRec(mouse, layout.area))
    {
        const int c = static_cast<int>(std::floor((mouse.x - layout.ox) / s));
        const int r = static_cast<int>(std::floor((mouse.y - layout.oy) / s));
        if (r >= 0 && c >= 0 && r < tiles && c < tiles)
        {
            DrawRectangleLinesEx(Rectangle{layout.ox + static_cast<float>(c) * s, layout.oy + static_cast<float>(r) * s, s, s},
                                 std::fmax(1.0f, s * 0.12f), WHITE);
        }
    }
    EndScissorMode();
    DrawRectangleLinesEx(layout.area, px(2), t.accent);
}

void handleCanvasInput(const CanvasLayout& layout)
{
    const Vector2 mouse = GetMousePosition();
    const bool inside = CheckCollisionPointRec(mouse, layout.area);
    const int tiles = getMapTileCount(app.editorMap);

    // Zoom with the wheel (around the mouse), pan with the middle button or Space + drag.
    const float wheel = inside ? GetMouseWheelMove() : 0.0f;
    if (wheel != 0.0f)
    {
        const float old = layout.tile;
        const float fit = std::fmin(layout.area.width, layout.area.height) / static_cast<float>(tiles);
        float next = old * (wheel > 0.0f ? 1.2f : 1.0f / 1.2f);
        next = std::fmax(fit, std::fmin(px(48), next));
        zoom = next;
        // keep the point under the mouse in place
        const float worldX = (mouse.x - layout.ox) / old;
        const float worldY = (mouse.y - layout.oy) / old;
        const float centeredX = layout.area.x + (layout.area.width - next * tiles) * 0.5f;
        const float centeredY = layout.area.y + (layout.area.height - next * tiles) * 0.5f;
        pan.x = mouse.x - worldX * next - centeredX;
        pan.y = mouse.y - worldY * next - centeredY;
        if (next <= fit + 0.01f)
        {
            zoom = 0.0f;
            pan = Vector2{0, 0};
        }
    }
    const bool panning = IsMouseButtonDown(MOUSE_BUTTON_MIDDLE) || (IsKeyDown(KEY_SPACE) && IsMouseButtonDown(MOUSE_BUTTON_LEFT));
    if (panning)
    {
        pan = Vector2Add(pan, GetMouseDelta());
        return;
    }

    const bool leftPressed = IsMouseButtonPressed(MOUSE_BUTTON_LEFT);
    const bool rightPressed = IsMouseButtonPressed(MOUSE_BUTTON_RIGHT);
    const bool leftDown = IsMouseButtonDown(MOUSE_BUTTON_LEFT);
    const bool rightDown = IsMouseButtonDown(MOUSE_BUTTON_RIGHT);
    if (!leftDown && !rightDown)
    {
        dragging = false;
        lastCell = -1;
        return;
    }
    if (!inside)
    {
        return;
    }
    if (leftPressed || rightPressed)
    {
        pushUndo();
        dragging = false;
        lastCell = -1;
    }
    else if (!dragging)
    {
        dragging = true;
    }
    const int c = static_cast<int>(std::floor((mouse.x - layout.ox) / layout.tile));
    const int r = static_cast<int>(std::floor((mouse.y - layout.oy) / layout.tile));
    if (r < 0 || c < 0 || r >= tiles || c >= tiles)
    {
        return;
    }
    static int lastTileRow = -1;
    static int lastTileCol = -1;
    if (dragging && r == lastTileRow && c == lastTileCol)
    {
        return; // still on the same tile
    }
    lastTileRow = r;
    lastTileCol = c;
    applyToolAt(r, c, rightDown && !leftDown);
}

// ---------------------------------------------------------------------------
// Tutorial: a guided first map. A card under the canvas explains one step at
// a time, a gold frame points at the button to use, and each step moves on
// by itself once the map shows it was done.
// ---------------------------------------------------------------------------

enum class TutorialTarget
{
    None,
    ToolOpen,
    ToolWall,
    ToolGem,
    ToolDoor,
    ToolPortal,
    ToolSpikes,
    RulesName,
    Problems,
    TestPlay
};

struct TutorialStep
{
    const char* title;
    const char* text;
    TutorialTarget target;
};

const TutorialStep TUTORIAL[] = {
    {"WELCOME TO THE MAP MAKER",
     "Let's build a small map together. The grid is your map: the ring in the top-left corner is the START, "
     "the glowing door in the bottom-right is the EXIT. Purple squares are walls. Click NEXT to begin.",
     TutorialTarget::None},
    {"1. CARVE CORRIDORS",
     "Pick the OPEN tool, then hold the left mouse button and drag from cell to cell. Every cell you drag "
     "across is joined to the last one. Start at the ring and carve a few corridors.",
     TutorialTarget::ToolOpen},
    {"2. BUILD A WALL",
     "Pick WALL and drag across two joined cells to close the gap between them again. The right mouse button "
     "erases anything, and Ctrl+Z undoes your last change.",
     TutorialTarget::ToolWall},
    {"3. PLACE A GEM",
     "Pick GEM and click a cell. Gems are worth points, and in RULES you can even seal the exit until every "
     "gem is found.",
     TutorialTarget::ToolGem},
    {"4. A DOOR AND ITS KEY",
     "Pick DOOR and click the gap between two cells on a corridor. Then pick KEY and drop a key on a cell the "
     "player reaches BEFORE that door - one key opens one door.",
     TutorialTarget::ToolDoor},
    {"5. A PAIR OF PORTALS",
     "Pick PORTAL and click two cells. They become a pair with their own colour and number: stepping on one "
     "takes you to the other. Tip: put portals at the end of a corridor.",
     TutorialTarget::ToolPortal},
    {"6. DANGER (OPTIONAL)",
     "Add SPIKES that rise and fall, a MINOTAUR lair (max 3), or power-ups like BOOTS and TORCH. "
     "Click NEXT when your map is exciting enough.",
     TutorialTarget::ToolSpikes},
    {"7. NAME YOUR MAP",
     "Open the RULES tab and type a name. Here you also choose the world, lives, Minotaur speed, fog of war, "
     "a time limit and whether the exit is sealed until all gems are found.",
     TutorialTarget::RulesName},
    {"8. MAKE IT POSSIBLE",
     "A red line above TEST PLAY tells you what still stops the map from working - usually that there is no "
     "way from the start to the exit yet. Carve until the red line is gone.",
     TutorialTarget::Problems},
    {"9. BEAT YOUR OWN MAP",
     "Press TEST PLAY and reach the exit. A map only becomes VERIFIED, playable and shareable once its maker "
     "has beaten it - that is how nobody can share an impossible map.",
     TutorialTarget::TestPlay},
    {"DONE - YOUR MAP IS PUBLISHED!",
     "It is saved as a .lmap file in the maps folder. Send that file to a friend: they drop it onto the game "
     "window and it appears in their list. Change the map later and you must beat it again.",
     TutorialTarget::None},
};
const int TUTORIAL_STEPS = static_cast<int>(sizeof(TUTORIAL) / sizeof(TUTORIAL[0]));

int tutorialStep = -1;     // -1 = no tutorial
float tutorialDoneTime = 0.0f; // > 0: the step was just completed (auto-advance)
int tutorialBaseOpen = 0;  // passage counts when the step began
int tutorialBaseWalls = 0;

struct PassageCounts
{
    int open = 0;
    int walls = 0;
    int doors = 0;
};

PassageCounts countPassages(const CustomMap& map)
{
    PassageCounts counts;
    const int tiles = getMapTileCount(map);
    for (int r = 1; r < tiles - 1; ++r)
    {
        for (int c = 1; c < tiles - 1; ++c)
        {
            if (!isEditablePassage(map, r, c))
            {
                continue;
            }
            const Tile tile = getMapTile(map, r, c);
            counts.open += tile == Tile::Path ? 1 : 0;
            counts.walls += tile == Tile::Wall ? 1 : 0;
            counts.doors += tile == Tile::Door ? 1 : 0;
        }
    }
    return counts;
}

int countKeys(const CustomMap& map)
{
    int keys = 0;
    for (const Tile tile : map.tiles)
    {
        keys += tile == Tile::Key ? 1 : 0;
    }
    return keys;
}

void setTutorialStep(int step)
{
    tutorialStep = step;
    tutorialDoneTime = 0.0f;
    const PassageCounts counts = countPassages(app.editorMap);
    tutorialBaseOpen = counts.open;
    tutorialBaseWalls = counts.walls;
}

void endTutorial()
{
    tutorialStep = -1;
    app.profile.mapTutorialDone = true;
    saveProfile();
}

// Has the player done what the current step asks?
bool isTutorialStepDone(const CustomMap& map)
{
    const PassageCounts counts = countPassages(map);
    switch (tutorialStep)
    {
    case 1:
        return counts.open >= tutorialBaseOpen + 4;
    case 2:
        // Carving after the step began lowers the count, so compare with the
        // lowest count seen: any wall built after that counts.
        tutorialBaseWalls = std::min(tutorialBaseWalls, counts.walls);
        return counts.walls > tutorialBaseWalls;
    case 3:
        return countMapItems(map, Item::Gem) > 0;
    case 4:
        return counts.doors > 0 && countKeys(map) > 0;
    case 5:
        return countMapItems(map, Item::Teleporter) >= 2 && countUnpairedPads(map) == 0;
    case 7:
        return isValidMapName(map.name);
    case 8:
        return findMapProblems(map).empty();
    case 9:
        return isMapVerified(map);
    default:
        return false; // welcome, optional and final steps use NEXT
    }
}

// Start a tutorial on a fresh 10x10 map with walls everywhere.
void startTutorialMap()
{
    CustomMap map;
    createBlankMap(map, 10, false);
    map.author = app.profile.name;
    map.theme = app.profile.settings.theme;
    app.editorMap = map;
    app.editorFile.clear();
    setTutorialStep(0);
    goTo(ScreenId::Editor);
}

void drawHighlight(Rectangle target, float time)
{
    const float pulse = 0.5f + 0.5f * std::sin(time * 6.0f);
    const float grow = px(4.0f) + pulse * px(4.0f);
    const Rectangle frame{target.x - grow, target.y - grow, target.width + grow * 2.0f, target.height + grow * 2.0f};
    DrawRectangleRoundedLinesEx(frame, 0.25f, 8, px(3.0f), Color{255, 215, 60, static_cast<unsigned char>(150 + pulse * 105)});
}

bool saveDraft()
{
    CustomMap& map = app.editorMap;
    if (!isValidMapName(map.name))
    {
        setStatus("Give the map a name first (1-24 letters, digits, spaces, - or _).", Color{255, 120, 100, 255});
        return false;
    }
    if (map.author.empty())
    {
        map.author = app.profile.name;
    }
    if (!saveCustomMap(map))
    {
        setStatus("Could not write the map file.", Color{255, 120, 100, 255});
        return false;
    }
    setStatus(isMapVerified(map) ? "Saved. This map is verified and can be shared."
                                 : "Saved as a draft. Beat it in TEST PLAY to publish it.",
              Color{120, 255, 160, 255});
    return true;
}

} // namespace

// ---------------------------------------------------------------------------
// Shared helpers
// ---------------------------------------------------------------------------

std::string mapsDirectory()
{
    // On this device, next to the saves (see setupSaveFolder in main.cpp).
    std::string folder = (app.dataDir.empty() ? std::string(GetApplicationDirectory()) : app.dataDir) + "maps";
    try
    {
        std::filesystem::create_directories(folder);
    }
    catch (const std::exception&)
    {
        folder = "maps";
        try
        {
            std::filesystem::create_directories(folder);
        }
        catch (const std::exception&)
        {
            folder = ".";
        }
    }
    return folder;
}

bool saveCustomMap(CustomMap& map)
{
    if (!isValidMapName(map.name))
    {
        return false;
    }
    const std::string fileName = mapFileName(map.name);
    const bool ok = writeTextFile(mapsDirectory() + "/" + fileName, serializeMap(map));
    if (ok && !app.editorFile.empty() && app.editorFile != fileName && &map == &app.editorMap)
    {
        std::remove((mapsDirectory() + "/" + app.editorFile).c_str()); // renamed
    }
    if (ok && &map == &app.editorMap)
    {
        app.editorFile = fileName;
    }
    listDirty = true;
    return ok;
}

void launchCustomMap(const CustomMap& map, bool testPlay)
{
    app.pendingMap = map;
    app.pendingRun.mode = RunMode::Custom;
    app.pendingRun.index = 0;
    app.pendingRun.theme = static_cast<ThemeId>(map.theme);
    app.pendingRun.config = map.config;
    app.pendingRun.testPlay = testPlay;
    app.resumeRequested = false;
    goTo(ScreenId::Game);
}

void importDroppedMaps()
{
    FilePathList dropped = LoadDroppedFiles();
    int imported = 0;
    int rejected = 0;
    for (unsigned int i = 0; i < dropped.count; ++i)
    {
        const char* path = dropped.paths[i];
        if (!IsFileExtension(path, ".lmap"))
        {
            ++rejected;
            continue;
        }
        std::string text;
        CustomMap map;
        if (!readTextFile(path, text) || !deserializeMap(text, map) ||
            !writeTextFile(mapsDirectory() + "/" + mapFileName(map.name), serializeMap(map)))
        {
            ++rejected;
            continue;
        }
        ++imported;
    }
    UnloadDroppedFiles(dropped);
    if (imported > 0)
    {
        showToast("MAP ADDED", std::to_string(imported) + " map(s) copied into the maps folder", Color{120, 255, 160, 255});
        playSfx(Sfx::PowerUp);
    }
    if (rejected > 0)
    {
        showToast("NOT A MAP", "Only valid .lmap files can be added", Color{255, 90, 90, 255});
    }
    listDirty = true;
}

// ---------------------------------------------------------------------------
// Maps screen
// ---------------------------------------------------------------------------

void enterMaps()
{
    reloadList();
    confirmDelete = -1;
    listScroll = 0;
}

void updateMaps(float dt)
{
    if (listDirty)
    {
        reloadList();
    }
    drawMenuBackground(dt);
    const Theme& menu = getThemeData(menuTheme());
    beginUi(menu.accent, menu.accent2, menu.panel);
    if (drawScreenHeader("MAP MAKER", "Build mazes, beat them, share the .lmap files with friends"))
    {
        goTo(ScreenId::MainMenu);
    }
    const float w = static_cast<float>(ui.width);
    const float listX = w * 0.5f - px(600);
    const float listW = px(760);
    const float rowH = px(58);
    const int visibleRows = 7;
    float y = px(124);

    const Rectangle listPanel{listX - px(10), y - px(10), listW + px(20), rowH * visibleRows + px(16) * (visibleRows - 1) * 0.3f + px(30)};
    drawPanel(listPanel, withAlpha(menu.panel, 0.85f), menu.accent);
    if (entries.empty())
    {
        drawTextCentered(FontKind::Body, "No maps yet. Make one on the right, or drag a .lmap file onto this window.",
                         listX + listW * 0.5f, y + px(140), px(22), Color{210, 210, 230, 255});
    }
    listScroll = std::max(0, std::min(listScroll, static_cast<int>(entries.size()) - visibleRows));
    for (int i = listScroll; i < static_cast<int>(entries.size()) && i < listScroll + visibleRows; ++i)
    {
        MapEntry& entry = entries[static_cast<std::size_t>(i)];
        const bool verified = entry.readable && isMapVerified(entry.map);
        std::string subtitle;
        if (!entry.readable)
        {
            subtitle = "Damaged file - " + entry.fileName;
        }
        else
        {
            subtitle = "by " + (entry.map.author.empty() ? std::string("?") : entry.map.author) + "   " +
                       std::to_string(entry.map.config.size) + "x" + std::to_string(entry.map.config.size) + "   " +
                       (verified ? "VERIFIED - beaten in " + std::to_string(entry.map.verifiedMoves) + " steps"
                                 : "DRAFT - its maker has not beaten it yet");
        }
        const std::string title = entry.readable ? entry.map.name : entry.fileName;
        const Rectangle row{listX, y, listW - px(370), rowH};
        if (button(title.c_str(), row, entry.readable, subtitle.c_str()) && verified)
        {
            launchCustomMap(entry.map, false);
        }
        drawStar(Vector2{row.x + row.width - px(22), row.y + px(20)}, px(10),
                 verified ? Color{90, 255, 150, 255} : Color{90, 90, 100, 255});
        if (button("PLAY", Rectangle{listX + listW - px(360), y + px(6), px(110), rowH - px(12)}, verified))
        {
            launchCustomMap(entry.map, false);
        }
        if (button("EDIT", Rectangle{listX + listW - px(240), y + px(6), px(110), rowH - px(12)}, entry.readable))
        {
            app.editorMap = entry.map;
            app.editorFile = entry.fileName;
            goTo(ScreenId::Editor);
        }
        if (button(confirmDelete == i ? "SURE?" : "DELETE", Rectangle{listX + listW - px(120), y + px(6), px(120), rowH - px(12)}))
        {
            if (confirmDelete == i)
            {
                std::remove(entry.path.c_str());
                confirmDelete = -1;
                listDirty = true;
                showToast("MAP DELETED", title, Color{255, 120, 100, 255});
            }
            else
            {
                confirmDelete = i;
            }
        }
        y += rowH + px(5);
    }
    if (static_cast<int>(entries.size()) > visibleRows)
    {
        const float sy = listPanel.y + listPanel.height + px(8);
        if (button("UP", Rectangle{listX, sy, px(120), px(40)}, listScroll > 0))
        {
            --listScroll;
        }
        if (button("DOWN", Rectangle{listX + px(130), sy, px(120), px(40)},
                   listScroll + visibleRows < static_cast<int>(entries.size())))
        {
            ++listScroll;
        }
    }
    const float wheel = GetMouseWheelMove();
    if (wheel != 0.0f)
    {
        listScroll -= wheel > 0.0f ? 1 : -1;
    }

    // New map panel
    const float rx = listX + listW + px(40);
    const float rw = px(400);
    const Rectangle create{rx, px(114), rw, px(330)};
    drawPanel(create, withAlpha(menu.panel, 0.92f), menu.accent2);
    drawText(FontKind::Display, "NEW MAP", rx + px(20), create.y + px(16), px(22), menu.accent3);
    slider("Size", Rectangle{rx + px(20), create.y + px(60), rw - px(40), px(50)}, newMapSize, MIN_MAZE_SIZE, MAX_MAZE_SIZE, "");
    stepper("Start", Rectangle{rx + px(20), create.y + px(120), rw - px(40), px(50)}, newMapStyle, 0, 2, STYLE_SHORT);
    drawTextCentered(FontKind::Body, STYLE_NAMES[newMapStyle], rx + rw * 0.5f, create.y + px(178), px(20),
                     Color{220, 220, 240, 255});
    if (bigButton("CREATE", Rectangle{rx + px(20), create.y + px(220), rw - px(40), px(58)}, menu.accent3))
    {
        CustomMap map;
        if (newMapStyle == 2)
        {
            createGeneratedMap(map, newMapSize, generateRandomSeed());
        }
        else
        {
            createBlankMap(map, newMapSize, newMapStyle == 1);
        }
        map.author = app.profile.name;
        map.name = "";
        map.theme = app.profile.settings.theme;
        app.editorMap = map;
        app.editorFile.clear();
        goTo(ScreenId::Editor);
    }
    drawWrappedText(FontKind::Body, "Every map is a .lmap file. Send it to a friend; they drop it onto the game window.",
                    rx + px(20), create.y + px(290), rw - px(40), px(17), Color{200, 200, 220, 220});

    const Rectangle tutorialButton{rx, px(458), rw, px(54)};
    if (!app.profile.mapTutorialDone)
    {
        drawHighlight(tutorialButton, app.time); // new here? start with the tutorial
    }
    if (bigButton("TUTORIAL: YOUR FIRST MAP", tutorialButton, Color{255, 215, 60, 255}))
    {
        startTutorialMap();
    }
    if (button("OPEN MAPS FOLDER", Rectangle{rx, px(522), rw, px(46)}))
    {
        OpenURL(mapsDirectory().c_str());
    }
    drawWrappedText(FontKind::Body,
                    "Only VERIFIED maps can be played: the maker must beat a map in TEST PLAY first, so every shared map "
                    "is known to be possible. Change a map and it must be beaten again.",
                    rx, px(580), rw, px(17), Color{210, 210, 230, 230});
    endUi();
}

// ---------------------------------------------------------------------------
// Editor screen
// ---------------------------------------------------------------------------

namespace
{
void updateTutorialCard(Rectangle card, const Rectangle* toolRects, Rectangle toolsTab, Rectangle rulesTab_,
                        Rectangle nameField, Rectangle problemLine, Rectangle testButton, float dt)
{
    const CustomMap& map = app.editorMap;
    const TutorialStep& step = TUTORIAL[tutorialStep];

    // Point at the button to use (or at the tab that shows it).
    auto toolTarget = [&](Tool which) { return rulesTab ? toolsTab : toolRects[static_cast<int>(which)]; };
    switch (step.target)
    {
    case TutorialTarget::ToolOpen:
        drawHighlight(toolTarget(Tool::Open), app.time);
        break;
    case TutorialTarget::ToolWall:
        drawHighlight(toolTarget(Tool::Wall), app.time);
        break;
    case TutorialTarget::ToolGem:
        drawHighlight(toolTarget(Tool::Gem), app.time);
        break;
    case TutorialTarget::ToolDoor:
        drawHighlight(toolTarget(countPassages(map).doors > 0 ? Tool::Key : Tool::Door), app.time);
        break;
    case TutorialTarget::ToolPortal:
        drawHighlight(toolTarget(Tool::Teleporter), app.time);
        break;
    case TutorialTarget::ToolSpikes:
        drawHighlight(toolTarget(Tool::Spikes), app.time);
        if (!rulesTab)
        {
            drawHighlight(toolRects[static_cast<int>(Tool::Minotaur)], app.time);
        }
        break;
    case TutorialTarget::RulesName:
        drawHighlight(rulesTab ? nameField : rulesTab_, app.time);
        break;
    case TutorialTarget::Problems:
        if (!findMapProblems(map).empty())
        {
            drawHighlight(problemLine, app.time);
        }
        break;
    case TutorialTarget::TestPlay:
        drawHighlight(testButton, app.time);
        break;
    case TutorialTarget::None:
        break;
    }

    // Step finished? Say so, then move on by itself.
    const bool done = isTutorialStepDone(map);
    if (done)
    {
        tutorialDoneTime += dt;
        if (tutorialDoneTime > 1.2f && tutorialStep + 1 < TUTORIAL_STEPS)
        {
            playSfx(Sfx::PowerUp, 1.2f, 0.6f);
            setTutorialStep(tutorialStep + 1);
            return;
        }
    }

    drawPanel(card, Color{12, 10, 26, 240}, Color{255, 215, 60, 255});
    const std::string counter = "TUTORIAL  " + std::to_string(tutorialStep + 1) + " / " + std::to_string(TUTORIAL_STEPS);
    drawText(FontKind::Body, counter.c_str(), card.x + px(18), card.y + px(10), px(16), Color{255, 215, 60, 220});
    drawText(FontKind::Display, step.title, card.x + px(18), card.y + px(30), px(22), WHITE);
    const float textW = card.width - px(230);
    drawWrappedText(FontKind::Body, step.text, card.x + px(18), card.y + px(62), textW, px(18), Color{220, 220, 240, 255});
    if (done && tutorialStep + 1 < TUTORIAL_STEPS)
    {
        drawText(FontKind::Display, "DONE!", card.x + card.width - px(320), card.y + px(30), px(22), Color{90, 255, 150, 255});
    }

    const float bx = card.x + card.width - px(196);
    const bool last = tutorialStep + 1 >= TUTORIAL_STEPS;
    if (button(last ? "FINISH" : "NEXT", Rectangle{bx, card.y + px(18), px(180), px(48)}))
    {
        if (last)
        {
            endTutorial();
            return;
        }
        setTutorialStep(tutorialStep + 1);
        return;
    }
    if (button(tutorialStep > 0 ? "BACK" : "SKIP TUTORIAL", Rectangle{bx, card.y + px(74), px(180), px(44)}))
    {
        if (tutorialStep > 0)
        {
            setTutorialStep(tutorialStep - 1);
        }
        else
        {
            endTutorial();
        }
        return;
    }
    // Close the tutorial from any step (it can be restarted with "? TUTORIAL").
    if (button("X", Rectangle{bx - px(52), card.y + px(18), px(42), px(42)}))
    {
        endTutorial();
    }
}
} // namespace

void enterEditor()
{
    if (app.editorMap.tiles.empty())
    {
        createBlankMap(app.editorMap, 15, false);
        app.editorMap.author = app.profile.name;
    }
    undoStack.clear();
    zoom = 0.0f;
    pan = Vector2{0, 0};
    dragging = false;
    lastCell = -1;
    statusTimer = 0.0f;
    if (isMapVerified(app.editorMap))
    {
        setStatus("Verified! This map can be played and shared.", Color{90, 255, 150, 255});
    }
}

void updateEditor(float dt)
{
    CustomMap& map = app.editorMap;
    const Theme& t = getThemeData(static_cast<ThemeId>(map.theme));
    ClearBackground(Color{10, 8, 20, 255});
    DrawRectangleGradientV(0, 0, ui.width, ui.height, withAlpha(t.skyTop, 0.6f), withAlpha(BLACK, 1.0f));
    beginUi(t.accent, t.accent2, t.panel);
    statusTimer = std::fmax(0.0f, statusTimer - dt);

    const float w = static_cast<float>(ui.width);
    const float h = static_cast<float>(ui.height);
    const float panelW = px(420);
    const bool tutorial = tutorialStep >= 0;
    const float cardH = px(150);
    const Rectangle canvas{px(16), px(78), w - panelW - px(48), h - px(126) - (tutorial ? cardH - px(30) : 0.0f)};
    const Rectangle card{canvas.x, canvas.y + canvas.height + px(10), canvas.width, cardH - px(14)};
    const CanvasLayout layout = layoutCanvas(canvas);
    // Where the tutorial may point this frame.
    Rectangle toolRects[TOOL_COUNT] = {};
    Rectangle toolsTabRect{};
    Rectangle rulesTabRect{};
    Rectangle nameRect{};
    Rectangle problemRect{};
    Rectangle testRect{};

    // Header
    const std::string title = map.name.empty() ? std::string("UNTITLED MAP") : map.name;
    drawText(FontKind::Display, title.c_str(), px(170), px(22), px(28), WHITE);
    const bool verified = isMapVerified(map);
    drawText(FontKind::Body, verified ? "VERIFIED - ready to share" : "DRAFT - beat it in TEST PLAY to publish",
             px(170), px(52), px(18), verified ? Color{90, 255, 150, 255} : Color{255, 200, 120, 255});
    const bool back = button("< MAPS", Rectangle{px(16), px(16), px(140), px(46)}) || (ui.back && ui.activeField == nullptr);
    if (!tutorial && button("? TUTORIAL", Rectangle{canvas.x + canvas.width - px(170), px(16), px(170), px(46)}))
    {
        setTutorialStep(1); // explain the editor with the map that is open
    }

    // Right panel
    const float rx = w - panelW - px(16);
    const Rectangle side{rx, px(16), panelW, h - px(64)};
    drawPanel(side, withAlpha(t.panel, 0.94f), t.accent);
    const float bx = rx + px(14);
    const float bw = panelW - px(28);
    toolsTabRect = Rectangle{bx, px(28), bw * 0.5f - px(4), px(40)};
    rulesTabRect = Rectangle{bx + bw * 0.5f + px(4), px(28), bw * 0.5f - px(4), px(40)};
    if (button("TOOLS", toolsTabRect))
    {
        rulesTab = false;
    }
    if (button("RULES", rulesTabRect))
    {
        rulesTab = true;
    }
    DrawRectangleRec(Rectangle{rulesTab ? bx + bw * 0.5f + px(4) : bx, px(70), bw * 0.5f - px(4), px(4)}, t.accent3);
    float y = px(86);

    if (!rulesTab)
    {
        const float cellW = (bw - px(16)) / 3.0f;
        for (int i = 0; i < TOOL_COUNT; ++i)
        {
            const Rectangle r{bx + static_cast<float>(i % 3) * (cellW + px(8)), y + static_cast<float>(i / 3) * px(48), cellW,
                              px(42)};
            toolRects[i] = r;
            const Color saved = ui.accent;
            if (static_cast<int>(tool) == i)
            {
                ui.accent = t.accent3;
                DrawRectangleRounded(Rectangle{r.x - px(3), r.y - px(3), r.width + px(6), r.height + px(6)}, 0.3f, 6,
                                     withAlpha(t.accent3, 0.6f));
            }
            if (button(TOOL_NAMES[i], r))
            {
                tool = static_cast<Tool>(i);
            }
            ui.accent = saved;
        }
        y += px(48) * 5 + px(6);
        drawWrappedText(FontKind::Body, TOOL_HELP[static_cast<int>(tool)], bx, y, bw, px(18), Color{220, 220, 240, 255});
        y += px(50);
        const std::string counts = "Gems " + std::to_string(countMapItems(map, Item::Gem)) + "   Portals " +
                                   std::to_string(countMapItems(map, Item::Teleporter)) + "/6   Minotaurs " +
                                   std::to_string(map.minotaurCells.size()) + "/3";
        drawText(FontKind::Body, counts.c_str(), bx, y, px(18), t.accent2);
        y += px(26);
        drawText(FontKind::Body, "Right mouse: erase   Wheel: zoom   Middle / Space+drag: pan   Ctrl+Z: undo", bx, y,
                 px(14), Color{180, 180, 200, 220});
    }
    else
    {
        const float rowH = px(42);
        const float step = rowH + px(6);
        CustomMap before = map;
        nameRect = Rectangle{bx, y, bw, rowH};
        textField("Name", nameRect, map.name, MAX_MAP_NAME_LENGTH, false, "my map");
        y += step;
        stepper("World", Rectangle{bx, y, bw, rowH}, map.theme, 0, THEME_COUNT - 1, THEME_NAMES);
        y += step;
        stepper("Lives", Rectangle{bx, y, bw, rowH}, map.config.lives, 1, 5);
        y += step;
        static const float SPEEDS[5] = {0.8f, 0.65f, 0.55f, 0.45f, 0.38f};
        static const char* const SPEED_NAMES[5] = {"Slow", "Steady", "Normal", "Fast", "Brutal"};
        int speed = 2;
        for (int i = 0; i < 5; ++i)
        {
            speed = std::fabs(SPEEDS[i] - map.config.minotaurStepSeconds) < 0.01f ? i : speed;
        }
        if (stepper("Minotaur", Rectangle{bx, y, bw, rowH}, speed, 0, 4, SPEED_NAMES))
        {
            map.config.minotaurStepSeconds = SPEEDS[speed];
        }
        y += step;
        static const int LIMITS[5] = {0, 60, 120, 180, 300};
        static const char* const LIMIT_NAMES[5] = {"None", "1:00", "2:00", "3:00", "5:00"};
        int limit = 0;
        for (int i = 0; i < 5; ++i)
        {
            limit = LIMITS[i] == map.config.timeLimitSeconds ? i : limit;
        }
        if (stepper("Time limit", Rectangle{bx, y, bw, rowH}, limit, 0, 4, LIMIT_NAMES))
        {
            map.config.timeLimitSeconds = LIMITS[limit];
        }
        y += step;
        toggle("Fog of war", Rectangle{bx, y, bw, rowH}, map.config.fogOfWar);
        y += step;
        toggle("Exit sealed until all gems", Rectangle{bx, y, bw, rowH}, map.config.requireAllGems);
        y += step;
        toggle("Gauntlet (Minotaurs chase)", Rectangle{bx, y, bw, rowH}, map.config.minotaurChase);
        y += step;
        toggle("Hints allowed", Rectangle{bx, y, bw, rowH}, map.config.hintsAllowed);
        y += step;
        if (computeMapChecksum(before) != computeMapChecksum(map))
        {
            markMapChanged(map); // rules changed: has to be beaten again
        }
    }

    // Problems and actions at the bottom of the panel
    const std::vector<std::string> problems = findMapProblems(map);
    const float actionsY = side.y + side.height - px(132);
    problemRect = Rectangle{bx, actionsY - px(48), bw, px(42)};
    testRect = Rectangle{bx, actionsY, bw, px(56)};
    if (!problems.empty())
    {
        drawWrappedText(FontKind::Body, ("! " + problems.front()).c_str(), bx, actionsY - px(46), bw, px(17),
                        Color{255, 120, 100, 255});
    }
    const bool canTest = problems.empty() && isValidMapName(map.name);
    if (problems.empty() && !isValidMapName(map.name))
    {
        drawWrappedText(FontKind::Body, "Name your map in RULES to test it.", bx, actionsY - px(30), bw, px(17),
                        Color{255, 200, 120, 255});
    }
    if (bigButton("TEST PLAY", Rectangle{bx, actionsY, bw, px(56)}, Color{90, 255, 150, 255}, canTest))
    {
        if (saveDraft())
        {
            launchCustomMap(map, true);
        }
    }
    if (button("SAVE", Rectangle{bx, actionsY + px(66), bw * 0.5f - px(4), px(48)}))
    {
        saveDraft();
    }
    if (button("UNDO", Rectangle{bx + bw * 0.5f + px(4), actionsY + px(66), bw * 0.5f - px(4), px(48)}, !undoStack.empty()) ||
        ((IsKeyDown(KEY_LEFT_CONTROL) || IsKeyDown(KEY_RIGHT_CONTROL)) && IsKeyPressed(KEY_Z) && !undoStack.empty()))
    {
        map = undoStack.back();
        undoStack.pop_back();
    }

    // Canvas
    if ((ui.activeField == nullptr || !CheckCollisionPointRec(GetMousePosition(), side)) &&
        !(tutorial && CheckCollisionPointRec(GetMousePosition(), card)))
    {
        handleCanvasInput(layout);
    }
    drawCanvas(layout);
    if (tutorial)
    {
        updateTutorialCard(card, toolRects, toolsTabRect, rulesTabRect, nameRect, problemRect, testRect, dt);
    }
    else
    {
    const std::string footer = std::to_string(map.config.size) + "x" + std::to_string(map.config.size) + "   by " +
                               (map.author.empty() ? app.profile.name : map.author) +
                               "   -   green ring = start, top-left; the exit is bottom-right";
    drawText(FontKind::Body, footer.c_str(), canvas.x, canvas.y + canvas.height + px(10), px(18), Color{200, 200, 220, 220});
    }
    if (statusTimer > 0.0f)
    {
        const Vector2 size = measure(FontKind::Body, statusText.c_str(), px(22));
        const Rectangle box{canvas.x + canvas.width * 0.5f - size.x * 0.5f - px(16), canvas.y + px(12), size.x + px(32), px(38)};
        drawPanel(box, Color{0, 0, 0, 210}, statusColor);
        drawTextCentered(FontKind::Body, statusText.c_str(), canvas.x + canvas.width * 0.5f, box.y + px(7), px(22),
                         withAlpha(statusColor, std::fmin(1.0f, statusTimer)));
    }

    if (back)
    {
        tutorialStep = -1; // leaving the editor ends the tutorial (it can be restarted)
        if (isValidMapName(map.name))
        {
            saveDraft(); // never lose work
        }
        goTo(ScreenId::Maps);
    }
    endUi();
}
