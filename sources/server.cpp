#include "server.hpp"
#include "Config.hpp"
#include "ConfigParser.hpp"

#include <iostream>
#include <stdexcept>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <fcntl.h>
#include <signal.h>
#include <string>
#include <cstring>

void Interrupt(int sig)
{
	if (sig == SIGINT)
	{
		Server::running = false;
	}
}

/**
 * @brief Construct a Server engine from parsed configuration blocks.
 *
 * @param serverConfigs A list of parsed ServerParse structures,
 *        each representing a server block from the .conf file.
 *
 * @details
 * The constructor:
 * - Saves the server configs internally.
 * - Creates listening sockets for all servers.
 * - Initializes epoll to handle connections.
 */
Server::Server(const std::vector<ServerParse>& serverConfigs)
	: _servers(serverConfigs), epollFD(-1)
{
	signal(SIGINT, Interrupt);

	try
	{
		CreateSockets();
		CreateEpoll();
		// std::cout << "Server is running on the following sockets: "; // Always starts from Socket 3
		// for (size_t i = 0; i < serverSockets.size(); ++i)
		// {
		// 	std::cout << serverSockets[i] << " ";
		// 	std::cout << std::endl;
		// }
	}
	catch (const std::exception& e)
	{
		std::cerr << "Failed to create server: " << e.what() << std::endl;
		Destroy();
	}
}

Server::~Server()
{
	std::cout << std::endl
			  << "Closing server." << std::endl;

	Destroy();
}

/**
 * @brief Create listening sockets for all servers.
 *
 * @details
 * For each ServerParse in _servers:
 *  - Creates a non-blocking TCP socket.
 *  - Allow quick restart with SO_REUSEADDR.
 *  - Binds to host and port.
 *  - Listens for incoming connections (SOMAXCONN backlog).
 *	- Each socketFD is stored in serverSockets and later added to epoll for monitoring
 * 
 * Differences from old code:
 * - Old code used `ServerConfig` with multiple ports and a SocketConfig.
 *   The type/domain/protocol were configurable via the struct.
 * - New code uses the parsed ServerParse:
 *     - Each server block has one port and host.
 *     - Type/domain/protocol are fixed (AF_INET, SOCK_STREAM | SOCK_NONBLOCK, 0).
 * - Old code assumed one server; new code supports multiple servers via _servers vector.
 */
void Server::CreateSockets()
{
	DestroySockets();

	if (!serverSockets.empty())
	{
		throw(std::runtime_error("Server sockets already exists."));
	}

	for (size_t i = 0; i < _servers.size(); ++i)
	{
		const ServerParse& server = _servers[i];

		int socketFD = socket(AF_INET, SOCK_STREAM | SOCK_NONBLOCK, 0);		// AF_INET = IPv4, SOCK_STREAM = TCP
		if (socketFD < 0)
		{
			throw(std::runtime_error("Failed to create server socket."));
		}

		int opt = 1;	// sets socket options. SO_REUSEADDR allows a quick server restart
		if (setsockopt(socketFD, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt)) < 0)
		{
			throw(std::runtime_error("Failed to set socket option,"));
		}

		sockaddr_in address{};							// struct that holds IP + port for the socket
		address.sin_family = AF_INET;					// IPv4
		address.sin_port = htons(server.port);			// convert port from host byte order to network byte order
		address.sin_addr.s_addr = server.host.empty() 	// the IP address the socket listens on
									? INADDR_ANY		// if !host, INADDR_ANY listens on all network interfaces
									: inet_addr(server.host.c_str());	// converts string to numeric format for the socket

		if (bind(socketFD, (sockaddr*)&address, sizeof(address)) < 0)	// Associates the socket with a specific IP + port
		{
			throw(std::runtime_error("Failed to bind server socket."));
		}

		// Check to which IP the socket is bound.
		// char buf[INET_ADDRSTRLEN];
		// inet_ntop(AF_INET, &address.sin_addr, buf, sizeof(buf));
		// std::cout << "Bound socketFD " << socketFD << " to " << buf << ":" << ntohs(address.sin_port) << std::endl;

		if (listen(socketFD, SOMAXCONN) < 0)			// Makes the socket start acception connections
		{
			throw(std::runtime_error("Failed to listen on server socket."));
		}

		SetNonBlocking(socketFD);
		serverSockets.push_back(socketFD);
	}
}

