#include "Utils.hpp"
#include "Server.hpp"

#include <iostream>

namespace Utils
{
	bool strContains(const std::string& text, const char* needle)
	{
		return text.find(needle) != std::string::npos;
	}

	void logColored(std::ostream& out, const std::string& msg, const char* color)
	{
		if (color && color[0] != '\0')
			out << color;
		out << msg;
		if (color && color[0] != '\0')
			out << RESET;
		out << std::endl;
	}

	void logColored(const std::string& msg, const char* color)
	{
		logColored(std::cout, msg, color);
	}

	void interruptHandler(int sig)
	{
		if (sig == SIGINT)
			Server::running = 0;
	}
}
