#include "HTTPResponse.hpp"
#include "server.hpp"
#include "CGI.hpp"



static int						childCGI(const HTTPRequest& request, const ServerParse& server, const std::string& filePath, const LocationParse& location, int pipe_p2c[2], int pipe_c2p[2]);
static int						createPipe(int pipe2open[2]);
static int						nonblockFd(int fd);
static pid_t					forkCGI();
static int						redirectPipe(int oldfd, int newfd);
static int						closeFd(int& fd);
static std::vector<std::string>	buildArgV(const std::string& filePath, const LocationParse& location);
static std::string				method2Str(HTTPMethod method);
static std::string				protocol2Str(HTTPProtocolVersion version);
static std::vector<std::string>	buildEnvP(const HTTPRequest& request, const ServerParse& server, const std::string& filePath, const LocationParse& location);
static std::vector<char*>		str2Ptr(std::vector<std::string>& str);
static int						executeCGI(std::vector<std::string>& argV_str, std::vector<std::string>& envP_str);
static int						updateStruct(std::shared_ptr<CGI> cgi, int fd_stdin, int fd_stdout, const HTTPRequest& request);
static int						addProcess(std::shared_ptr<CGI> cgi, std::map<int, CGIInfo>& cgiProcesses, int clientFD);
static int						addEpoll(std::shared_ptr<CGI> cgi, int epollFD);
static void						removeCGIParent(std::shared_ptr<CGI> cgi, int pipe_p2c[2], int pipe_c2p[2], std::map<int, CGIInfo>& cgiProcesses, int epollFD);
static void						removeCGIChild(int pipe_p2c[2], int pipe_c2p[2]);



void	Server::startCGI(int clientFD, const HTTPRequest& request, const ServerParse& server, const std::string& filePath, const LocationParse& location)
{
	std::shared_ptr<CGI>	cgi(new CGI);
	int						pipe_p2c[2] = {-1, -1};
	int						pipe_c2p[2] = {-1, -1};

	while (true)
	{
		/* PIPES */
		if (createPipe(pipe_p2c) ||
			createPipe(pipe_c2p))
			break;

		/* SET TO NONBLOCKING */
		if (nonblockFd(pipe_p2c[0]) ||
			nonblockFd(pipe_p2c[1]) ||
			nonblockFd(pipe_c2p[0]) ||
			nonblockFd(pipe_c2p[1]))
			break;

		/* FORK */
		cgi->pid = forkCGI();
		if (cgi->pid < 0)
			break;

		/* ***CHILD*** */
		else if (cgi->pid == 0)
			_exit(childCGI(request, server, filePath, location, pipe_p2c, pipe_c2p));

		/* ***PARENT*** */
		/* CLOSE PIPES */
		if (closeFd(pipe_p2c[0]) ||
			closeFd(pipe_c2p[1]))
			break;

		/* UPDATE STRUCT */
		if (updateStruct(cgi, pipe_p2c[1], pipe_c2p[0], request))
			break;

		/* ADD PROCESS */
		if (addProcess(cgi, cgiProcesses, clientFD))
			break;

		/* ADD EPOLL */
		if (addEpoll(cgi, epollFD))
			break;
		return;
	}
	removeCGIParent(cgi, pipe_p2c, pipe_c2p, cgiProcesses, epollFD);
}



static int	childCGI(const HTTPRequest& request, const ServerParse& server, const std::string& filePath, const LocationParse& location, int pipe_p2c[2], int pipe_c2p[2])
{
	std::vector<std::string>	argV_str;
	std::vector<std::string>	envP_str;

	/* REDIRECT */
	if (redirectPipe(pipe_p2c[0], STDIN_FILENO) ||
		redirectPipe(pipe_c2p[1], STDOUT_FILENO) ||
		redirectPipe(pipe_c2p[1], STDERR_FILENO))
		return (removeCGIChild(pipe_p2c, pipe_c2p), 1);

	/* CLOSE PIPES */
	if (closeFd(pipe_p2c[0]) ||
		closeFd(pipe_p2c[1]) ||
		closeFd(pipe_c2p[0]) ||
		closeFd(pipe_c2p[1]))
		return (removeCGIChild(pipe_p2c, pipe_c2p), 2);

	/* BUILD ARGV */
	argV_str = buildArgV(filePath, location);
	if (argV_str.empty())
		return (3);

	/* BUILD ENVP */
	envP_str = buildEnvP(request, server, filePath, location);
	if (envP_str.empty())
		return (4);

	/* EXECUTE */
	if (executeCGI(argV_str, envP_str))
		return (5);
	return (0);
}



