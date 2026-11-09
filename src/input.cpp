// input.cpp
// Keyboard and console handling.
//
// Windows: _getch() from <conio.h> returns arrow keys as two codes (224, then 72/80/75/77).
// Linux/macOS: the terminal is switched to "raw" mode, and arrow keys arrive
// as escape sequences such as ESC [ A.

#include "input.h"

#include <cctype>
#include <cstdio>

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <conio.h>
#include <windows.h>
#ifndef ENABLE_VIRTUAL_TERMINAL_PROCESSING
#define ENABLE_VIRTUAL_TERMINAL_PROCESSING 0x0004
#endif
#else
#include <csignal>
#include <poll.h>
#include <termios.h>
#include <unistd.h>
#endif

namespace
{

bool ansiSupported = true;

// Translates an ordinary character (letters, digits, Enter, Esc) into a Key.
Key mapCharacter(int character)
{
    switch (std::tolower(character))
    {
    case 'w':
        return Key::Up;
    case 's':
        return Key::Down;
    case 'a':
        return Key::Left;
    case 'd':
        return Key::Right;
    case 'e':
    case '3':
        return Key::Diagonal;
    case 'h':
        return Key::Hint;
    case 'm':
        return Key::Map;
    case 'i':
        return Key::Inspect;
    case '?':
    case '/':
        return Key::Help;
    case 'q':
    case 27: // Esc
        return Key::Quit;
    case 'y':
        return Key::Yes;
    case '\r':
    case '\n':
        return Key::Enter;
    default:
        return Key::Other;
    }
}

#ifndef _WIN32
termios originalTerminal;
bool rawModeActive = false;

void restoreTerminal()
{
    if (rawModeActive)
    {
        tcsetattr(STDIN_FILENO, TCSANOW, &originalTerminal);
        rawModeActive = false;
    }
}

// Ctrl+C during the game: give the terminal back in a usable state.
void handleInterrupt(int)
{
    restoreTerminal();
    const char reset[] = "\033[0m\033[?25h\n";
    const ssize_t written = write(STDOUT_FILENO, reset, sizeof(reset) - 1);
    (void)written;
    _exit(1);
}

int readByte()
{
    if (rawModeActive)
    {
        unsigned char byte = 0;
        return (read(STDIN_FILENO, &byte, 1) == 1) ? byte : -1;
    }
    return std::getchar(); // input is redirected from a file or a pipe
}

// After an Esc byte, checks whether more bytes follow (an arrow key)
// or whether the Esc key was pressed on its own.
bool isByteWaiting()
{
    if (!rawModeActive)
    {
        return true;
    }
    pollfd request{};
    request.fd = STDIN_FILENO;
    request.events = POLLIN;
    return poll(&request, 1, 50) > 0;
}
#endif

} // namespace

bool isAnsiSupported()
{
    return ansiSupported;
}

// Wipes the whole console - the visible window AND the scroll-back - and puts
// the cursor in the top-left corner, so every new page starts on a blank screen.
void clearConsole()
{
    std::fflush(stdout);
    if (ansiSupported)
    {
        // 2J = clear window, 3J = clear scroll-back, H = cursor home
        std::fputs("\033[2J\033[3J\033[H", stdout);
        std::fflush(stdout);
        return;
    }

#ifdef _WIN32
    // Older Windows consoles without ANSI support: clear through the Win32 API.
    const HANDLE output = GetStdHandle(STD_OUTPUT_HANDLE);
    CONSOLE_SCREEN_BUFFER_INFO info;
    if (output != INVALID_HANDLE_VALUE && GetConsoleScreenBufferInfo(output, &info) != 0)
    {
        const DWORD cellCount = static_cast<DWORD>(info.dwSize.X) * static_cast<DWORD>(info.dwSize.Y);
        const COORD home = {0, 0};
        DWORD written = 0;
        FillConsoleOutputCharacterA(output, ' ', cellCount, home, &written);
        FillConsoleOutputAttribute(output, info.wAttributes, cellCount, home, &written);
        SetConsoleCursorPosition(output, home);
        return;
    }
#endif

    for (int line = 0; line < 60; ++line) // last resort: push old text out of view
    {
        std::fputs("\n", stdout);
    }
    std::fflush(stdout);
}

#ifdef _WIN32

void initializeConsole()
{
    // Windows 10+ understands ANSI colour codes once this flag is switched on.
    const HANDLE output = GetStdHandle(STD_OUTPUT_HANDLE);
    DWORD mode = 0;
    ansiSupported = output != INVALID_HANDLE_VALUE && GetConsoleMode(output, &mode) != 0 &&
                    SetConsoleMode(output, mode | ENABLE_VIRTUAL_TERMINAL_PROCESSING) != 0;
}

void enableRawInput()
{
    // _getch() already reads single keys without echo.
}

void disableRawInput()
{
}

Key readKey()
{
    const int first = _getch();
    if (first == 0 || first == 224)
    {
        switch (_getch())
        {
        case 72:
            return Key::Up;
        case 80:
            return Key::Down;
        case 75:
            return Key::Left;
        case 77:
            return Key::Right;
        case 81: // Page Down
            return Key::Diagonal;
        default:
            return Key::Other;
        }
    }
    return mapCharacter(first);
}

#else

void initializeConsole()
{
    std::signal(SIGINT, handleInterrupt);
    std::signal(SIGTERM, handleInterrupt);
}

void enableRawInput()
{
    if (rawModeActive || !isatty(STDIN_FILENO))
    {
        return;
    }
    tcgetattr(STDIN_FILENO, &originalTerminal);
    termios raw = originalTerminal;
    raw.c_lflag &= ~static_cast<tcflag_t>(ICANON | ECHO); // no line buffering, no echo
    raw.c_cc[VMIN] = 1;
    raw.c_cc[VTIME] = 0;
    tcsetattr(STDIN_FILENO, TCSANOW, &raw);
    rawModeActive = true;
}

void disableRawInput()
{
    restoreTerminal();
}

Key readKey()
{
    const int first = readByte();
    if (first < 0)
    {
        return Key::Quit; // end of input
    }
    if (first != 27)
    {
        return mapCharacter(first);
    }
    if (!isByteWaiting())
    {
        return Key::Quit; // Esc on its own
    }

    const int second = readByte();
    if (second != '[' && second != 'O')
    {
        return Key::Quit;
    }
    switch (readByte())
    {
    case 'A':
        return Key::Up;
    case 'B':
        return Key::Down;
    case 'C':
        return Key::Right;
    case 'D':
        return Key::Left;
    case '6': // Page Down = ESC [ 6 ~
        readByte();
        return Key::Diagonal;
    default:
        return Key::Other;
    }
}

#endif
