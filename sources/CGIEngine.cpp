#include "CGIEngine.hpp"

#include <cstring>
#include <fstream>
#include <iostream>
#include <algorithm>
#include <sstream>
#include <sys/epoll.h>
#include <unistd.h>

#include "ServerParse.hpp"
#include "HTTPRequest.hpp"
#include "Utils.hpp"

namespace
{
	const size_t CGI_READ_BUFFER_SIZE = 65536;
}

CGIEngine::CGIEngine(int& epollFD,
	std::map<int, std::string>& pendingWrites,
	std::map<int, size_t>& writeOffsets,
	std::map<int, bool>& closeAfterWrite,
	std::function<void(int)> removeClient)
	: _epollFD(epollFD),
	  _pendingWrites(pendingWrites),
	  _writeOffsets(writeOffsets),
	  _closeAfterWrite(closeAfterWrite),
	  _removeClient(removeClient)
{
}

bool CGIEngine::hasProcessFd(int fd) const
{
	return _cgiProcesses.find(fd) != _cgiProcesses.end();
}

bool CGIEngine::hasActiveProcesses() const
{
	return !_cgiProcesses.empty();
}

void CGIEngine::handleTimeouts()
{
	std::vector<std::shared_ptr<CGI> > snapshot;
	snapshot.reserve(_cgiProcesses.size());
	for (std::map<int, std::shared_ptr<CGI> >::iterator it = _cgiProcesses.begin(); it != _cgiProcesses.end(); ++it)
	{
		if (it->second)
			snapshot.push_back(it->second);
	}
	for (size_t i = 0; i < snapshot.size(); ++i)
	{
		std::shared_ptr<CGI> cgi = snapshot[i];
		if (!cgi)
			continue;
		if (cgi->cgi_finished)
			continue;
		time_t now = time(NULL);
		if (now == -1)
			continue;
		if (now - cgi->start_time > TIMEOUT)
			handleCGITimeOut(cgi);
	}
}

void CGIEngine::startCGI(int clientFD, const HTTPRequest& request, const ServerParse& server, const std::string& filePath, const LocationParse& location)
{
	int pipe_p2c[2] = {-1, -1};
	int pipe_c2p[2] = {-1, -1};

	if (Utils::createPipe(pipe_p2c) || Utils::createPipe(pipe_c2p))
		return;
	std::shared_ptr<CGI> cgi = std::make_shared<CGI>();
	cgi->pid = Utils::forkCGI();
	if (cgi->pid < 0)
		return removeCGIParent(cgi, pipe_p2c, pipe_c2p);
	if (cgi->pid == 0)
		return (void)childCGI(request, server, filePath, location, pipe_p2c, pipe_c2p);
	Utils::closeFd(pipe_p2c[0]);
	Utils::closeFd(pipe_c2p[1]);
	if (Utils::nonblockFd(pipe_p2c[1]) || Utils::nonblockFd(pipe_c2p[0]))
		return removeCGIParent(cgi, pipe_p2c, pipe_c2p);
	if (updateStruct(cgi, pipe_p2c[1], pipe_c2p[0], clientFD, request) ||
		addProcess(cgi) ||
		addEpoll(cgi))
		return removeCGIParent(cgi, pipe_p2c, pipe_c2p);
}

void CGIEngine::handleCGIEvent(int fd, uint32_t events)
{
	std::map<int, std::shared_ptr<CGI> >::iterator it = _cgiProcesses.find(fd);
	if (it == _cgiProcesses.end() || !it->second)
		return;
	std::shared_ptr<CGI> cgi = it->second;
	if (cgi->cgi_finished)
		return;
	if (events & (EPOLLERR | EPOLLHUP))
		return (void)handleCGIError(cgi);
	if (fd == cgi->fd_stdin)
		handleCGIWrite(cgi, fd, events);
	else if (fd == cgi->fd_stdout)
		handleCGIRead(cgi, fd, events);
	if (cgi->cgi_finished == false)
		handleCGIWait(cgi);
}

void CGIEngine::handleCGITimeOut(std::shared_ptr<CGI> cgi)
{
	cgi->time_out = true;
	cgi->cgi_finished = true;
	handleCGIErrorResponse(cgi);
}

