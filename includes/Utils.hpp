#pragma once

#include <sys/types.h>

#include <ostream>
#include <string>
#include <vector>

namespace Utils
{
	bool strContains(const std::string& text, const char* needle);
	ssize_t parseContentLength(const std::string& raw, size_t headersEnd);

	int createPipe(int fds[2]);
	pid_t forkCGI();
	int redirectPipe(int oldfd, int newfd);
	int closeFd(int& fd);
	std::vector<char*> str2Ptr(std::vector<std::string>& str);
	int executeCGI(std::vector<std::string>& argV_str, std::vector<std::string>& envP_str);
	int nonblockFd(int fd);
	std::string parseCGIHeaders(const std::string& rawHeaders, std::string& statusLine);

	void logColored(std::ostream& out, const std::string& msg, const char* color = NULL);
	void logColored(const std::string& msg, const char* color = NULL);
	void interruptHandler(int sig);
}