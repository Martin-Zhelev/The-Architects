// savegame.h
// Saving a running labyrinth to text and loading it back ("Save & Quit" /
// "Continue"). Pure logic: no console input/output or drawing.

#ifndef SAVEGAME_H
#define SAVEGAME_H

#include "game.h"

#include <string>

// Extra information the app needs to resume a run.
struct SaveMeta
{
    int mode = 0;  // RunMode
    int index = 0; // level or challenge number
    int theme = 0; // ThemeId
};

std::string serializeRun(const GameState& state, const SaveMeta& meta);
// Rebuilds the whole game from text. On failure the state is left empty.
bool deserializeRun(const std::string& text, GameState& state, SaveMeta& meta);

#endif // SAVEGAME_H
