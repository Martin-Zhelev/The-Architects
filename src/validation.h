// validation.h
// Checking and converting what the user types. Pure logic: these functions
// only look at strings, they never read or print anything themselves.

#ifndef VALIDATION_H
#define VALIDATION_H

#include <cstddef>
#include <string>

const std::size_t MAX_PLAYER_NAME_LENGTH = 12;

std::string trim(const std::string& text);
bool parseInt(const std::string& text, int& value);
bool parseUnsigned(const std::string& text, unsigned int& value);
bool isInRange(int value, int minValue, int maxValue);
bool isValidPlayerName(const std::string& name);

#endif // VALIDATION_H