static int	createPipe(int pipe2open[2])
{
	if (pipe(pipe2open) == -1)
	{
		std::cerr << "pipe(): " << strerror(errno) << std::endl;
		return (1);
	}
	return (0);
}



static int	nonblockFd(int fd)
{
	int flags;

	flags = fcntl(fd, F_GETFL, 0);
	if (flags == -1)
	{
		std::cerr << "fcntl(F_GETFL): " << strerror(errno) << std::endl;
		return (1);
	}
	if (fcntl(fd, F_SETFL, flags | O_NONBLOCK) == -1)
	{
		std::cerr << "fcntl(F_SETFL): " << strerror(errno) << std::endl;
		return (1);
	}
	flags = fcntl(fd, F_GETFD, 0);
	if (flags == -1)
	{
		std::cerr << "fcntl(F_GETFD): " << strerror(errno) << std::endl;
		return (1);
	}
	if (fcntl(fd, F_SETFD, flags | FD_CLOEXEC) == -1)
	{
		std::cerr << "fcntl(F_SETFD): " << strerror(errno) << std::endl;
		return (1);
	}
	return (0);
}



static pid_t	forkCGI()
{
	pid_t	pid = fork();

	if (pid < 0)
		std::cerr << "fork(): " << strerror(errno) << std::endl;
	return (pid);
}



static int	redirectPipe(int oldfd, int newfd)
{
	if (dup2(oldfd, newfd) == -1)
	{
		std::cerr << "dup2(): " << strerror(errno) << std::endl;
		return (1);
	}
	return (0);
}



static int	closeFd(int& fd)
{
	if (fd == -1)
		return (0);
	if (close(fd) == -1)
	{
		std::cerr << "close(): " << strerror(errno) << std::endl;
		return (1);
	}
	fd = -1;
	return (0);
}



static std::vector<std::string>	buildArgV(const std::string& filePath, const LocationParse& location)
{
	std::vector<std::string>	argV_str;

	if (location.cgi_extension != ".cgi")										/* WHICH TO USE? */
		argV_str.push_back(location.cgi_executable);
	argV_str.push_back(filePath);
	return (argV_str);
}



static std::string	method2Str(HTTPMethod method)
{
	switch (method)
	{
		case HTTPMethod::GET:
			return ("GET");
		case HTTPMethod::POST:
			return ("POST");
		case HTTPMethod::DELETE:
			return ("DELETE");
		case HTTPMethod::PUT:
			return ("PUT");
		case HTTPMethod::HEAD:
			return ("HEAD");
		case HTTPMethod::PATCH:
			return ("PATCH");
		default:
			return ("UNSUPPORTED");
	}
}



static std::string	protocol2Str(HTTPProtocolVersion version)
{
	switch (version)
	{
		case HTTPProtocolVersion::HTTP_0_9:
			return ("HTTP/0.9");
		case HTTPProtocolVersion::HTTP_1_0:
			return ("HTTP/1.0");
		case HTTPProtocolVersion::HTTP_1_1:
			return ("HTTP/1.1");
		case HTTPProtocolVersion::HTTP_2_0:
			return ("HTTP/2.0");
		case HTTPProtocolVersion::HTTP_3_0:
			return ("HTTP/3.0");
		default:
			return ("UNSUPPORTED");
	}
}

