// main.cpp
// Labyrinth - a console maze game in C++.
// Entry point: prepares the console and hands control to the main menu.

#include "input.h"
#include "ui.h"

int main()
{
    initializeConsole();
    runApplication();
    return 0;
}