void CGIEngine::handleCGIError(std::shared_ptr<CGI> cgi)
{
	cgi->error = true;
	cgi->cgi_finished = true;
	handleCGIErrorResponse(cgi);
}

void CGIEngine::handleCGIWrite(std::shared_ptr<CGI> cgi, int fd, uint32_t events)
{
	if (!(events & EPOLLOUT))
		return;
	ssize_t n = write(fd, cgi->body.data() + cgi->body_written, cgi->body_size - cgi->body_written);
	if (n > 0)
		cgi->body_written += n;
	if (n < 0 && errno != EAGAIN && errno != EWOULDBLOCK)
		return (void)handleCGIError(cgi);
	if (cgi->body_written == cgi->body_size)
	{
		cgi->write_finished = true;
		if (epoll_ctl(_epollFD, EPOLL_CTL_DEL, cgi->fd_stdin, NULL) == -1)
			std::cerr << "epoll_ctl(DEL): " << strerror(errno) << std::endl;
		_cgiProcesses.erase(cgi->fd_stdin);
		Utils::closeFd(cgi->fd_stdin);
	}
}

void CGIEngine::handleCGIRead(std::shared_ptr<CGI> cgi, int fd, uint32_t events)
{
	if (!(events & EPOLLIN))
		return;
	char buffer[CGI_READ_BUFFER_SIZE];
	ssize_t n = read(fd, buffer, CGI_READ_BUFFER_SIZE);
	if (n > 0)
		cgi->output.append(buffer, n);
	if (n == 0)
	{
		cgi->read_finished = true;
		if (epoll_ctl(_epollFD, EPOLL_CTL_DEL, cgi->fd_stdout, NULL) == -1)
			std::cerr << "epoll_ctl(DEL): " << strerror(errno) << std::endl;
		_cgiProcesses.erase(cgi->fd_stdout);
		Utils::closeFd(cgi->fd_stdout);
	}
	if (n < 0 && errno != EAGAIN && errno != EWOULDBLOCK)
		return (void)handleCGIError(cgi);
}

void CGIEngine::handleCGIWait(std::shared_ptr<CGI> cgi)
{
	if (cgi->read_finished == false)
		return;
	int status = 0;
	pid_t result = waitpid(cgi->pid, &status, WNOHANG);
	if (result == 0)
		return;
	if (result == -1)
	{
		std::cerr << "waitpid(): " << strerror(errno) << std::endl;
		cgi->error = true;
		cgi->cgi_finished = true;
		return handleCGIErrorResponse(cgi);
	}
	cgi->cgi_finished = true;
	if (WIFEXITED(status) && WEXITSTATUS(status) == 0)
		return handleCGIResponse(cgi);
	cgi->error = true;
	handleCGIErrorResponse(cgi);
}

std::string CGIEngine::parseCGIHeaders(const std::string& rawHeaders, std::string& statusLine) const
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

void CGIEngine::handleCGIResponse(std::shared_ptr<CGI> cgi)
{
	std::string response;
	std::string body;
	std::string statusLine = "200 OK";
	std::string headers;
	size_t headerEnd = cgi->output.find("\r\n\r\n");
	if (headerEnd != std::string::npos)
	{
		headers = parseCGIHeaders(cgi->output.substr(0, headerEnd), statusLine);
		body = cgi->output.substr(headerEnd + 4);
	}
	else
	{
		headers = "Content-Type: text/html\r\n";
		body = cgi->output;
	}
	response = "HTTP/1.1 " + statusLine + "\r\n";
	response += headers;
	response += "Content-Length: " + std::to_string(body.size()) + "\r\n";
	response += "Connection: close\r\n\r\n";
	response += body;
	queueCGIResponse(cgi->fd_client, response);
	if (cgi->fd_stdin != -1)
	{
		if (epoll_ctl(_epollFD, EPOLL_CTL_DEL, cgi->fd_stdin, NULL) == -1)
			std::cerr << "epoll_ctl(DEL): " << strerror(errno) << std::endl;
		_cgiProcesses.erase(cgi->fd_stdin);
		Utils::closeFd(cgi->fd_stdin);
	}
	if (cgi->fd_stdout != -1)
	{
		if (epoll_ctl(_epollFD, EPOLL_CTL_DEL, cgi->fd_stdout, NULL) == -1)
			std::cerr << "epoll_ctl(DEL): " << strerror(errno) << std::endl;
		_cgiProcesses.erase(cgi->fd_stdout);
		Utils::closeFd(cgi->fd_stdout);
	}
}