static std::vector<std::string>	buildEnvP(const HTTPRequest& request, const ServerParse& server, const std::string& filePath, const LocationParse& location)
{
	std::vector<std::string>	envP_str;

	if (request.method == HTTPMethod::UNSUPPORTED)
	{
		std::cerr << "Unsupported method" << std::endl;
		return {};
	}
	envP_str.push_back("GATEWAY_INTERFACE=CGI/1.1");
	envP_str.push_back("REQUEST_METHOD=" + method2Str(request.method));
	envP_str.push_back("SCRIPT_NAME=" + request.resourcePath);
	envP_str.push_back("SERVER_NAME=" + server.serverName);
	envP_str.push_back("SERVER_PORT=" + std::to_string(server.port));
	envP_str.push_back("SERVER_PROTOCOL=" + protocol2Str(request.protocolVersion));
	envP_str.push_back("SERVER_SOFTWARE=webserv/1.0");
	envP_str.push_back("REMOTE_ADDR=127.0.0.1");
	envP_str.push_back("QUERY_STRING=" + request.queryStringCGI);
	if (request.method == HTTPMethod::POST ||
		request.method == HTTPMethod::PUT ||
		request.method == HTTPMethod::PATCH)
	{
		envP_str.push_back("CONTENT_LENGTH=" + (request.headers.count("Content-Length") ? request.headers.at("Content-Length") : "0"));
		envP_str.push_back("CONTENT_TYPE=" + (request.headers.count("Content-Type") ? request.headers.at("Content-Type") : ""));
	}
	if (location.cgi_extension == ".php")
	{
		envP_str.push_back("REDIRECT_STATUS=200");
		envP_str.push_back("SCRIPT_FILENAME=" + filePath);
	}
	return (envP_str);
}



static std::vector<char*>	str2Ptr(std::vector<std::string>& str)
{
	std::vector<char*>	ptr;

	for (size_t	i = 0; i < str.size(); i++)
		ptr.push_back(const_cast<char*>(str[i].c_str()));
	ptr.push_back(NULL);
	return (ptr);
}



static int	executeCGI(std::vector<std::string>& argV_str, std::vector<std::string>& envP_str)
{
	std::vector<char*>	argV = str2Ptr(argV_str);
	std::vector<char*>	envP = str2Ptr(envP_str);

	if (execve(argV[0], argV.data(), envP.data()) == -1)
	{
		std::cerr << "execve(): " << strerror(errno) << std::endl;
		return (1);
	}
	return (0);
}



static int	updateStruct(std::shared_ptr<CGI> cgi, int fd_stdin, int fd_stdout, const HTTPRequest& request)
{
	cgi->fd_stdin = fd_stdin;
	cgi->fd_stdout = fd_stdout;
	cgi->write_finished =
		(request.method == HTTPMethod::GET ||
		request.method == HTTPMethod::HEAD ||
		request.method == HTTPMethod::DELETE);
	if (cgi->write_finished == false)
	{
		cgi->body = request.body;
		cgi->body_size = cgi->body.size();
	}
	cgi->start_time = time(NULL);
	if (cgi->start_time == -1)
	{
		std::cerr << "time(NULL): " << strerror(errno) << std::endl;
		return (1);
	}
	return (0);
}



static int	addProcess(std::shared_ptr<CGI> cgi, std::map<int, CGIInfo>& cgiProcesses, int clientFD)
{
	if (cgi->write_finished == false)
	{
		if (cgiProcesses.count(cgi->fd_stdin))
		{
			std::cerr << "FD_STDIN already tracked: " << cgi->fd_stdin << std::endl;
			return (1);
		}
		cgiProcesses[cgi->fd_stdin] = CGIInfo{cgi, true, clientFD};
	}
	if (cgiProcesses.count(cgi->fd_stdout))
	{
		std::cerr << "FD_STDOUT already tracked: " << cgi->fd_stdout << std::endl;
		return (1);
	}
	cgiProcesses[cgi->fd_stdout] = CGIInfo{cgi, false, clientFD};
	return (0);
}