void Server::CreateEpoll()
{
	if (epollFD >= 0)
	{
		throw(std::runtime_error("Epoll instance already exists."));
	}

	epollFD = epoll_create(1);

	if (epollFD < 0)
	{
		throw(std::runtime_error("Failed to create epoll instance."));
	}

	for (const int &socketFD : serverSockets)
	{
		epoll_event event{};
		event.events = EPOLLIN;
		event.data.fd = socketFD;

		if (epoll_ctl(epollFD, EPOLL_CTL_ADD, socketFD, &event) < 0)
		{
			throw(std::runtime_error("Failed to add server socket to epoll."));
		}
	}
}

void Server::DestroySockets()
{
	for (int &socketFD : serverSockets)
	{
		if (socketFD < 0)
		{
			continue;
		}

		if (close(socketFD) < 0)
		{
			std::cerr << "Failed to close server socket." << std::endl;
		}

		socketFD = -1;
	}

	serverSockets.clear();
}

void Server::DestroyEpoll()
{
	for (size_t i = 0; i < clients.size(); i++)
	{
		if (clients[i] >= 0)
		{
			if (close(clients[i]) < 0)
			{
				std::cerr << "Failed to close client FD: " << clients[i] << "." << std::endl;
			}
		}
	}

	clients.clear();

	if (epollFD < 0)
	{
		return;
	}

	if (close(epollFD) < 0)
	{
		std::cerr << "Failed to close epoll instance." << std::endl;
	}

	epollFD = -1;
}

void Server::Destroy()
{
	DestroySockets();
	DestroyEpoll();
}

void Server::SetNonBlocking(const int &FD)
{
	int flags = fcntl(FD, F_GETFL, 0);

	if (flags < 0)
	{
		throw(std::runtime_error("Failed to retrieve FD flags."));
	}

	if (fcntl(FD, F_SETFL, flags | O_NONBLOCK) < 0)
	{
		throw(std::runtime_error("Failed to set FD flags."));
	}
}

bool Server::IsServerSocket(const int &FD)
{
	for (const int &socketFD : serverSockets)
	{
		if (socketFD == FD && socketFD >= 0)
		{
			return (true);
		}
	}
	return (false);
}

void Server::AddClient(const epoll_event &event)
{
	while (true)
	{
		sockaddr_in address{};
		socklen_t length = sizeof(in_addr);

		int clientFD = accept(event.data.fd, (sockaddr *)&address, &length);
		if (clientFD < 0)
		{
			if (errno == EAGAIN || errno == EWOULDBLOCK)
			{
				break;
			}

			throw(std::runtime_error("Failed to accept client connection."));
		}

		// Check to see if client was added
		// std::cout << "Accepted client FD: " << clientFD 
        //           << " from " << inet_ntoa(address.sin_addr)
        //           << ":" << ntohs(address.sin_port) << std::endl;
		
		clients.push_back(clientFD);
		SetNonBlocking(clientFD);

		epoll_event event{};
		event.events = EPOLLIN;
		event.data.fd = clientFD;

		if (epoll_ctl(epollFD, EPOLL_CTL_ADD, clientFD, &event) < 0)
		{
			throw(std::runtime_error("Failed to add client socket to epoll."));
		}

		std::cout << std::endl
				  << "Added FD: " << event.data.fd << std::endl;
	}
}

void Server::RemoveClient(const int &clientFD)
{
	if (clientFD < 0)
	{
		return;
	}

	int index = -1;
	for (size_t i = 0; i < clients.size(); i++)
	{
		if (clients[i] == clientFD)
		{
			index = (int)i;
			break;
		}
	}

	if (index < 0)
	{
		return;
	}

	if (close(clientFD) < 0)
	{
		std::cerr << "Failed to close client FD: " << clientFD << "." << std::endl;
	}

	clients[index] = -1;

	std::cout << std::endl
			  << "Removed FD: " << clientFD << std::endl;
}

// Resize at end resizes buffer to actual received data
std::vector<char> Server::ReadClient(const int &FD, const size_t size)
{
	std::vector<char> result;
	result.resize(size);

	ssize_t readSize = read(FD, result.data(), size);

	if (readSize == 0)	// client closed connection
	{
		RemoveClient(FD);
		result.clear();
	}
	if (readSize < 0 && errno != EAGAIN)	// error
	{
		throw(std::runtime_error("Failed to read client."));
	}

	result.resize(readSize);
	return (result);
}