void CGIEngine::handleCGIErrorResponse(std::shared_ptr<CGI> cgi)
{
	std::string response;
	if (cgi->time_out)
		response = "HTTP/1.1 504 Gateway Timeout\r\nContent-Length: 0\r\nConnection: close\r\n\r\n";
	else
		response = "HTTP/1.1 502 Bad Gateway\r\nContent-Length: 0\r\nConnection: close\r\n\r\n";
	queueCGIResponse(cgi->fd_client, response);
	if (cgi->fd_stdin != -1)
	{
		if (epoll_ctl(_epollFD, EPOLL_CTL_DEL, cgi->fd_stdin, NULL) == -1)
			std::cerr << "epoll_ctl(DEL): " << strerror(errno) << std::endl;
		_cgiProcesses.erase(cgi->fd_stdin);
		Utils::closeFd(cgi->fd_stdin);
	}
	if (cgi->fd_stdout != -1)
	{
		if (epoll_ctl(_epollFD, EPOLL_CTL_DEL, cgi->fd_stdout, NULL) == -1)
			std::cerr << "epoll_ctl(DEL): " << strerror(errno) << std::endl;
		_cgiProcesses.erase(cgi->fd_stdout);
		Utils::closeFd(cgi->fd_stdout);
	}
	if (cgi->pid > 0)
	{
		if (waitpid(cgi->pid, NULL, WNOHANG) == 0)
		{
			kill(cgi->pid, SIGKILL);
			waitpid(cgi->pid, NULL, 0);
		}
		cgi->pid = -1;
	}
}

void CGIEngine::queueCGIResponse(int clientFD, const std::string& response)
{
	if (clientFD < 0)
		return;
	_pendingWrites[clientFD] = response;
	_writeOffsets[clientFD] = 0;
	_closeAfterWrite[clientFD] = true;

	epoll_event ev{};
	ev.events = EPOLLOUT;
	ev.data.fd = clientFD;
	if (epoll_ctl(_epollFD, EPOLL_CTL_MOD, clientFD, &ev) == -1)
	{
		std::cerr << "epoll_ctl(MOD): " << strerror(errno) << std::endl;
		_pendingWrites.erase(clientFD);
		_writeOffsets.erase(clientFD);
		_closeAfterWrite.erase(clientFD);
		_removeClient(clientFD);
	}
}

bool CGIEngine::isCGIRequest(const HTTPRequest& request, const ServerParse& server, std::string& filePath, const LocationParse*& location) const
{
	filePath = "";
	location = NULL;
	if (request.resourcePath.find("..") != std::string::npos)
		return false;

	for (size_t i = 0; i < server.locations.size(); i++)
	{
		if (request.resourcePath.rfind(server.locations[i].path, 0) != 0)
			continue;
		std::string relPath = request.resourcePath.substr(server.locations[i].path.size());
		if (!relPath.empty() && relPath[0] == '/')
			relPath.erase(0, 1);
		if (relPath.empty())
			continue;
		std::string candidate;
		if (server.locations[i].path == "/")
			candidate = server.locations[i].root + request.resourcePath;
		else
			candidate = server.locations[i].root + "/" + relPath;
		std::string ext;
		size_t dot = candidate.find_last_of('.');
		if (dot != std::string::npos)
			ext = candidate.substr(dot);
		for (size_t j = 0; j < server.locations[i].cgi_extension.size(); j++)
		{
			if (ext == server.locations[i].cgi_extension[j])
			{
				filePath = candidate;
				location = &server.locations[i];
				return true;
			}
		}
	}
	return false;
}

