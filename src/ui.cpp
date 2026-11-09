// ui.cpp
// User interface: menus, screens, drawing and prompts.
// All console input and output of the game lives here (and in input.cpp);
// the rules themselves are in maze.cpp, game.cpp and number_trail.cpp.

#include "ui.h"

#include "input.h"
#include "number_trail.h"
#include "validation.h"

#include <chrono>
#include <iomanip>
#include <iostream>
#include <sstream>
#include <string>

namespace
{

using Clock = std::chrono::steady_clock;

// ---- Layout ----
const int MAZE_VIEW_TILES = 41;  // visible window for big mazes (fits a 20x20 maze fully)
const int TRAIL_VIEW_CELLS = 13; // visible window for the number grid

// ---- ANSI colour codes ----
const char* const COLOR_RESET = "\033[0m";
const char* const COLOR_TITLE = "\033[1;36m";
const char* const COLOR_DIM = "\033[2m";
const char* const COLOR_GOOD = "\033[1;32m";
const char* const COLOR_BAD = "\033[1;31m";
const char* const COLOR_ACCENT = "\033[1;33m";

// How a single maze tile looks on screen.
enum class TileLook
{
    Wall,
    Floor,
    Door,
    Key,
    Player,
    Exit,
    Hint,
    Trail
};

// ===========================================================================
// Small output helpers
// ===========================================================================

bool colorsOn(const Settings& settings)
{
    return settings.useColors && isAnsiSupported();
}

std::string paint(const std::string& text, const char* color, const Settings& settings)
{
    if (!colorsOn(settings))
    {
        return text;
    }
    return std::string(color) + text + COLOR_RESET;
}

// Starts a new page: everything from the previous page is removed.
void clearScreen()
{
    std::cout.flush();
    clearConsole();
}

// Positions are stored from 0 (row 0 = first row), but players count from 1,
// so the last square of a 20x20 grid is shown as "Row 20/20  Col 20/20".
std::string formatPosition(int row, int col, int size)
{
    return "Row " + std::to_string(row + 1) + "/" + std::to_string(size) + "  Col " + std::to_string(col + 1) +
           "/" + std::to_string(size);
}

void setCursorVisible(bool visible)
{
    if (isAnsiSupported())
    {
        std::cout << (visible ? "\033[?25h" : "\033[?25l") << std::flush;
    }
}

// Draws a whole frame at once. Instead of clearing the screen (which flickers)
// the cursor jumps home and every line is overwritten.
void presentFrame(const std::string& frame)
{
    if (!isAnsiSupported())
    {
        clearScreen();
        std::cout << frame << std::flush;
        return;
    }

    std::string output = "\033[H";
    for (const char symbol : frame)
    {
        if (symbol == '\n')
        {
            output += "\033[K"; // erase leftovers from the previous frame
        }
        output += symbol;
    }
    output += "\033[J";
    std::cout << output << std::flush;
}

std::string formatTime(long long seconds)
{
    std::ostringstream text;
    text << std::setfill('0') << std::setw(2) << seconds / 60 << ':' << std::setw(2) << seconds % 60;
    return text.str();
}

long long secondsSince(Clock::time_point start)
{
    return static_cast<long long>(std::chrono::duration_cast<std::chrono::seconds>(Clock::now() - start).count());
}

std::string onOff(bool value)
{
    return value ? "ON" : "OFF";
}

// ===========================================================================
// Line input with validation
// ===========================================================================

bool readLine(std::string& line)
{
    return static_cast<bool>(std::getline(std::cin, line));
}

void waitForEnter()
{
    std::cout << "\n  Press Enter to continue...";
    std::string ignored;
    readLine(ignored);
}

void waitForAnyKey()
{
    std::cout << "\n  Press any key to continue..." << std::flush;
    readKey();
}

// Keeps asking until the user types a whole number in [minValue, maxValue].
// Returns false only if the input stream has ended.
bool readIntInRange(const std::string& prompt, int minValue, int maxValue, int& value)
{
    while (true)
    {
        std::cout << prompt << std::flush;
        std::string line;
        if (!readLine(line))
        {
            return false;
        }
        if (parseInt(line, value) && isInRange(value, minValue, maxValue))
        {
            return true;
        }
        std::cout << "  Invalid input. Please enter a whole number from " << minValue << " to " << maxValue
                  << ".\n";
    }
}

// Asks for a seed. An empty answer means "surprise me".
bool readSeed(unsigned int& seed)
{
    while (true)
    {
        std::cout << "  Seed (a number to replay a maze, or just press Enter for a random one): " << std::flush;
        std::string line;
        if (!readLine(line))
        {
            return false;
        }
        if (trim(line).empty())
        {
            seed = generateRandomSeed();
            return true;
        }
        if (parseUnsigned(line, seed))
        {
            return true;
        }
        std::cout << "  Invalid seed. Use digits only (0 - 4294967295) or leave it empty.\n";
    }
}

bool readPlayerName(std::string& name)
{
    while (true)
    {
        std::cout << "  Enter your name (1-" << MAX_PLAYER_NAME_LENGTH << " letters, digits, spaces, - or _): "
                  << std::flush;
        std::string line;
        if (!readLine(line))
        {
            return false;
        }
        name = trim(line);
        if (isValidPlayerName(name))
        {
            return true;
        }
        std::cout << "  That name is not valid, please try again.\n";
    }
}

// ===========================================================================
// Title banner (block letters built at runtime from a small font table)
// ===========================================================================

const int LETTER_HEIGHT = 5;
const int TITLE_LENGTH = 9;
const char* const TITLE_FONT[TITLE_LENGTH][LETTER_HEIGHT] = {
    {"#    ", "#    ", "#    ", "#    ", "#####"}, // L
    {" ### ", "#   #", "#####", "#   #", "#   #"}, // A
    {"#### ", "#   #", "#### ", "#   #", "#### "}, // B
    {"#   #", " # # ", "  #  ", "  #  ", "  #  "}, // Y
    {"#### ", "#   #", "#### ", "#  # ", "#   #"}, // R
    {"###", " # ", " # ", " # ", "###"},           // I
    {"#   #", "##  #", "# # #", "#  ##", "#   #"}, // N
    {"#####", "  #  ", "  #  ", "  #  ", "  #  "}, // T
    {"#   #", "#   #", "#####", "#   #", "#   #"}, // H
};

std::string buildBanner(const Settings& settings)
{
    std::string banner;
    for (int line = 0; line < LETTER_HEIGHT; ++line)
    {
        std::string row = "   ";
        for (int letter = 0; letter < TITLE_LENGTH; ++letter)
        {
            row += TITLE_FONT[letter][line];
            row += "  ";
        }
        banner += paint(row, COLOR_TITLE, settings) + '\n';
    }
    return banner;
}

// ===========================================================================
// Maze drawing
// ===========================================================================

// The two characters shown for each kind of tile.
std::string tileGlyph(TileLook look, const Settings& settings)
{
    const bool color = colorsOn(settings);
    switch (look)
    {
    case TileLook::Wall:
        return color ? "\033[47m  \033[0m" : "##";
    case TileLook::Floor:
        return "  ";
    case TileLook::Door:
        return color ? "\033[30;45m[]\033[0m" : "[]";
    case TileLook::Key:
        return color ? "\033[1;33mo-\033[0m" : "o-";
    case TileLook::Player:
        return color ? "\033[1;30;43m()\033[0m" : "()";
    case TileLook::Exit:
        return color ? "\033[1;30;42mEX\033[0m" : "EX";
    case TileLook::Hint:
        return color ? "\033[1;36m::\033[0m" : "::";
    case TileLook::Trail:
        return color ? "\033[2m..\033[0m" : "..";
    }
    return "  ";
}

// Information about a specific element of the maze (used by the legend).
const char* describeTileLook(TileLook look)
{
    switch (look)
    {
    case TileLook::Wall:
        return "wall - you cannot walk through it";
    case TileLook::Floor:
        return "open corridor";
    case TileLook::Door:
        return "special door - a shortcut, opens with one key";
    case TileLook::Key:
        return "key - walk over it to pick it up";
    case TileLook::Player:
        return "you";
    case TileLook::Exit:
        return "the exit - reach it to win";
    case TileLook::Hint:
        return "hint - the shortest corridor route to the exit";
    case TileLook::Trail:
        return "your footprints - squares you already visited";
    }
    return "";
}

// Decides how one tile of the maze should look right now.
TileLook classifyTile(const GameState& state, const Settings& settings, int tileRow, int tileCol)
{
    if (tileRow == cellToTile(state.playerRow) && tileCol == cellToTile(state.playerCol))
    {
        return TileLook::Player;
    }
    const int exitTile = cellToTile(state.maze.size - 1);
    if (tileRow == exitTile && tileCol == exitTile)
    {
        return TileLook::Exit;
    }

    switch (state.maze.tiles[tileRow][tileCol])
    {
    case Tile::Wall:
        return TileLook::Wall;
    case Tile::Door:
        return TileLook::Door;
    case Tile::Key:
        return TileLook::Key;
    case Tile::Path:
        break;
    }

    if (state.hintVisible && state.hintTiles[tileRow][tileCol])
    {
        return TileLook::Hint;
    }
    const bool isCellTile = (tileRow % 2 == 1) && (tileCol % 2 == 1);
    if (settings.showTrail && isCellTile && state.visitedCells[tileRow / 2][tileCol / 2])
    {
        return TileLook::Trail;
    }
    return TileLook::Floor;
}

// Draws the maze (top-down view) into a string. `window` is the number of
// tiles visible per side; bigger mazes scroll so the player stays in view.
std::string renderMaze(const GameState& state, const Settings& settings, int window)
{
    const int total = state.maze.tileCount;
    const int visible = (total < window) ? total : window;
    const int firstRow = computeViewportStart(cellToTile(state.playerRow), total, window);
    const int firstCol = computeViewportStart(cellToTile(state.playerCol), total, window);

    std::string output;
    for (int row = firstRow; row < firstRow + visible; ++row)
    {
        output += "  ";
        for (int col = firstCol; col < firstCol + visible; ++col)
        {
            output += tileGlyph(classifyTile(state, settings, row, col), settings);
        }
        output += '\n';
    }
    return output;
}

std::string describeMoveResult(MoveResult result)
{
    switch (result)
    {
    case MoveResult::Moved:
        return "...";
    case MoveResult::PickedUpKey:
        return "You found a key! Keys open the special doors [].";
    case MoveResult::OpenedDoor:
        return "Click! You unlock a special door and take the shortcut.";
    case MoveResult::ReachedExit:
        return "You found the exit!";
    case MoveResult::BlockedByWall:
        return "Ouch - a wall blocks that way.";
    case MoveResult::BlockedByLockedDoor:
        return "This special door is locked. Find a key first.";
    case MoveResult::BlockedByEdge:
        return "That is the outer wall of the labyrinth.";
    case MoveResult::PickedUpItem:
        return "You picked something up!";
    case MoveResult::Teleported:
        return "Whoosh - teleported!";
    case MoveResult::Hurt:
        return "Ouch - a trap!";
    case MoveResult::Caught:
        return "The Minotaur caught you!";
    case MoveResult::ExitLocked:
        return "The exit is sealed - collect every gem first.";
    case MoveResult::GameOver:
        return "Game over.";
    }
    return "";
}

// Shows the maze together with information about the current position.
void drawLabyrinthScreen(const GameState& state, const Settings& settings, long long seconds,
                         const std::string& message)
{
    std::ostringstream frame;
    frame << "  " << paint("LABYRINTH", COLOR_TITLE, settings) << "   " << state.maze.size << "x"
          << state.maze.size << "   seed " << state.maze.seed << "\n\n";
    frame << renderMaze(state, settings, MAZE_VIEW_TILES) << '\n';
    frame << "  Moves: " << state.moves << "   Time: " << formatTime(seconds) << "   Keys: " << state.keysHeld
          << "   " << formatPosition(state.playerRow, state.playerCol, state.maze.size);
    if (state.maze.tileCount > MAZE_VIEW_TILES)
    {
        frame << paint("   [scrolling view - M for full map]", COLOR_DIM, settings);
    }
    frame << "\n  > " << message << "\n\n";
    frame << paint("  Arrows/WASD move   H hint   I inspect   M map   ? help   Q quit", COLOR_DIM, settings)
          << '\n';
    presentFrame(frame.str());
}

// ===========================================================================
// Screens
// ===========================================================================

std::string buildHelpText(const Settings& settings)
{
    std::ostringstream text;
    text << "\n  " << paint("HOW TO PLAY", COLOR_TITLE, settings) << "\n\n";
    text << "  " << paint("LABYRINTH", COLOR_ACCENT, settings) << '\n'
         << "    You start in the top-left corner (row 1, column 1). The exit is in the bottom-right corner.\n"
         << "    Move one square at a time with the arrow keys or W A S D. Walls block you.\n"
         << "    Special doors are shortcuts towards the exit, but each needs a key.\n"
         << "    There are fewer keys than doors, so choose your shortcut wisely.\n"
         << "    Score: fewer moves, less time and fewer hints = more points.\n\n"
         << "    Arrows/WASD move    H show/hide hint (-100 points)    I inspect location\n"
         << "    M full map          ? this help                       Q or Esc quit to menu\n\n";
    text << "  " << paint("NUMBER TRAIL", COLOR_ACCENT, settings) << '\n'
         << "    Every square holds a number. From your square you may move RIGHT (right arrow),\n"
         << "    DOWN (down arrow) or DIAGONALLY down-right (E, 3 or Page Down).\n"
         << "    Always step onto the SMALLEST number you can reach. "
         << TRAIL_MAX_MISTAKES << " wrong steps and you lose.\n\n";
    text << "  " << paint("LEGEND", COLOR_ACCENT, settings) << '\n';
    const TileLook legend[] = {TileLook::Player, TileLook::Exit, TileLook::Wall, TileLook::Door,
                               TileLook::Key,    TileLook::Hint, TileLook::Trail};
    for (const TileLook look : legend)
    {
        text << "    " << tileGlyph(look, settings) << "  " << describeTileLook(look) << '\n';
    }
    return text.str();
}

void showMainMenu(const Settings& settings)
{
    clearScreen();
    std::cout << '\n'
              << buildBanner(settings) << '\n'
              << "   " << paint("Find your way from the top-left corner to the exit in the bottom-right.",
                                COLOR_DIM, settings)
              << "\n\n"
              << "     1. Play Labyrinth\n"
              << "     2. Play Number Trail  (follow the smallest numbers)\n"
              << "     3. How to play\n"
              << "     4. Leaderboard\n"
              << "     5. Settings\n"
              << "     0. Exit\n\n";
}

void showLeaderboard(const AppContext& app)
{
    clearScreen();
    std::cout << "\n  " << paint("LEADERBOARD", COLOR_TITLE, app.settings) << "  (this session)\n\n";
    if (app.leaderboard.count == 0)
    {
        std::cout << "  No scores yet - escape a labyrinth to get on the board!\n";
    }
    else
    {
        std::cout << "  #  " << std::left << std::setw(14) << "Name" << std::setw(8) << "Maze" << std::setw(8)
                  << "Moves" << std::setw(8) << "Time" << "Score\n";
        std::cout << "  " << std::string(46, '-') << '\n';
        for (int i = 0; i < app.leaderboard.count; ++i)
        {
            const ScoreEntry& entry = app.leaderboard.entries[i];
            const std::string mazeText = std::to_string(entry.mazeSize) + "x" + std::to_string(entry.mazeSize);
            std::cout << "  " << i + 1 << "  " << std::left << std::setw(14) << entry.name << std::setw(8)
                      << mazeText << std::setw(8) << entry.moves << std::setw(8) << formatTime(entry.seconds)
                      << entry.score << '\n';
        }
        std::cout << std::right;
    }
    waitForEnter();
}

void showSettingsMenu(Settings& settings)
{
    while (true)
    {
        clearScreen();
        std::cout << "\n  " << paint("SETTINGS", COLOR_TITLE, settings) << "\n\n"
                  << "     1. Colours:               " << onOff(settings.useColors) << '\n'
                  << "     2. Show visited squares:  " << onOff(settings.showTrail) << '\n'
                  << "     0. Back\n\n";
        if (!isAnsiSupported())
        {
            std::cout << "  (This console does not support colours, so plain characters are used.)\n\n";
        }

        int choice = 0;
        if (!readIntInRange("  Choose an option (0-2): ", 0, 2, choice) || choice == 0)
        {
            return;
        }
        if (choice == 1)
        {
            settings.useColors = !settings.useColors;
        }
        else
        {
            settings.showTrail = !settings.showTrail;
        }
    }
}

const char* describePassage(const GameState& state, Direction direction)
{
    int rowOffset = 0;
    int colOffset = 0;
    getDirectionOffset(direction, rowOffset, colOffset);
    if (!isInsideMaze(state.maze, state.playerRow + rowOffset, state.playerCol + colOffset))
    {
        return "outer wall";
    }
    switch (getPassageTile(state.maze, state.playerRow, state.playerCol, direction))
    {
    case Tile::Wall:
        return "wall";
    case Tile::Door:
        return state.keysHeld > 0 ? "special door (you have a key!)" : "special door (locked)";
    case Tile::Path:
    case Tile::Key:
        break;
    }
    return "open corridor";
}

// Information about the current location and the inventory.
void showLocationInfo(const GameState& state, const Settings& settings)
{
    clearScreen();
    const int last = state.maze.size - 1;
    std::cout << "\n  " << paint("LOCATION", COLOR_TITLE, settings) << "\n\n"
              << "    You are at row " << state.playerRow + 1 << " of " << state.maze.size << ", column "
              << state.playerCol + 1 << " of " << state.maze.size << ".\n"
              << "    The exit is " << last - state.playerRow << " rows down and " << last - state.playerCol
              << " columns to the right.\n\n"
              << "    Up    : " << describePassage(state, Direction::Up) << '\n'
              << "    Down  : " << describePassage(state, Direction::Down) << '\n'
              << "    Left  : " << describePassage(state, Direction::Left) << '\n'
              << "    Right : " << describePassage(state, Direction::Right) << "\n\n"
              << "  " << paint("INVENTORY", COLOR_TITLE, settings) << "\n\n"
              << "    Keys held      : " << state.keysHeld << '\n'
              << "    Doors opened   : " << state.doorsOpened << " (maze has " << state.maze.doorCount
              << " doors and " << state.maze.keyCount << " keys)\n"
              << "    Hints used     : " << state.hintsUsed << '\n'
              << "    Squares visited: " << countVisitedCells(state) << " of " << state.maze.size * state.maze.size
              << '\n';
    waitForAnyKey();
    clearScreen();
}

void showFullMap(const GameState& state, const Settings& settings)
{
    clearScreen();
    std::cout << "\n  " << paint("FULL MAP", COLOR_TITLE, settings) << "\n\n"
              << renderMaze(state, settings, state.maze.tileCount);
    waitForAnyKey();
    clearScreen();
}

void showHelpInGame(const Settings& settings)
{
    clearScreen();
    std::cout << buildHelpText(settings);
    waitForAnyKey();
    clearScreen();
}

std::string rateRun(const GameState& state)
{
    if (state.moves < state.optimalMoves)
    {
        return "Shortcut master - you beat the corridor route!";
    }
    if (state.moves == state.optimalMoves)
    {
        return "Perfect run - not a single wasted step!";
    }
    if (state.moves <= state.optimalMoves * 2)
    {
        return "Great navigation!";
    }
    return "You made it - the labyrinth didn't stand a chance.";
}

void showLabyrinthVictory(AppContext& app, const GameState& state, long long seconds)
{
    clearScreen();
    const int score = calculateScore(state, seconds);
    std::cout << "\n  " << paint("*** YOU ESCAPED THE LABYRINTH! ***", COLOR_GOOD, app.settings) << "\n\n"
              << "    " << rateRun(state) << "\n\n"
              << "    Maze           : " << state.maze.size << "x" << state.maze.size << "  (seed "
              << state.maze.seed << ")\n"
              << "    Exit reached   : " << formatPosition(state.playerRow, state.playerCol, state.maze.size) << '\n'
              << "    Moves          : " << state.moves << '\n'
              << "    Corridor route : " << state.optimalMoves << " moves\n"
              << "    Time           : " << formatTime(seconds) << '\n'
              << "    Doors opened   : " << state.doorsOpened << '\n'
              << "    Hints used     : " << state.hintsUsed << '\n'
              << "    " << paint("Score          : " + std::to_string(score), COLOR_ACCENT, app.settings)
              << "\n\n";

    if (qualifiesForLeaderboard(app.leaderboard, score))
    {
        std::cout << "  New leaderboard score!\n";
        std::string name;
        if (readPlayerName(name))
        {
            ScoreEntry entry;
            entry.name = name;
            entry.mazeSize = state.maze.size;
            entry.moves = state.moves;
            entry.seconds = seconds;
            entry.score = score;
            addToLeaderboard(app.leaderboard, entry);
            std::cout << "  Saved. Well done, " << name << "!\n";
        }
    }
    waitForEnter();
}

// ===========================================================================
// Labyrinth game loop
// ===========================================================================

bool keyToDirection(Key key, Direction& direction)
{
    switch (key)
    {
    case Key::Up:
        direction = Direction::Up;
        return true;
    case Key::Down:
        direction = Direction::Down;
        return true;
    case Key::Left:
        direction = Direction::Left;
        return true;
    case Key::Right:
        direction = Direction::Right;
        return true;
    default:
        return false;
    }
}

void playLabyrinth(AppContext& app)
{
    clearScreen();
    std::cout << "\n  " << paint("NEW LABYRINTH", COLOR_TITLE, app.settings) << "\n\n";
    int size = 0;
    const std::string sizePrompt =
        "  Maze size (" + std::to_string(MIN_MAZE_SIZE) + "-" + std::to_string(MAX_MAZE_SIZE) + "): ";
    unsigned int seed = 0;
    if (!readIntInRange(sizePrompt, MIN_MAZE_SIZE, MAX_MAZE_SIZE, size) || !readSeed(seed))
    {
        return;
    }

    GameState state;
    if (!startGame(state, size, seed))
    {
        std::cout << "  Could not create the maze.\n";
        waitForEnter();
        return;
    }

    const Clock::time_point startTime = Clock::now();
    std::string message = "Find the exit in the bottom-right corner. Good luck!";
    bool quit = false;

    enableRawInput();
    setCursorVisible(false);
    clearScreen();

    while (!state.won && !quit)
    {
        drawLabyrinthScreen(state, app.settings, secondsSince(startTime), message);
        const Key key = readKey();

        Direction direction = Direction::Up;
        if (keyToDirection(key, direction))
        {
            message = describeMoveResult(movePlayer(state, direction));
            continue;
        }

        switch (key)
        {
        case Key::Hint:
            toggleHint(state);
            message = state.hintVisible ? "Hint on: follow the :: marks (-100 points)." : "Hint hidden.";
            break;
        case Key::Inspect:
            showLocationInfo(state, app.settings);
            break;
        case Key::Map:
            showFullMap(state, app.settings);
            break;
        case Key::Help:
            showHelpInGame(app.settings);
            break;
        case Key::Quit:
            drawLabyrinthScreen(state, app.settings, secondsSince(startTime),
                                "Quit to the menu? Press Y to quit, any other key to keep playing.");
            {
                const Key answer = readKey();
                quit = (answer == Key::Yes || answer == Key::Quit);
            }
            message = "Back to the maze!";
            break;
        default:
            message = "Unknown key. Press ? for help.";
            break;
        }
    }

    const long long seconds = secondsSince(startTime);
    if (state.won)
    {
        drawLabyrinthScreen(state, app.settings, seconds, describeMoveResult(MoveResult::ReachedExit));
        waitForAnyKey();
    }
    disableRawInput();
    setCursorVisible(true);

    if (state.won)
    {
        showLabyrinthVictory(app, state, seconds);
    }
    endGame(state);
}

// ===========================================================================
// Number Trail mode
// ===========================================================================

std::string renderNumberTrail(const NumberTrail& trail, const Settings& settings)
{
    const int digits = countDigits(trail.maxValue);
    const int visible = (trail.size < TRAIL_VIEW_CELLS) ? trail.size : TRAIL_VIEW_CELLS;
    const int firstRow = computeViewportStart(trail.row, trail.size, TRAIL_VIEW_CELLS);
    const int firstCol = computeViewportStart(trail.col, trail.size, TRAIL_VIEW_CELLS);
    const bool color = colorsOn(settings);

    std::string output;
    for (int row = firstRow; row < firstRow + visible; ++row)
    {
        output += "  ";
        for (int col = firstCol; col < firstCol + visible; ++col)
        {
            std::ostringstream number;
            number << std::setw(digits) << trail.values[row][col];

            // Brackets mark special squares, so the game also works without colours.
            char left = ' ';
            char right = ' ';
            const char* style = nullptr;
            if (row == trail.row && col == trail.col)
            {
                left = '[';
                right = ']';
                style = "\033[1;30;43m";
            }
            else if (isReachableFromPlayer(trail, row, col))
            {
                left = '<';
                right = '>';
                style = "\033[1;36m";
            }
            else if (row == trail.size - 1 && col == trail.size - 1)
            {
                left = '{';
                right = '}';
                style = "\033[1;30;42m";
            }
            else if (trail.visited[row][col])
            {
                style = "\033[32m";
            }
            else
            {
                style = "\033[2m";
            }

            const std::string cell = std::string(1, left) + number.str() + right;
            output += color ? std::string(style) + cell + COLOR_RESET : cell;
            output += ' ';
        }
        output += '\n';
    }
    return output;
}

void drawTrailScreen(const NumberTrail& trail, const Settings& settings, long long seconds,
                     const std::string& message)
{
    std::ostringstream frame;
    frame << "  " << paint("NUMBER TRAIL", COLOR_TITLE, settings) << "   " << trail.size << "x" << trail.size
          << "\n\n";
    frame << renderNumberTrail(trail, settings) << '\n';
    frame << "  Moves: " << trail.moves << "   Mistakes: " << trail.mistakes << "/" << TRAIL_MAX_MISTAKES
          << "   Time: " << formatTime(seconds) << "   " << formatPosition(trail.row, trail.col, trail.size)
          << '\n';
    frame << "  > " << message << "\n\n";
    frame << paint("  [ ] you   < > squares you can reach   { } exit", COLOR_DIM, settings) << '\n';
    frame << paint("  Right/Down arrows move   E, 3 or PgDn diagonal   ? help   Q quit", COLOR_DIM, settings)
          << '\n';
    presentFrame(frame.str());
}

std::string describeTrailResult(TrailResult result, const NumberTrail& trail)
{
    switch (result)
    {
    case TrailResult::Correct:
        return "Correct! Keep following the smallest numbers.";
    case TrailResult::Wrong:
        return "Wrong - that was not the smallest reachable number. Mistakes: " + std::to_string(trail.mistakes) +
               "/" + std::to_string(TRAIL_MAX_MISTAKES);
    case TrailResult::OutOfBounds:
        return "You can't leave the grid that way.";
    case TrailResult::Finished:
        return "You reached the exit!";
    case TrailResult::GameOver:
        return "Too many mistakes - the trail is lost.";
    }
    return "";
}

void playNumberTrail(AppContext& app)
{
    clearScreen();
    std::cout << "\n  " << paint("NUMBER TRAIL", COLOR_TITLE, app.settings) << "\n\n"
              << "  Move right, down or diagonally down-right, always onto the SMALLEST number\n"
              << "  you can reach. Get from the top-left to the bottom-right corner.\n\n";
    int size = 0;
    const std::string sizePrompt =
        "  Grid size (" + std::to_string(MIN_MAZE_SIZE) + "-" + std::to_string(MAX_MAZE_SIZE) + "): ";
    unsigned int seed = 0;
    if (!readIntInRange(sizePrompt, MIN_MAZE_SIZE, MAX_MAZE_SIZE, size) || !readSeed(seed))
    {
        return;
    }

    NumberTrail trail;
    if (!createNumberTrail(trail, size, seed))
    {
        std::cout << "  Could not create the grid.\n";
        waitForEnter();
        return;
    }

    const Clock::time_point startTime = Clock::now();
    std::string message = "Pick the smallest of the highlighted < > numbers.";
    bool quit = false;

    enableRawInput();
    setCursorVisible(false);
    clearScreen();

    while (!trail.finished && !trail.lost && !quit)
    {
        drawTrailScreen(trail, app.settings, secondsSince(startTime), message);
        const Key key = readKey();
        switch (key)
        {
        case Key::Right:
            message = describeTrailResult(makeTrailStep(trail, TrailStep::Right), trail);
            break;
        case Key::Down:
            message = describeTrailResult(makeTrailStep(trail, TrailStep::Down), trail);
            break;
        case Key::Diagonal:
            message = describeTrailResult(makeTrailStep(trail, TrailStep::Diagonal), trail);
            break;
        case Key::Up:
        case Key::Left:
            message = "In Number Trail you can only go right, down or diagonally down-right.";
            break;
        case Key::Help:
            showHelpInGame(app.settings);
            break;
        case Key::Quit:
            drawTrailScreen(trail, app.settings, secondsSince(startTime),
                            "Quit to the menu? Press Y to quit, any other key to keep playing.");
            {
                const Key answer = readKey();
                quit = (answer == Key::Yes || answer == Key::Quit);
            }
            message = "Back to the trail!";
            break;
        default:
            message = "Unknown key. Press ? for help.";
            break;
        }
    }

    const long long seconds = secondsSince(startTime);
    if (!quit)
    {
        drawTrailScreen(trail, app.settings, seconds,
                        trail.finished ? "You reached the exit!" : "Too many mistakes - the trail is lost.");
        waitForAnyKey();
    }
    disableRawInput();
    setCursorVisible(true);

    if (!quit)
    {
        clearScreen();
        if (trail.finished)
        {
            std::cout << "\n  " << paint("*** TRAIL COMPLETE! ***", COLOR_GOOD, app.settings) << "\n\n"
                      << "    Grid     : " << trail.size << "x" << trail.size << '\n'
                      << "    Finished : " << formatPosition(trail.row, trail.col, trail.size) << '\n'
                      << "    Moves    : " << trail.moves << '\n'
                      << "    Mistakes : " << trail.mistakes << '\n'
                      << "    Time     : " << formatTime(seconds) << '\n'
                      << "    Score    : " << calculateTrailScore(trail, seconds) << '\n';
        }
        else
        {
            std::cout << "\n  " << paint("*** GAME OVER ***", COLOR_BAD, app.settings) << "\n\n"
                      << "    You made " << TRAIL_MAX_MISTAKES << " mistakes after " << trail.moves
                      << " correct steps.\n"
                      << "    Stopped at " << formatPosition(trail.row, trail.col, trail.size) << " of a "
                      << trail.size << "x" << trail.size << " grid.\n"
                      << "    Tip: compare all the < > numbers before you move.\n";
        }
        waitForEnter();
    }
    destroyNumberTrail(trail);
}

} // namespace

// ===========================================================================
// Main menu
// ===========================================================================

void runApplication()
{
    AppContext app;
    bool running = true;

    while (running)
    {
        showMainMenu(app.settings);
        int choice = 0;
        if (!readIntInRange("   Choose an option (0-5): ", 0, 5, choice))
        {
            break; // input stream closed
        }

        switch (choice)
        {
        case 1:
            playLabyrinth(app);
            break;
        case 2:
            playNumberTrail(app);
            break;
        case 3:
            clearScreen();
            std::cout << buildHelpText(app.settings);
            waitForEnter();
            break;
        case 4:
            showLeaderboard(app);
            break;
        case 5:
            showSettingsMenu(app.settings);
            break;
        default:
            running = false;
            break;
        }
    }

    clearScreen();
    std::cout << "  Thanks for playing Labyrinth. Bye!\n";
}