/**
 * @brief Main server loop handling all connections and events.
 *
 * @details
 * - Waits for events on all server and client sockets using epoll.
 * - For server sockets: accepts new client connections.
 * - For client sockets: reads incoming data, parses HTTP requests, 
 *   generates responses, and writes them back.
 * - Handles client errors, disconnects, and cleanup automatically.
 * - Runs until Server::running is set to false (e.g., on SIGINT).
 * - Cleans up all sockets and epoll instance when the loop ends.
 * 
 * - epoll_event: List of notifications from the kernel. Each element contains:
 * 				- events[i].data.fd -> which socket
 * 				- events[i].events  -> what happened (readable, error etc)
 */
void Server::Start()
{
	std::cout << "--- Welcome to Webserv ---" << std::endl;
	std::cout << "--- Server Side ---" << std::endl;
	running = true;

	epoll_event events[_maxEvents];

	// std::cout << "Server FDs in epoll: ";
	// for (size_t i = 0; i < serverSockets.size(); i++)
    // std::cout << serverSockets[i] << " ";
	// std::cout << std::endl;

	while (running)
	{
		int count = epoll_wait(epollFD, events, _maxEvents, -1);
		if (count < 0) 
		{
			std::cerr << "epoll_wait failed: " << strerror(errno) << std::endl;
			break;
		}
		
		for (int i = 0; i < count; i++)	// handles each socket that changed state.
		{
			if (IsServerSocket(events[i].data.fd))	// Accept new connection + add client
			{
				AddClient(events[i]);
				continue;
			}

			if (events[i].events & (EPOLLERR | EPOLLHUP | EPOLLRDHUP))	// If it's an error, remove Client
			{
				std::cout << "Remove client" << std::endl;
				RemoveClient(events[i].data.fd);
				continue;
			}

			if (events[i].events & EPOLLIN)		// If socket received input
			{
				// --- Read Request ---
				std::vector<char> data = ReadClient(events[i].data.fd, 512);
				if (data.size() > 0)
					std::cout << std::endl
							  << BOLDYELLOW << "Read FD: " << events[i].data.fd << std::endl;

				// --- Parse Request ---
				HTTPRequest request;
				try
				{
					if (!request.parseRequest(std::string(data.data(), data.size())))
						continue;
					request.printRequest();
					std::cout << RESET << std::endl;
				}
				// --- !!! Always handles request as error, even if it was successfull ---
				catch (const HTTPRequest::HTTPRequestException &exc)
				{
					std::cerr << "Failed to parse HTTP request: " << exc.what() << std::endl;
					// Added, check if is good?
					RemoveClient(events[i].data.fd);
					continue;
				}
				if (data.size() == 0)
					RemoveClient(events[i].data.fd);

				// --- Select appropriate server based on Host header ---
				// the value of the Host header from the request, or empty if the client didn’t send it.
				const std::string hostHeader = request.headers.count("HOST") ? request.headers.at("HOST") : "";
				const ServerParse* serverPtr = nullptr;

				for (size_t s = 0; s < _servers.size(); s++)
				{
					if (_servers[s].serverName == hostHeader)
					{
						serverPtr = &_servers[s];
						break;
					}
				}

				if (!serverPtr && !_servers.empty())
					serverPtr = &_servers[0];	// fallback
				
				if (!serverPtr)
				{
					std::cerr << "Failed to find server configuration for request. Removing client FD: " << events[i].data.fd << std::endl;
					RemoveClient(events[i].data.fd);
					continue;
				}


				// --- Build and Send Response ---
				HTTPResponse response(*serverPtr);
				std::string responseStr = response.buildResponse(request);
				// send back HTTP Response to client
				ssize_t writeSize = write(events[i].data.fd, responseStr.c_str(), responseStr.size());
				// std::cout << std::endl
				// 		  << BOLDGREEN << "Wrote FD: " << events[i].data.fd << std::endl;
				// response.printResponse();
				// std::cout << RESET << std::endl;
				if (writeSize < 0)
				{
					std::cerr << "Failed to write response to client." << std::endl;
					RemoveClient(events[i].data.fd);
				}
				continue;
			}
		}
	}

	running = false;

	Destroy();
}

bool Server::running = false;