HTTPState CGIEngine::checkCGIAccess(const std::string& filePath) const
{
	if (access(filePath.c_str(), F_OK) != 0)
		return HTTPState::NotFound;
	if (access(filePath.c_str(), R_OK) != 0)
		return HTTPState::Forbidden;
	if (access(filePath.c_str(), X_OK) != 0)
		return HTTPState::Forbidden;
	return HTTPState::Ok;
}

std::vector<std::string> CGIEngine::buildArgV(const std::string& filePath, const LocationParse& location) const
{
	std::vector<std::string> argV_str = {};
	size_t dot = filePath.find_last_of('.');
	std::string ext = filePath.substr(dot);

	for (size_t i = 0; i < location.cgi_extension.size(); i++)
		if (ext == location.cgi_extension[i])
			argV_str.push_back(location.cgi_executable[i]);
	argV_str.push_back(filePath);
	return argV_str;
}

std::vector<std::string> CGIEngine::buildEnvP(const HTTPRequest& request, const ServerParse& server, const std::string& filePath) const
{
	std::vector<std::string> envP_str = {};
	size_t dot = filePath.find_last_of('.');
	std::string ext = filePath.substr(dot);

	if (request.method == HTTPMethod::UNSUPPORTED)
		return (std::cerr << "Unsupported method" << std::endl, envP_str);
	envP_str.push_back("GATEWAY_INTERFACE=CGI/1.1");
	envP_str.push_back("REQUEST_METHOD=" + HTTPCommon::methodToString(request.method));
	envP_str.push_back("SCRIPT_NAME=" + request.resourcePath);
	envP_str.push_back("SERVER_NAME=" + server.serverName);
	envP_str.push_back("SERVER_PORT=" + std::to_string(server.port));
	envP_str.push_back("SERVER_PROTOCOL=" + HTTPCommon::protocolVersionToString(request.protocolVersion));
	envP_str.push_back("SERVER_SOFTWARE=webserv/1.0");
	envP_str.push_back("REMOTE_ADDR=127.0.0.1");
	envP_str.push_back("QUERY_STRING=" + request.queryStringCGI);
	envP_str.push_back("SCRIPT_FILENAME=" + filePath);
	if (request.method == HTTPMethod::POST ||
		request.method == HTTPMethod::PUT ||
		request.method == HTTPMethod::PATCH)
	{
		envP_str.push_back("CONTENT_LENGTH=" + (request.headers.count("CONTENT-LENGTH") ? request.headers.at("CONTENT-LENGTH") : "0"));
		envP_str.push_back("CONTENT_TYPE=" + (request.headers.count("CONTENT-TYPE") ? request.headers.at("CONTENT-TYPE") : ""));
	}
	if (ext == ".php")
		envP_str.push_back("REDIRECT_STATUS=200");
	return envP_str;
}

int CGIEngine::updateStruct(std::shared_ptr<CGI> cgi, int fd_stdin, int fd_stdout, int clientFD, const HTTPRequest& request)
{
	cgi->fd_stdin = fd_stdin;
	cgi->fd_stdout = fd_stdout;
	cgi->fd_client = clientFD;
	cgi->write_finished =
		(request.method == HTTPMethod::GET ||
		 request.method == HTTPMethod::HEAD ||
		 request.method == HTTPMethod::DELETE);
	if (cgi->write_finished == false)
	{
		cgi->body = request.body;
		cgi->body_size = cgi->body.size();
		if (cgi->body_size == 0)
			cgi->write_finished = true;
	}
	if (cgi->write_finished == true)
		Utils::closeFd(cgi->fd_stdin);
	cgi->start_time = time(NULL);
	if (cgi->start_time == -1)
		return (std::cerr << "time(NULL): " << strerror(errno) << std::endl, 1);
	return 0;
}

int CGIEngine::addProcess(std::shared_ptr<CGI> cgi)
{
	if (cgi->write_finished == false)
		if (_cgiProcesses.count(cgi->fd_stdin))
			return (std::cerr << "FD_STDIN already tracked: " << cgi->fd_stdin << std::endl, 1);
	if (_cgiProcesses.count(cgi->fd_stdout))
		return (std::cerr << "FD_STDOUT already tracked: " << cgi->fd_stdout << std::endl, 1);
	if (cgi->write_finished == false)
		_cgiProcesses[cgi->fd_stdin] = cgi;
	_cgiProcesses[cgi->fd_stdout] = cgi;
	return 0;
}