static int	addEpoll(std::shared_ptr<CGI> cgi, int epollFD)
{
	epoll_event	evIn{};
	epoll_event	evOut{};

	if (cgi->write_finished == false)
	{
		evIn.events = EPOLLOUT | EPOLLERR | EPOLLHUP;
		evIn.data.fd = cgi->fd_stdin;
		if (epoll_ctl(epollFD, EPOLL_CTL_ADD, cgi->fd_stdin, &evIn) == -1 &&
			errno != EEXIST)
		{
			std::cerr << "epoll_ctl(ADD): " << strerror(errno) << std::endl;
			return (1);
		}
	}
	evOut.events = EPOLLIN | EPOLLERR | EPOLLHUP;
	evOut.data.fd = cgi->fd_stdout;
	if (epoll_ctl(epollFD, EPOLL_CTL_ADD, cgi->fd_stdout, &evOut) == -1 &&
		errno != EEXIST)
	{
		std::cerr << "epoll_ctl(ADD): " << strerror(errno) << std::endl;
		return (1);
	}
	return (0);
}



static void	removeCGIParent(std::shared_ptr<CGI> cgi, int pipe_p2c[2], int pipe_c2p[2], std::map<int, CGIInfo>& cgiProcesses, int epollFD)
{
	if (pipe_p2c[0] != -1)
		closeFd(pipe_p2c[0]);
	if (pipe_p2c[1] != -1)
	{
		if (epoll_ctl(epollFD, EPOLL_CTL_DEL, pipe_p2c[1], NULL) == -1 &&
			errno != ENOENT &&
			errno != EBADF)
		{
			std::cerr << "epoll_ctl(DEL): " << strerror(errno) << std::endl;
		}
		cgiProcesses.erase(pipe_p2c[1]);
		closeFd(pipe_p2c[1]);
	}
	if (pipe_c2p[0] != -1)
	{
		if (epoll_ctl(epollFD, EPOLL_CTL_DEL, pipe_c2p[0], NULL) == -1 &&
			errno != ENOENT &&
			errno != EBADF)
		{
			std::cerr << "epoll_ctl(DEL): " << strerror(errno) << std::endl;
		}
		cgiProcesses.erase(pipe_c2p[0]);
		closeFd(pipe_c2p[0]);
	}
	if (pipe_c2p[1] != -1)
		closeFd(pipe_c2p[1]);
	if (cgi->pid > 0)
	{
		waitpid(cgi->pid, NULL, WNOHANG);
		kill(cgi->pid, SIGKILL);
		cgi->pid = -1;
	}
}



static void	removeCGIChild(int pipe_p2c[2], int pipe_c2p[2])
{
	if (pipe_p2c[0] != -1)
		closeFd(pipe_p2c[0]);
	if (pipe_p2c[1] != -1)
		closeFd(pipe_p2c[1]);
	if (pipe_c2p[0] != -1)
		closeFd(pipe_c2p[0]);
	if (pipe_c2p[1] != -1)
		closeFd(pipe_c2p[1]);
}



void Server::handleCGIEvent(int fd, uint32_t events)
{
	std::map<int, CGIInfo>::iterator	it = cgiProcesses.find(fd);
	if (it == cgiProcesses.end())
	{
		epoll_ctl(epollFD, EPOLL_CTL_DEL, fd, NULL);
		return;
	}
	CGIInfo&							info = it->second;
	std::shared_ptr<CGI>				cgi = info.cgi;
	if (info.pipeIsInput)
	{
		if (events & (EPOLLERR | EPOLLHUP))
			handleCGIError(info);
		else if (!cgi->write_finished && (events & EPOLLOUT))
			handleCGIWrite(info);
	}
	else
	{
		if (!cgi->read_finished && (events & (EPOLLIN | EPOLLHUP | EPOLLERR)))
			handleCGIRead(info);
		if (!cgi->read_finished && (events & EPOLLERR))
			handleCGIError(info);
	}
	handleCGIWait(info);
	if (cgi->write_finished && cgi->read_finished && cgi->cgi_finished)
	{
		if (cgi->output.empty())
			handleCGIError502(info);
		else
			handleCGIResponse(info);
		handleCGICleanUp(cgi);
	}
}



