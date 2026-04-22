#include "HTTPResponse.hpp"
#include "server.hpp"
#include "CGI.hpp"

	// 		- for a simple blocking/synchronous CGI, your current parameters are enough
	//		- for a real epoll-based CGI, you probably also need the client fd or another stored link to the client
	// 0. prepare CGI struct with needed info (script path, executable, env variables)

	// 1. Create 2 pipes to communicate with the CGI process
			// pipeToChild -> send POST body
			// pipeFromChild -> read CGI output (headers + body)
		
	// 2. Fork  a child process (to run the CGI script).
			// In the child process == CGI script, we will execute the CGI script.
			// In the parent process == server, we will send input (if POST) and read output from the CGI.

	// 3. Child process setup: 
			// if pid == 0 -> in child process: redirect stdin/stdout to pipes
			// close unused pipes
			// change directory to the CGI script's directory (b/o relative paths)

	// 4. Set environment variables (must be inside the child before execve)
			// Store them in tempENV as "KEY=VALUE" strings
			// Then convert to char* array for execve (env vector in CGI struct)

	// 5. Execute the CGI script in the child process
			// If it fails, exit(1) + handle error in parent process

	// 6. Parent Process:
			// Close unused pipe ends
			// If POST, write request body to pipeToChild + close (signals EOF to CGI)
			// Read CGI output from pipeFromChild until EOF (returns 0) (CGI process ends and closes)
			// Wait for child to finish (waitpid) and check exit status for errors. Prevents zombie processes

	// 7. Parse CGI output: seperate headers and body (split by \r\n\r\n)
			// Set CGI output headers in HTTP response headers
			// Set CGI output body as HTTP response body

	// --- !!! May have to store http repsonse in client output buffer until fully written !!! ---

// }



static int						childCGI(const HTTPRequest& request, const ServerParse& server, const std::string& filePath, const LocationParse& location, int pipe_p2c[2], int pipe_c2p[2]);
// static int						ignoreSigPipe(void);
static int						createPipe(int pipe2open[2]);
static pid_t					forkCGI();
static int						redirectPipe(int oldfd, int newfd);
static int						closeFd(int& fd);
static std::vector<std::string>	buildArgV(const std::string& filePath, const LocationParse& location);
static std::string				method2Str(HTTPMethod method);
static std::string				protocol2Str(HTTPProtocolVersion version);
static std::vector<std::string>	buildEnvP(const HTTPRequest& request, const ServerParse& server, const std::string& filePath, const LocationParse& location);
static std::vector<char*>		str2Ptr(std::vector<std::string>& str);
static int						executeCGI(std::vector<std::string>& argV_str, std::vector<std::string>& envP_str);
static int						nonblockPipe(int fd);
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
		// /* BLOCK SIGNAL SO THE CHILD DOESN"T CRASH THE PARENT (REPLACE TO START UP, ONLY NEEDS TO RUN ONCE?) */
		// if (ignoreSigPipe())
		// 	break;

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
		if (nonblockPipe(pipe_p2c[1]) ||
			nonblockPipe(pipe_c2p[0]))
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



// static int	ignoreSigPipe(void)
// {
// 	if (signal(SIGPIPE, SIG_IGN) == SIG_ERR)
// 	{
// 		std::cerr << "signal(SIGPIPE): " << strerror(errno) << std::endl;
// 		return (1);
// 	}
// 	return (0);
// }



