#include "HTTPResponse.hpp"
#include "Server.hpp"
#include "CGI.hpp"



static int						childCGI(const HTTPRequest& request, const ServerParse& server, const std::string& filePath, const LocationParse& location, int pipe_p2c[2], int pipe_c2p[2]);
static int						createPipe(int fds[2]);
static pid_t					forkCGI();
static int						redirectPipe(int oldfd, int newfd);
static int						closeFd(int& fd);
static std::vector<std::string>	buildArgV(const std::string& filePath, const LocationParse& location);
static std::string				method2Str(HTTPMethod method);
static std::string				protocol2Str(HTTPProtocolVersion version);
static std::vector<std::string>	buildEnvP(const HTTPRequest& request, const ServerParse& server, const std::string& filePath, const LocationParse& location);
static std::vector<char*>		str2Ptr(std::vector<std::string>& str);
static int						executeCGI(std::vector<std::string>& argV_str, std::vector<std::string>& envP_str);
static int						nonblockFd(int fd);
static int						updateStruct(std::shared_ptr<CGI> cgi, int fd_stdin, int fd_stdout, int clientFD, const HTTPRequest& request);
static int						addProcess(std::shared_ptr<CGI> cgi, std::map<int, CGIInfo>& cgiProcesses, int clientFD);
static int						addEpoll(std::shared_ptr<CGI> cgi, int epollFD);
static void						removeCGIParent(std::shared_ptr<CGI> cgi, int pipe_p2c[2], int pipe_c2p[2], std::map<int, CGIInfo>& cgiProcesses, int epollFD);
static void						removeCGIChild(int pipe_p2c[2], int pipe_c2p[2]);

static std::string				parseCGIHeaders(const std::string& rawHeaders, std::string& statusLine);



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

		/* SET TO NONBLOCKING */
		if (nonblockFd(pipe_p2c[1]) ||
			nonblockFd(pipe_c2p[0]))
			break;

		/* UPDATE STRUCT */
		if (updateStruct(cgi, pipe_p2c[1], pipe_c2p[0], clientFD, request))
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
	removeClient(clientFD);
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