void Server::handleCGIError(CGIInfo& info)
{
	std::shared_ptr<CGI>	cgi = info.cgi;

	std::cerr << "CGI error — aborting (PID " << cgi->pid << ")." << std::endl;
	if (cgiProcesses.count(cgi->fd_stdin))
	{
		epoll_ctl(epollFD, EPOLL_CTL_DEL, cgi->fd_stdin, NULL);
		cgiProcesses.erase(cgi->fd_stdin);
		closeFd(cgi->fd_stdin);
	}
	if (cgiProcesses.count(cgi->fd_stdout))
	{
		epoll_ctl(epollFD, EPOLL_CTL_DEL, cgi->fd_stdout, NULL);
		cgiProcesses.erase(cgi->fd_stdout);
		closeFd(cgi->fd_stdout);
	}
	if (cgi->pid > 0)
	{
		kill(cgi->pid, SIGKILL);
		waitpid(cgi->pid, NULL, WNOHANG);
		cgi->pid = -1;
	}
	cgi->output.clear();
	cgi->write_finished = true;
	cgi->read_finished = true;
	cgi->cgi_finished = true;
}



void Server::handleCGIWrite(CGIInfo& info)
{
	std::shared_ptr<CGI>	cgi = info.cgi;
	ssize_t					ret;

	while (cgi->body_written < cgi->body_size)
	{
		ret = write(cgi->fd_stdin, cgi->body.c_str() + cgi->body_written, cgi->body_size - cgi->body_written);
		if (ret < 0)
		{
			if (errno == EAGAIN || errno == EWOULDBLOCK)
				return;
			if (errno == EINTR)
				continue;
			std::cerr << "CGI write(): " << strerror(errno) << std::endl;
			handleCGIError(info);
			return;
		}
		cgi->body_written += ret;
	}

	epoll_ctl(epollFD, EPOLL_CTL_DEL, cgi->fd_stdin, NULL);
	cgiProcesses.erase(cgi->fd_stdin);
	closeFd(cgi->fd_stdin);
	cgi->write_finished = true;
}



void Server::handleCGIRead(CGIInfo& info)
{
	std::shared_ptr<CGI>	cgi = info.cgi;
	char					buf[65536];
	ssize_t					ret;

	while (true)
	{
		ret = read(cgi->fd_stdout, buf, sizeof(buf));
		if (ret > 0)
			cgi->output.append(buf, static_cast<size_t>(ret));
		else if (ret == 0)
		{
			epoll_ctl(epollFD, EPOLL_CTL_DEL, cgi->fd_stdout, NULL);
			cgiProcesses.erase(cgi->fd_stdout);
			closeFd(cgi->fd_stdout);
			cgi->read_finished = true;
			return;
		}
		else
		{
			if (errno == EAGAIN || errno == EWOULDBLOCK)
				return;
			if (errno == EINTR)
				continue;
			std::cerr << "CGI read(): " << strerror(errno) << std::endl;
			handleCGIError(info);
			return;
		}
	}
}



void Server::handleCGIWait(CGIInfo& info)
{
	std::shared_ptr<CGI>	cgi = info.cgi;
	if (cgi->cgi_finished ||
		cgi->pid <= 0)
		return;
	int						status = 0;
	pid_t					result = waitpid(cgi->pid, &status, WNOHANG);
	time_t					now;

	if (result == cgi->pid)
	{
		cgi->cgi_finished = true;
		if (WIFEXITED(status) && WEXITSTATUS(status) != 0)
			std::cerr << "CGI (PID " << cgi->pid << ") exited with status " << WEXITSTATUS(status) << std::endl;
		else if (WIFSIGNALED(status))
			std::cerr << "CGI (PID " << cgi->pid << ") killed by signal " << WTERMSIG(status) << std::endl;
		return;
	}
	if (result == -1)
	{
		if (errno == ECHILD)
		{
			std::cerr << "waitpid(): ECHILD for PID " << cgi->pid << std::endl;
			cgi->cgi_finished = true;
		}
		else
		{
			std::cerr << "waitpid(): " << strerror(errno) << std::endl;
			handleCGIError(info);
		}
		return;
	}
	now = time(NULL);
	if (now != (time_t)-1 &&
		(now - cgi->start_time) < TIMEOUT)
		return;
	std::cerr << "CGI timeout (PID " << cgi->pid << ") — killing." << std::endl;
	handleCGIError(info);
}



