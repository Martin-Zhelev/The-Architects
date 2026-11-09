// ui.h
// User interface: menus, drawing the maze, prompts and input validation loops.
// Together with input.cpp this is the only part of the program that uses
// cin / cout.

#ifndef UI_H
#define UI_H

#include "game.h"

struct Settings
{
    bool useColors = true;
    bool showTrail = true;
};

// Data that lives for the whole session (between games).
struct AppContext
{
    Settings settings;
    Leaderboard leaderboard;
};

// Shows the main menu and runs games until the player chooses Exit.
void runApplication();

#endif // UI_H