int CGIEngine::addEpoll(std::shared_ptr<CGI> cgi)
{
	epoll_event evIn{};
	epoll_event evOut{};
	if (cgi->write_finished == false)
	{
		evIn.events = EPOLLOUT | EPOLLERR | EPOLLHUP;
		evIn.data.fd = cgi->fd_stdin;
		if (epoll_ctl(_epollFD, EPOLL_CTL_ADD, cgi->fd_stdin, &evIn) == -1 && errno != EEXIST)
			return (std::cerr << "epoll_ctl(ADD): " << strerror(errno) << std::endl, 1);
	}
	evOut.events = EPOLLIN | EPOLLERR | EPOLLHUP;
	evOut.data.fd = cgi->fd_stdout;
	if (epoll_ctl(_epollFD, EPOLL_CTL_ADD, cgi->fd_stdout, &evOut) == -1 && errno != EEXIST)
		return (std::cerr << "epoll_ctl(ADD): " << strerror(errno) << std::endl, 1);
	return 0;
}

void CGIEngine::removeCGIParent(std::shared_ptr<CGI> cgi, int pipe_p2c[2], int pipe_c2p[2])
{
	Utils::closeFd(pipe_p2c[0]);
	if (pipe_p2c[1] != -1)
	{
		if (epoll_ctl(_epollFD, EPOLL_CTL_DEL, pipe_p2c[1], NULL) == -1 && errno != ENOENT && errno != EBADF)
			std::cerr << "epoll_ctl(DEL): " << strerror(errno) << std::endl;
		_cgiProcesses.erase(pipe_p2c[1]);
		Utils::closeFd(pipe_p2c[1]);
	}
	if (pipe_c2p[0] != -1)
	{
		if (epoll_ctl(_epollFD, EPOLL_CTL_DEL, pipe_c2p[0], NULL) == -1 && errno != ENOENT && errno != EBADF)
			std::cerr << "epoll_ctl(DEL): " << strerror(errno) << std::endl;
		_cgiProcesses.erase(pipe_c2p[0]);
		Utils::closeFd(pipe_c2p[0]);
	}
	Utils::closeFd(pipe_c2p[1]);
	if (cgi->pid > 0)
	{
		if (waitpid(cgi->pid, NULL, WNOHANG) == 0)
		{
			kill(cgi->pid, SIGKILL);
			waitpid(cgi->pid, NULL, 0);
		}
		cgi->pid = -1;
	}
}

void CGIEngine::removeCGIChild(int pipe_p2c[2], int pipe_c2p[2])
{
	Utils::closeFd(pipe_p2c[0]);
	Utils::closeFd(pipe_p2c[1]);
	Utils::closeFd(pipe_c2p[0]);
	Utils::closeFd(pipe_c2p[1]);
}

int CGIEngine::childCGI(const HTTPRequest& request, const ServerParse& server,
	const std::string& filePath, const LocationParse& location,
	int pipe_p2c[2], int pipe_c2p[2])
{
	if (Utils::redirectPipe(pipe_p2c[0], STDIN_FILENO) ||
		Utils::redirectPipe(pipe_c2p[1], STDOUT_FILENO) ||
		Utils::closeFd(pipe_p2c[0]) ||
		Utils::closeFd(pipe_p2c[1]) ||
		Utils::closeFd(pipe_c2p[0]) ||
		Utils::closeFd(pipe_c2p[1]))
		return (removeCGIChild(pipe_p2c, pipe_c2p), exit(1), 1);
	std::vector<std::string> argV_str = buildArgV(filePath, location);
	std::vector<std::string> envP_str = buildEnvP(request, server, filePath);
	if (argV_str.empty() || envP_str.empty())
		return (removeCGIChild(pipe_p2c, pipe_c2p), exit(1), 1);
	if (Utils::executeCGI(argV_str, envP_str))
		return (removeCGIChild(pipe_p2c, pipe_c2p), exit(1), 1);
	return (removeCGIChild(pipe_p2c, pipe_c2p), exit(0), 0);
}
