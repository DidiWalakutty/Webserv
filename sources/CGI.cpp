#include "HTTPResponse.hpp"
#include "server.hpp"
#include "CGI.hpp"

// void Server::startCGI(int clientFD, const HTTPRequest& request, const std::string& filePath, const LocationParse& location)
// {
// 	// use the CGI struct: cgi->
// 	std::shared_ptr<CGI> cgi(new CGI);

// 	// test
// 	std::cout << "in start CGI" << std::endl;
// 	std::cout << "executable: " << location.cgi_executable;

// 	return;
// }

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



static int						childCGI(const HTTPRequest& request,const std::string& filePath, const LocationParse& location, int pipe_p2c[2], int pipe_c2p[2]);
static int						ignoreSigPipe(void);
static int						createPipe(int pipe2open[2]);
static pid_t					forkCGI();
static int						redirectPipe(int oldfd, int newfd);
static int						closePipe(int& pipe2close);
static std::vector<std::string>	buildArgV(const std::string& filePath, const LocationParse& location);
static std::string				method2Str(HTTPMethod method);
static std::string				protocol2Str(HTTPProtocolVersion version);
static std::vector<std::string>	buildEnvP(const HTTPRequest& request, const std::string& filePath);
static std::vector<char*>		str2Ptr(std::vector<std::string>& str);
static int						executeCGI(std::vector<std::string>& argV_str, std::vector<std::string>& envP_str);
static int						nonblockPipe(int fd);
static int						updateStruct(std::shared_ptr<CGI> cgi, int fd_stdin, int fd_stdout, const HTTPRequest& request);
static int						addProcess(std::shared_ptr<CGI> cgi, std::map<int, CGIInfo>& cgiProcesses, int clientFD);
static int						addEpoll(std::shared_ptr<CGI> cgi, int epollFD);
static void						removeCGIParent(std::shared_ptr<CGI> cgi, int pipe_p2c[2], int pipe_c2p[2], std::map<int, CGIInfo>& cgiProcesses, int epollFD);
static void						removeCGIChild(int pipe_p2c[2], int pipe_c2p[2]);



void	Server::startCGI(int clientFD, const HTTPRequest& request, const std::string& filePath, const LocationParse& location)
{
	std::shared_ptr<CGI>	cgi(new CGI);
	int						pipe_p2c[2] = {-1, -1};
	int						pipe_c2p[2] = {-1, -1};

	while (true)
	{		
		/* BLOCK SIGNAL SO THE CHILD DOESN"T CRASH THE PARENT (REPLACE TO START UP, ONLY NEEDS TO RUN ONCE?) */
		if (ignoreSigPipe())
			break;

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
			_exit(childCGI(request, filePath, location, pipe_p2c, pipe_c2p));

		/* ***PARENT*** */
		/* CLOSE PIPES */
		if (closePipe(pipe_p2c[0]) ||
			closePipe(pipe_c2p[1]))
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



static int	childCGI(const HTTPRequest& request, const std::string& filePath, const LocationParse& location, int pipe_p2c[2], int pipe_c2p[2])
{
	std::vector<std::string>	argV_str;
	std::vector<std::string>	envP_str;

	/* REDIRECT */
	if (redirectPipe(pipe_p2c[0], STDIN_FILENO) ||
		redirectPipe(pipe_c2p[1], STDOUT_FILENO) ||
		redirectPipe(pipe_c2p[1], STDERR_FILENO))
		return (removeCGIChild(pipe_p2c, pipe_c2p), 1);

	/* CLOSE PIPES */
	if (closePipe(pipe_p2c[0]) ||
		closePipe(pipe_p2c[1]) ||
		closePipe(pipe_c2p[0]) ||
		closePipe(pipe_c2p[1]))
		return (removeCGIChild(pipe_p2c, pipe_c2p), 2);

	/* BUILD ARGV */
	argV_str = buildArgV(filePath, location);
	if (argV_str.empty())
		return (3);

	/* BUILD ENVP */
	envP_str = buildEnvP(request, filePath);
	if (envP_str.empty())
		return (4);

	/* EXECUTE */
	if (executeCGI(argV_str, envP_str))
		return (5);
	return (0);
}



static int	ignoreSigPipe(void)
{
	if (signal(SIGPIPE, SIG_IGN) == SIG_ERR)
	{
		std::cerr << "signal(SIGPIPE): " << strerror(errno) << std::endl;
		return (1);
	}
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



static int	closePipe(int& pipe2close)
{
	if (pipe2close == -1)
		return (0);
	if (close(pipe2close) == -1)
	{
		std::cerr << "close(): " << strerror(errno) << std::endl;
		return (1);
	}
	pipe2close = -1;
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



static std::vector<std::string>	buildEnvP(const HTTPRequest& request, const std::string& filePath)
{
	std::vector<std::string>	envP_str;

	if (request.method == HTTPMethod::UNSUPPORTED)
	{
		std::cerr << "Unsupported method" << std::endl;
		return {};
	}
	envP_str.push_back("REQUEST_METHOD=" + method2Str(request.method));
	envP_str.push_back("SCRIPT_NAME=" + request.resourcePath);
	envP_str.push_back("SCRIPT_FILENAME=" + filePath);
	envP_str.push_back("SERVER_PROTOCOL=" + protocol2Str(request.protocolVersion));
	envP_str.push_back("GATEWAY_INTERFACE=CGI/1.1");
	envP_str.push_back("REDIRECT_STATUS=200");
	envP_str.push_back("PATH_INFO=" + request.resourcePath);
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
	int	flags = fcntl(fd, F_GETFL, 0);

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
			std::cerr << "FD already tracked: " << cgi->fd_stdin << std::endl;
			return (1);
		}
		cgiProcesses[cgi->fd_stdin] = CGIInfo{cgi, true, clientFD};
	}
	if (cgiProcesses.count(cgi->fd_stdout))
	{
		std::cerr << "FD already tracked: " << cgi->fd_stdout << std::endl;
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
		evIn.events = EPOLLOUT;
		evIn.data.fd = cgi->fd_stdin;
		if (epoll_ctl(epollFD, EPOLL_CTL_ADD, cgi->fd_stdin, &evIn) == -1 &&
			errno != EEXIST)
		{
			std::cerr << "epoll_ctl(ADD): " << strerror(errno) << std::endl;
			return (1);
		}
	}
	evOut.events = EPOLLIN;
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
		closePipe(pipe_p2c[0]);
	if (pipe_p2c[1] != -1)
	{
		if (epoll_ctl(epollFD, EPOLL_CTL_DEL, pipe_p2c[1], NULL) == -1 &&
			errno != ENOENT &&
			errno != EBADF)
		{
			std::cerr << "epoll_ctl(DEL): " << strerror(errno) << std::endl;
		}
		cgiProcesses.erase(pipe_p2c[1]);
		closePipe(pipe_p2c[1]);
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
		closePipe(pipe_c2p[0]);
	}
	if (pipe_c2p[1] != -1)
		closePipe(pipe_c2p[1]);
	if (cgi->pid > 0)
	{
		kill(cgi->pid, SIGKILL);
		waitpid(cgi->pid, &status, WNOHANG);
	}
}



static void	removeCGIChild(int pipe_p2c[2], int pipe_c2p[2])
{
	if (pipe_p2c[0] != -1)
		closePipe(pipe_p2c[0]);
	if (pipe_p2c[1] != -1)
		closePipe(pipe_p2c[1]);
	if (pipe_c2p[0] != -1)
		closePipe(pipe_c2p[0]);
	if (pipe_c2p[1] != -1)
		closePipe(pipe_c2p[1]);
}
