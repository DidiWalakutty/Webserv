#pragma once

#include <string>
#include <cctype>
#include <algorithm>

std::string cleanWhiteSpace(std::string str);
bool isHex(char c);

/**
 * @brief Checks if a string starts with a given substring.
 *
 * @param longStr The string to check.
 * @param beginningStr The substring to look for at the start of longStr.
 * @return true if longStr starts with beginningStr, false otherwise.
 */
bool startsWith(const std::string longStr, const std::string beginningStr);