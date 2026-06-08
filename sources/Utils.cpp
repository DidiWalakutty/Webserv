#include "Utils.hpp"
#include "Server.hpp"

#include <algorithm>
#include <cctype>
#include <cerrno>
#include <cstring>
#include <fcntl.h>
#include <iostream>
#include <sstream>
#include <sys/epoll.h>
#include <unistd.h>

namespace Utils
{
	bool strContains(const std::string& text, const char* needle)
	{
		return text.find(needle) != std::string::npos;
	}

	ssize_t parseContentLength(const std::string& raw, size_t headersEnd)
	{
		std::string headerBlock = raw.substr(0, headersEnd);
		std::transform(headerBlock.begin(), headerBlock.end(), headerBlock.begin(), ::tolower);

		size_t pos = headerBlock.find("content-length:");
		if (pos == std::string::npos)
			return -1;

		pos += 15;
		while (pos < raw.size() && (raw[pos] == ' ' || raw[pos] == '\t'))
			pos++;

		size_t end = pos;
		while (end < raw.size() && std::isdigit(static_cast<unsigned char>(raw[end])))
			end++;

		if (end == pos)
			return -2;

		try
		{
			return std::stoll(raw.substr(pos, end - pos));
		}
		catch (...)
		{
			return -2;
		}
	}

	int createPipe(int fds[2])
	{
		if (pipe(fds) == -1)
			return (std::cerr << "pipe(): " << strerror(errno) << std::endl, 1);
		return 0;
	}

	pid_t forkCGI()
	{
		pid_t pid = fork();
		if (pid < 0)
			std::cerr << "fork(): " << strerror(errno) << std::endl;
		return pid;
	}

	int redirectPipe(int oldfd, int newfd)
	{
		if (dup2(oldfd, newfd) == -1)
			return (std::cerr << "dup2(): " << strerror(errno) << std::endl, 1);
		return 0;
	}

	int closeFd(int& fd)
	{
		if (fd == -1)
			return 0;
		if (close(fd) == -1)
			return (std::cerr << "close(): " << strerror(errno) << std::endl, 1);
		fd = -1;
		return 0;
	}

	std::vector<char*> str2Ptr(std::vector<std::string>& str)
	{
		std::vector<char*> ptr;
		for (size_t i = 0; i < str.size(); i++)
			ptr.push_back(const_cast<char*>(str[i].c_str()));
		ptr.push_back(NULL);
		return ptr;
	}

	int executeCGI(std::vector<std::string>& argV_str, std::vector<std::string>& envP_str)
	{
		std::vector<char*> argV = str2Ptr(argV_str);
		std::vector<char*> envP = str2Ptr(envP_str);
		if (execve(argV[0], argV.data(), envP.data()) == -1)
			return (std::cerr << "execve(): " << strerror(errno) << std::endl, 1);
		return 0;
	}

	int nonblockFd(int fd)
	{
		int flags = fcntl(fd, F_GETFL, 0);
		if (flags == -1)
			return (std::cerr << "fcntl(F_GETFL): " << strerror(errno) << std::endl, 1);
		if (fcntl(fd, F_SETFL, flags | O_NONBLOCK) == -1)
			return (std::cerr << "fcntl(F_SETFL): " << strerror(errno) << std::endl, 1);
		flags = fcntl(fd, F_GETFD, 0);
		if (flags == -1)
			return (std::cerr << "fcntl(F_GETFD): " << strerror(errno) << std::endl, 1);
		if (fcntl(fd, F_SETFD, flags | FD_CLOEXEC) == -1)
			return (std::cerr << "fcntl(F_SETFD): " << strerror(errno) << std::endl, 1);
		return 0;
	}

	std::string parseCGIHeaders(const std::string& rawHeaders, std::string& statusLine)
	{
		std::istringstream stream(rawHeaders);
		std::string line;
		std::string headers;
		bool hasContentType = false;
		while (std::getline(stream, line))
		{
			if (!line.empty() && line.back() == '\r')
				line.pop_back();
			if (line.empty())
				continue;
			std::string lower(line);
			std::transform(lower.begin(), lower.end(), lower.begin(), [](unsigned char c){ return std::tolower(c); });
			if (lower.compare(0, 7, "status:") == 0)
			{
				statusLine = line.substr(7);
				size_t start = statusLine.find_first_not_of(' ');
				if (start == std::string::npos)
					statusLine = "200 OK";
				else
					statusLine.erase(0, start);
				if (statusLine.size() < 3 || !std::isdigit(static_cast<unsigned char>(statusLine[0])) || !std::isdigit(static_cast<unsigned char>(statusLine[1])) || !std::isdigit(static_cast<unsigned char>(statusLine[2])) || (statusLine.size() > 3 && statusLine[3] != ' '))
					statusLine = "502 Bad Gateway";
			}
			else if (lower.compare(0, 13, "content-type:") == 0)
			{
				hasContentType = true;
				headers += line + "\r\n";
			}
			else if (lower.compare(0, 15, "content-length:") != 0)
			{
				headers += line + "\r\n";
			}
		}
		if (!hasContentType)
			headers += "Content-Type: text/html\r\n";
		return headers;
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