static int	createPipe(int pipe2open[2])
{
	if (pipe(pipe2open) == -1)
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
	envP_str.push_back("REMOTE_ADDR=" + getpeername());
	envP_str.push_back("QUERY_STRING=" + request.queryStringCGI);
	if (request.method == HTTPMethod::POST ||
		request.method == HTTPMethod::PUT ||
		request.method == HTTPMethod::PATCH)
	{
		std::string	len = "";
		if (request.headers.count("Content-Length"))
			len = request.headers.at("Content-Length");
		envP_str.push_back("CONTENT_LENGTH=" + len);
		std::string	type = "";
		if (request.headers.count("Content-Type"))
			type = request.headers.at("Content-Type");
		envP_str.push_back("CONTENT_TYPE=" + type);
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



static int	nonblockPipe(int fd)
{
	if (fcntl(fd, F_SETFL, O_NONBLOCK) == -1)
	{
		std::cerr << "fcntl(F_SETFL): " << strerror(errno) << std::endl;
		return (1);
	}
	if (fcntl(fd, F_SETFD, FD_CLOEXEC) == -1)
	{
		std::cerr << "fcntl(F_SETFD): " << strerror(errno) << std::endl;
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
		evIn.events  = EPOLLOUT | EPOLLERR | EPOLLHUP;
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
	int	status;

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
		waitpid(cgi->pid, &status, WNOHANG);
		kill(cgi->pid, SIGKILL);
		waitpid(cgi->pid, &status, 0);
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











static void	handleCGIWrite(int fd, std::shared_ptr<CGI> cgi,  int epollFD, std::map<int, CGIInfo>& cgiProcesses);
static void	handleCGIRead(int fd, std::shared_ptr<CGI> cgi, int epollFD, std::map<int, CGIInfo>& cgiProcesses);
static void	cleanupCGI(std::shared_ptr<CGI> cgi);



void	Server::handleCGIEvent(int fd, uint32_t events)
{
	std::map<int, CGIInfo>::iterator	it = cgiProcesses.find(fd);
	if (it == cgiProcesses.end())
		return;
	CGIInfo&							info = it->second;
	std::shared_ptr<CGI>				cgi = info.cgi;
	int									status = 0;
	pid_t								result;

	if (events & (EPOLLERR | EPOLLHUP))
	{
		if (info.pipeIsInput)
			cgi->write_finished = true;
		else
			cgi->read_finished = true;
	}



	if (info.pipeIsInput == true &&
		cgi->write_finished == false &&
		(events & EPOLLOUT))
	{
		handleCGIWrite(fd, cgi, epollFD, cgiProcesses);
		if (cgi->write_finished == true)
		{
			epoll_ctl(epollFD, EPOLL_CTL_DEL, fd, NULL);
			cgiProcesses.erase(fd);
			closeFd(fd);
		}
	}
	else if (info.pipeIsInput == false &&
		cgi->read_finished == false &&
		(events & EPOLLIN))
	{
		handleCGIRead(fd, cgi, epollFD, cgiProcesses);
		if (cgi->read_finished == true)
		{
			epoll_ctl(epollFD, EPOLL_CTL_DEL, fd, NULL);
			cgiProcesses.erase(fd);
			closeFd(fd);
		}
		
	}


	// TIMEOUT!!!
	if (waitpid(cgi->pid, &status, WNOHANG) == cgi->pid)
		cgi->cgi_finished = true;






	if (cgi->write_finished == true &&
		cgi->read_finished == true &&
		cgi->cgi_finished == true)
	{
		if (cgi->output.empty())
		{
			cgi->output =
				"HTTP/1.1 502 Bad Gateway\r\n"
				"Content-Length: 0\r\n"
				"Connection: close\r\n\r\n";
		}





		pendingWrites[info.clientFD] = cgi->output;
		writeOffsets[info.clientFD] = 0;
		closeAfterWrite[info.clientFD] = true;
		epoll_event ev{};
		ev.events = EPOLLOUT;
		ev.data.fd = info.clientFD;
		epoll_ctl(epollFD, EPOLL_CTL_MOD, info.clientFD, &ev);
		cleanupCGI(cgi);
	}
}



static void	handleCGIWrite(int fd, std::shared_ptr<CGI> cgi, int epollFD, std::map<int, CGIInfo>& cgiProcesses)
{
	ssize_t	n;

	while (cgi->body_written < cgi->body_size)
	{
		n = write(fd, cgi->body.data() + cgi->body_written, cgi->body_size - cgi->body_written);
		if (n > 0)
			cgi->body_written += n;
		else if (n == 0)
			return;
		else
		{
			if (errno == EINTR)
				continue;
			if (errno == EAGAIN || errno == EWOULDBLOCK)
				return;
			if (errno == EPIPE)
				break;
			break;
		}
	}
	cgi->write_finished = true;
}



static void	handleCGIRead(int fd, std::shared_ptr<CGI> cgi, int epollFD, std::map<int, CGIInfo>& cgiProcesses)
{
	char	buffer[8192];
	ssize_t	n;

	while (true)
	{
		n = read(fd, buffer, sizeof(buffer));
		if (n > 0)
			cgi->output.append(buffer, n);
		else if (n == 0)
			break;
		else
		{
			if (errno == EINTR)
				continue;
			if (errno == EAGAIN || errno == EWOULDBLOCK)
				return;
			break;
		}
	}
	cgi->read_finished = true;
}



static void	cleanupCGI(std::shared_ptr<CGI> cgi)
{
	int	status;
	
	if (cgi->fd_stdin != -1)
		closeFd(cgi->fd_stdin);
	if (cgi->fd_stdout != -1)
		closeFd(cgi->fd_stdout);
	waitpid(cgi->pid, &status, WNOHANG);
}







