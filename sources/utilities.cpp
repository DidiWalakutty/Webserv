#include "utilities.hpp"

std::string cleanWhiteSpace(std::string str)
{
	str.erase(0, str.find_first_not_of(" \t\r\n"));
	str.erase(str.find_last_not_of(" \t\r\n") + 1);
	return str;
}

// Checks if the character is a valid hexadecimal for the percent-encoded sequence of an URL. 
bool isHex(char c)
{
	return (std::isdigit(c) || (std::tolower(c) >= 'a' && std::tolower(c) <= 'f'));
}

bool startsWith(const std::string longStr, const std::string beginningStr)
{
	return longStr.size() >= beginningStr.size() &&
		   longStr.compare(0, beginningStr.size(), beginningStr) == 0;
}