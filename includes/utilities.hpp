#pragma once

#include <string>
#include <cctype>
#include <algorithm>
#include <sys/stat.h>
#include <unistd.h>

std::string cleanWhiteSpace(std::string str);
bool isHex(char c);
bool startsWith(const std::string longStr, const std::string beginningStr);

bool pathExists(const std::string& path);
bool isReadable(const std::string& path);
bool isWritable(const std::string& path);