static int	createPipe(int fds[2])
{
	if (pipe(fds) == -1)
	{
		std::cerr << "pipe(): " << strerror(errno) << std::endl;
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

	if (!location.cgi_executable.empty())
		argV_str.push_back(location.cgi_executable);
	else if (location.cgi_extension == ".py")
		argV_str.push_back("/usr/bin/python3");
	else if (location.cgi_extension == ".sh")
		argV_str.push_back("/usr/bin/bash");
	else if (location.cgi_extension == ".php")
		argV_str.push_back("/usr/bin/php");
	else
		return (argV_str);
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
	envP_str.push_back("SCRIPT_FILENAME=" + filePath);
	if (request.method == HTTPMethod::POST ||
		request.method == HTTPMethod::PUT ||
		request.method == HTTPMethod::PATCH)
	{
		envP_str.push_back("CONTENT_LENGTH=" + (request.headers.count("CONTENT-LENGTH") ? request.headers.at("CONTENT-LENGTH") : "0"));
		envP_str.push_back("CONTENT_TYPE=" + (request.headers.count("CONTENT-TYPE") ? request.headers.at("CONTENT-TYPE") : ""));
	}
	if (location.cgi_extension == ".php")
		envP_str.push_back("REDIRECT_STATUS=200");
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



static int	updateStruct(std::shared_ptr<CGI> cgi, int fd_stdin, int fd_stdout, int clientFD, const HTTPRequest& request)
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
		closeFd(cgi->fd_stdin);
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
	closeFd(pipe_c2p[1]);
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



static void	removeCGIChild(int pipe_p2c[2], int pipe_c2p[2])
{
	closeFd(pipe_p2c[0]);
	closeFd(pipe_p2c[1]);
	closeFd(pipe_c2p[0]);
	closeFd(pipe_c2p[1]);
}



void	Server::handleCGIEvent(int fd, uint32_t events)
{
	std::map<int, CGIInfo>::iterator	it = cgiProcesses.find(fd);
	if (it == cgiProcesses.end())
	{
		epoll_ctl(epollFD, EPOLL_CTL_DEL, fd, NULL);
		return;
	}
	std::shared_ptr<CGI>	cgi = it->second.cgi;
	bool					pipeIsInput = it->second.pipeIsInput;

	handleCGITimeOut(cgi);
	// handleCGIError (called from timeout) erases entries from cgiProcesses,
	// invalidating 'it'. Do not access 'it' after this point.
	if (cgi->error)
	{
		handleCGIResponse(cgi);
		return;
	}
	if (pipeIsInput)
		handleCGIWrite(cgi, events);
	else
		handleCGIRead(cgi, events);
	handleCGIWait(cgi);
	handleCGIResponse(cgi);
}



void	Server::handleCGITimeOut(std::shared_ptr<CGI> cgi)
{
	time_t	now = time(NULL);

	if (now == (time_t)-1)
	{
		std::cerr << "time(NULL): " << strerror(errno) << std::endl;
		return;
	}
	if ((now - cgi->start_time) < TIMEOUT)
		return;
	cgi->time_out = true;
	handleCGIError(cgi);
}



void	Server::handleCGIError(std::shared_ptr<CGI> cgi)
{
	if (cgi->error)
		return;
	if (cgi->fd_stdin != -1 && cgiProcesses.count(cgi->fd_stdin))
	{
		epoll_ctl(epollFD, EPOLL_CTL_DEL, cgi->fd_stdin, NULL);
		cgiProcesses.erase(cgi->fd_stdin);
		closeFd(cgi->fd_stdin);
	}
	if (cgi->fd_stdout != -1 && cgiProcesses.count(cgi->fd_stdout))
	{
		epoll_ctl(epollFD, EPOLL_CTL_DEL, cgi->fd_stdout, NULL);
		cgiProcesses.erase(cgi->fd_stdout);
		closeFd(cgi->fd_stdout);
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
	cgi->error = true;
	handleCGIErrorResponse(cgi);
}



void	Server::handleCGIWrite(std::shared_ptr<CGI> cgi, uint32_t events)
{
	ssize_t	ret;

	if (cgi->error || cgi->write_finished)
		return;
	if (events & (EPOLLERR | EPOLLHUP))
	{
		handleCGIError(cgi);
		return;
	}
	if (!(events & EPOLLOUT))
		return;
	while (cgi->body_written < cgi->body_size)
	{
		ret = write(cgi->fd_stdin, cgi->body.c_str() + cgi->body_written, cgi->body_size - cgi->body_written);
		if (ret > 0)
		{
			cgi->body_written += ret;
			continue;
		}
		if (ret == 0)
		{
			std::cerr << "CGI write() returned 0" << std::endl;
			handleCGIError(cgi);
			return;
		}
		if (ret < 0 && (errno == EAGAIN || errno == EWOULDBLOCK))
			return;
		if (ret < 0 && errno == EINTR)
			continue;
		std::cerr << "CGI write(): " << strerror(errno) << std::endl;
		handleCGIError(cgi);
		return;
	}
	epoll_ctl(epollFD, EPOLL_CTL_DEL, cgi->fd_stdin, NULL);
	cgiProcesses.erase(cgi->fd_stdin);
	closeFd(cgi->fd_stdin);
	cgi->write_finished = true;
}




void	Server::handleCGIRead(std::shared_ptr<CGI> cgi, uint32_t events)
{
	char	buf[65536];
	ssize_t	ret;

	if (cgi->error || cgi->read_finished)
		return;
	if (events & EPOLLERR)
	{
		handleCGIError(cgi);
		return;
	}
	if (!(events & (EPOLLIN | EPOLLHUP)))
		return;
	while (true)
	{
		ret = read(cgi->fd_stdout, buf, sizeof(buf));
		if (ret > 0)
		{
			cgi->output.append(buf, static_cast<size_t>(ret));
			continue;
		}
		if (ret == 0)
		{
			epoll_ctl(epollFD, EPOLL_CTL_DEL, cgi->fd_stdout, NULL);
			cgiProcesses.erase(cgi->fd_stdout);
			closeFd(cgi->fd_stdout);
			cgi->read_finished = true;
			return;
		}
		if (errno == EAGAIN || errno == EWOULDBLOCK)
			return;
		if (errno == EINTR)
			continue;
		std::cerr << "CGI read(): " << strerror(errno) << std::endl;
		handleCGIError(cgi);
		return;
	}
}



void	Server::handleCGIWait(std::shared_ptr<CGI> cgi)
{
	int		status = 0;
	pid_t	result;

	if (cgi->error || cgi->cgi_finished)
		return;
	// When all I/O is done (both pipes closed), the child should exit immediately.
	// Use a blocking wait to avoid the race where WNOHANG misses a process that
	// has not yet been scheduled to exit, leaving cgi_finished permanently false.
	int flags = (cgi->write_finished && cgi->read_finished) ? 0 : WNOHANG;
	result = waitpid(cgi->pid, &status, flags);
	if (result == cgi->pid)
	{
		if (WIFEXITED(status) && WEXITSTATUS(status) != 0)
			std::cerr << "CGI (PID " << cgi->pid << ") exited with status " << WEXITSTATUS(status) << std::endl;
		else if (WIFSIGNALED(status))
			std::cerr << "CGI (PID " << cgi->pid << ") killed by signal " << WTERMSIG(status) << std::endl;
		cgi->cgi_finished = true;
		cgi->pid = -1;
	}
	else if (result == -1)
	{
		if (errno == ECHILD)
		{
			std::cerr << "waitpid(): ECHILD for PID " << cgi->pid << std::endl;
			cgi->cgi_finished = true;
			cgi->pid = -1;
		}
		else
		{
			std::cerr << "waitpid(): " << strerror(errno) << std::endl;
			handleCGIError(cgi);
		}
	}
}



void	Server::handleCGIErrorResponse(std::shared_ptr<CGI> cgi)
{
	std::string	response;

	if (cgi->time_out == false)
	{
		std::cerr << "CGI failed — sending 502." << std::endl;
		response =
			"HTTP/1.1 502 Bad Gateway\r\n"
			"Content-Type: text/plain\r\n"
			"Content-Length: 11\r\n"
			"Connection: close\r\n\r\n"
			"Bad Gateway";
	}
	else
	{
		std::cerr << "CGI timeout — sending 504." << std::endl;
		response =
			"HTTP/1.1 504 Gateway Timeout\r\n"
			"Content-Type: text/plain\r\n"
			"Content-Length: 15\r\n"
			"Connection: close\r\n\r\n"
			"Gateway Timeout";
	}
	queueCGIResponse(cgi->fd_client, response);
}



static std::string	parseCGIHeaders(const std::string& rawHeaders, std::string& statusLine)
{
	std::istringstream	stream(rawHeaders);
	std::string			line;
	std::string			headers;
	bool				hasContentType = false;

	while (std::getline(stream, line))
	{
		if (!line.empty() && line.back() == '\r')
			line.pop_back();
		if (line.empty())
			continue;
		std::string	lower(line);
		std::transform(lower.begin(), lower.end(), lower.begin(), [](unsigned char c){return std::tolower(c);});
		if (lower.compare(0, 7, "status:") == 0)
		{
			statusLine = line.substr(7);
			size_t	start = statusLine.find_first_not_of(' ');
			if (start == std::string::npos)
				statusLine = "200 OK";
			else
				statusLine.erase(0, start);
			if (statusLine.size() < 3 || !std::isdigit(statusLine[0]) || !std::isdigit(statusLine[1]) || !std::isdigit(statusLine[2]) || (statusLine.size() > 3 && statusLine[3] != ' '))
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
	return (headers);
}



void	Server::handleCGIResponse(std::shared_ptr<CGI> cgi)
{
	size_t		split = cgi->output.find("\r\n\r\n");
	size_t		boundaryLen = 4;
	std::string	cgiHeaders;
	std::string	cgiBody;


	if (cgi->error || !cgi->write_finished || !cgi->read_finished || !cgi->cgi_finished)
		return;
	if (split == std::string::npos)
	{
		split = cgi->output.find("\n\n");
		boundaryLen = 2;
	}
	if (split == std::string::npos)
		cgiBody = cgi->output;
	else
	{
		cgiHeaders = cgi->output.substr(0, split);
		cgiBody = cgi->output.substr(split + boundaryLen);
	}
	std::string	statusLine = "200 OK";
	std::string	headers = parseCGIHeaders(cgiHeaders, statusLine);
	std::string	response =
		"HTTP/1.1 " + statusLine + "\r\n" +
		headers +
		"Content-Length: " + std::to_string(cgiBody.size()) + "\r\n"
		"Connection: close\r\n\r\n" +
		cgiBody;
	queueCGIResponse(cgi->fd_client, response);
}



void	Server::queueCGIResponse(int clientFD, const std::string& response)
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
		removeClient(clientFD);
	}
}