void Server::handleCGIResponse(CGIInfo& info)
{
	std::shared_ptr<CGI>	cgi = info.cgi;
	int						clientFD = info.clientFD;
	std::string				boundary;
	size_t					split;
	std::string				cgiHeaders;
	std::string				cgiBody;
	std::string				statusLine;
	size_t					statusPos;
	size_t					valueStart;
	size_t					valueEnd;
	size_t					lineEnd;
	std::string				response;

	boundary = "\r\n\r\n";
	split = cgi->output.find(boundary);
	if (split == std::string::npos)
	{
		boundary = "\n\n";
		split = cgi->output.find(boundary);
	}
	if (split == std::string::npos)
	{
		cgiHeaders = "Content-Type: text/html";
		cgiBody = cgi->output;
	}
	else
	{
		cgiHeaders = cgi->output.substr(0, split);
		cgiBody = cgi->output.substr(split + boundary.size());
	}
	statusLine = "200 OK";
	statusPos = cgiHeaders.find("Status:");
	if (statusPos != std::string::npos)
	{
		valueStart = statusPos + 7;
		while (valueStart < cgiHeaders.size() && cgiHeaders[valueStart] == ' ')
			valueStart++;
		valueEnd = cgiHeaders.find('\n', valueStart);
		statusLine = cgiHeaders.substr(valueStart, valueEnd == std::string::npos ? std::string::npos : valueEnd - valueStart);
		if (!statusLine.empty() && statusLine.back() == '\r')
			statusLine.pop_back();
		lineEnd = cgiHeaders.find('\n', statusPos);
		cgiHeaders.erase(statusPos, lineEnd == std::string::npos ? std::string::npos : lineEnd - statusPos + 1);
	}
	response =
		"HTTP/1.1 " + statusLine + "\r\n" +
		cgiHeaders + "\r\n" +
		"Content-Length: " + std::to_string(cgiBody.size()) + "\r\n" +
		"Connection: close\r\n\r\n" +
		cgiBody;
	queueCGIResponse(clientFD, response);
}



void Server::handleCGIError502(CGIInfo& info)
{
	std::string	response;

	std::cerr << "CGI failed — sending 502." << std::endl;
	response =
		"HTTP/1.1 502 Bad Gateway\r\n"
		"Content-Type: text/plain\r\n"
		"Content-Length: 11\r\n"
		"Connection: close\r\n\r\n"
		"Bad Gateway";
	queueCGIResponse(info.clientFD, response);
}



void Server::queueCGIResponse(int clientFD, const std::string& response)
{
	epoll_event	ev{};

	pendingWrites[clientFD] = response;
	writeOffsets[clientFD] = 0;
	closeAfterWrite[clientFD] = true;
	ev.events = EPOLLOUT;
	ev.data.fd = clientFD;
	if (epoll_ctl(epollFD, EPOLL_CTL_MOD, clientFD, &ev) == -1)
	{
		std::cerr << "CGI: epoll_ctl(MOD clientFD " << clientFD << "): " << strerror(errno) << std::endl;
		RemoveClient(clientFD);
	}
}



void Server::handleCGICleanUp(std::shared_ptr<CGI> cgi)
{
	std::map<int, CGIInfo>::iterator	it = cgiProcesses.begin();
	int									orphanFd;

	while (it != cgiProcesses.end())
	{
		if (it->second.cgi == cgi)
		{
			orphanFd = it->first;
			epoll_ctl(epollFD, EPOLL_CTL_DEL, orphanFd, NULL);
			it = cgiProcesses.erase(it);
			closeFd(orphanFd);
		}
		else
			it++;
	}
	if (cgi->pid > 0)
		waitpid(cgi->pid, NULL, WNOHANG);
}
