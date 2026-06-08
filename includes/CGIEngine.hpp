#pragma once

#include <signal.h>
#include <string.h>
#include <sys/types.h>
#include <sys/wait.h>

#include <functional>
#include <map>
#include <memory>
#include <string>
#include <vector>

#include "HTTPCommon.hpp"

class HTTPRequest;
struct ServerParse;
struct LocationParse;

#define TIMEOUT 5

struct CGI
{
	int fd_stdin = -1;
	int fd_stdout = -1;
	int fd_client = -1;
	pid_t pid = -1;
	bool write_finished = false;
	std::string body = "";
	ssize_t body_size = 0;
	ssize_t body_written = 0;
	bool read_finished = false;
	bool cgi_finished = false;
	time_t start_time = 0;
	bool time_out = false;
	bool error = false;
	std::string output = "";
};

class CGIEngine
{
private:
	int& _epollFD;
	std::map<int, std::string>& _pendingWrites;
	std::map<int, size_t>& _writeOffsets;
	std::map<int, bool>& _closeAfterWrite;
	std::function<void(int)> _removeClient;
	std::map<int, std::shared_ptr<CGI> > _cgiProcesses;

	int childCGI(const HTTPRequest& request, const ServerParse& server,
		const std::string& filePath, const LocationParse& location,
		int pipe_p2c[2], int pipe_c2p[2]);
	void queueCGIResponse(int clientFD, const std::string& response);
	void handleCGITimeOut(std::shared_ptr<CGI> cgi);
	void handleCGIError(std::shared_ptr<CGI> cgi);
	void handleCGIWrite(std::shared_ptr<CGI> cgi, int fd, uint32_t events);
	void handleCGIRead(std::shared_ptr<CGI> cgi, int fd, uint32_t events);
	void handleCGIWait(std::shared_ptr<CGI> cgi);
	void handleCGIResponse(std::shared_ptr<CGI> cgi);
	void handleCGIErrorResponse(std::shared_ptr<CGI> cgi);
	std::vector<std::string> buildArgV(const std::string& filePath, const LocationParse& location) const;
	std::string method2Str(HTTPMethod method) const;
	std::string protocol2Str(HTTPProtocolVersion version) const;
	std::vector<std::string> buildEnvP(const HTTPRequest& request, const ServerParse& server, const std::string& filePath) const;
	int updateStruct(std::shared_ptr<CGI> cgi, int fd_stdin, int fd_stdout, int clientFD, const HTTPRequest& request);
	int addProcess(std::shared_ptr<CGI> cgi);
	int addEpoll(std::shared_ptr<CGI> cgi);
	void removeCGIParent(std::shared_ptr<CGI> cgi, int pipe_p2c[2], int pipe_c2p[2]);
	void removeCGIChild(int pipe_p2c[2], int pipe_c2p[2]);

public:
	CGIEngine(int& epollFD,
		std::map<int, std::string>& pendingWrites,
		std::map<int, size_t>& writeOffsets,
		std::map<int, bool>& closeAfterWrite,
		std::function<void(int)> removeClient);

	bool hasProcessFd(int fd) const;
	bool hasActiveProcesses() const;
	void handleTimeouts();
	void startCGI(int clientFD, const HTTPRequest& request, const ServerParse& server, const std::string& filePath, const LocationParse& location);
	void handleCGIEvent(int fd, uint32_t events);

	bool isCGIRequest(const HTTPRequest& request, const ServerParse& server, std::string& filePath, const LocationParse*& location) const;
	HTTPState checkCGIAccess(const std::string& filePath) const;
};
