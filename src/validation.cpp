// validation.cpp
// Checking and converting user input. Pure logic: no console input or output.

#include "validation.h"

#include <cctype>
#include <climits>

std::string trim(const std::string& text)
{
    const std::string whitespace = " \t\r\n";
    const std::size_t first = text.find_first_not_of(whitespace);
    if (first == std::string::npos)
    {
        return "";
    }
    const std::size_t last = text.find_last_not_of(whitespace);
    return text.substr(first, last - first + 1);
}

bool parseInt(const std::string& text, int& value)
{
    const std::string cleaned = trim(text);
    if (cleaned.empty())
    {
        return false;
    }

    std::size_t index = 0;
    bool negative = false;
    if (cleaned[0] == '+' || cleaned[0] == '-')
    {
        negative = (cleaned[0] == '-');
        index = 1;
    }
    if (index == cleaned.size())
    {
        return false; // a sign with no digits
    }

    long long result = 0;
    for (; index < cleaned.size(); ++index)
    {
        const unsigned char character = static_cast<unsigned char>(cleaned[index]);
        if (!std::isdigit(character))
        {
            return false;
        }
        result = result * 10 + (cleaned[index] - '0');
        if (result > static_cast<long long>(INT_MAX) + 1)
        {
            return false; // too big for an int - stop before overflowing
        }
    }

    if (negative)
    {
        result = -result;
    }
    if (result < INT_MIN || result > INT_MAX)
    {
        return false;
    }
    value = static_cast<int>(result);
    return true;
}

bool parseUnsigned(const std::string& text, unsigned int& value)
{
    const std::string cleaned = trim(text);
    if (cleaned.empty())
    {
        return false;
    }

    unsigned long long result = 0;
    for (const char symbol : cleaned)
    {
        if (!std::isdigit(static_cast<unsigned char>(symbol)))
        {
            return false;
        }
        result = result * 10 + static_cast<unsigned long long>(symbol - '0');
        if (result > UINT_MAX)
        {
            return false;
        }
    }
    value = static_cast<unsigned int>(result);
    return true;
}

bool isInRange(int value, int minValue, int maxValue)
{
    return value >= minValue && value <= maxValue;
}

// Names are 1-12 characters long: letters, digits, spaces, '-' and '_',
// and must contain at least one letter or digit.
bool isValidPlayerName(const std::string& name)
{
    if (name.empty() || name.size() > MAX_PLAYER_NAME_LENGTH)
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
