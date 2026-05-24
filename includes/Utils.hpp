#pragma once

#include <ostream>
#include <string>

namespace Utils
{
	bool strContains(const std::string& text, const char* needle);
	void logColored(std::ostream& out, const std::string& msg, const char* color = NULL);
	void logColored(const std::string& msg, const char* color = NULL);
	void interruptHandler(int sig);
}