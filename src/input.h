// input.h
// Keyboard and console handling for Windows and Linux/macOS.
// Reads single key presses (including the arrow keys) without Enter.

#ifndef INPUT_H
#define INPUT_H

enum class Key
{
    Up,
    Down,
    Left,
    Right,
    Diagonal,
    Hint,
    Map,
    Inspect,
    Help,
    Quit,
    Yes,
    Enter,
    Other
};

void initializeConsole();
bool isAnsiSupported();
void clearConsole();
void enableRawInput();
void disableRawInput();
Key readKey();

#endif // INPUT_H
