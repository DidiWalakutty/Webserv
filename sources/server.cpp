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
#include <cctype>
#include <cerrno>

const int MAX_CLIENTS = 1024;
const size_t READ_BUFFER_SIZE = 65536;  // 64KB per read

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
		
		SetNonBlocking(socketFD);

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

		serverSockets.push_back(socketFD);
	}
}

void Server::setMaxRequestSize(size_t size)
{
	maxRequestSize = size;
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
				// The socket is marked nonblocking and no connections are present to be accepted.
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

		// Remember which server accepted this client
		for (size_t i = 0; i < serverSockets.size(); i++)
		{
			if (serverSockets[i] == event.data.fd)
			{
				clientToServer[clientFD] = i;
				break;
			}
		}

		epoll_event event{};
		event.events = EPOLLIN;
		event.data.fd = clientFD;

		if (epoll_ctl(epollFD, EPOLL_CTL_ADD, clientFD, &event) < 0)
		{
			throw(std::runtime_error("Failed to add client socket to epoll."));
		}

		// std::cout << std::endl
		// 		  << "Added FD: " << event.data.fd << std::endl;
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

	clients.erase(clients.begin() + index);
	clientBuffers.erase(clientFD);  // Clean up incomplete request buffer
	clientToServer.erase(clientFD); // Remove stored mapping of client to server

	std::cout << std::endl
			  << "Removed FD: " << clientFD << std::endl;
}

// Accumulates request data from a client until a complete request is available.
// Returns empty vector if incomplete, full request vector if complete.
std::vector<char> Server::ReadClient(const int &FD)
{
	std::vector<char> tempBuffer(READ_BUFFER_SIZE);
	std::vector<char> result;
	
	// Read new data from socket
	ssize_t bytesRead = read(FD, tempBuffer.data(), READ_BUFFER_SIZE);
	
	if (bytesRead < 0)
	{
		if (errno == EAGAIN || errno == EWOULDBLOCK)
		{
			// No new data available right now - check if we have incomplete request buffered
			if (clientBuffers.find(FD) != clientBuffers.end())
			{
				// Return empty - keep waiting for more data
				return result;
			}
			return result;
		}
		else
		{
			// Read error - remove this client instead of crashing the server
			std::cerr << "Read error on FD " << FD << ": " << strerror(errno) << std::endl;
			clientBuffers.erase(FD);
			RemoveClient(FD);
			return result;
		}
	}
	else if (bytesRead == 0)
	{
		// EOF - client closed connection
		clientBuffers.erase(FD);
		RemoveClient(FD);
		return result;
	}
	
	// this client was not previously buffered, so we create a new entry for it.
	if (clientBuffers.find(FD) == clientBuffers.end())
	{
		clientBuffers[FD] = std::string();
	}

	// add new data to client's buffer
	clientBuffers[FD].append(tempBuffer.data(), bytesRead);
	
	// Check if we have a complete HTTP request (headers + full body)
	std::string& data_str = clientBuffers[FD];
	size_t headersEnd = data_str.find("\r\n\r\n");
	
	if (headersEnd != std::string::npos)
	{
		// Found headers end, check Content-Length
		size_t contentLengthPos = data_str.find("Content-Length:");
		if (contentLengthPos != std::string::npos)
		{
			contentLengthPos += 15;  // strlen("Content-Length:")
			// Skip whitespace
			while (contentLengthPos < data_str.size() && 
			       (data_str[contentLengthPos] == ' ' || data_str[contentLengthPos] == '\t'))
			{
				contentLengthPos++;
			}
			// Extract the number
			size_t endPos = contentLengthPos;
			while (endPos < data_str.size() && std::isdigit(data_str[endPos]))
			{
				endPos++;
			}
			ssize_t contentLength = std::stoll(data_str.substr(contentLengthPos, endPos - contentLengthPos));
			// 4 bytes for the "\r\n\r\n" after headers
			size_t bodyStart = headersEnd + 4;
			size_t bodySize = data_str.size() - bodyStart;
			
			// Check if we have all the body data
			if (bodySize >= static_cast<size_t>(contentLength))
			{
				// Complete request! Convert to vector and clear buffer
				result = std::vector<char>(data_str.begin(), data_str.end());
				clientBuffers.erase(FD);
				return result;
			}
			// Incomplete - keep accumulating, return empty vector
			return result;
		}
		else
		{
			// No Content-Length (GET/HEAD/etc), just headers is enough
			result = std::vector<char>(data_str.begin(), data_str.end());
			clientBuffers.erase(FD);
			return result;
		}
	}
	
	// Headers not complete yet - keep waiting
	return result;
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
				if (clients.size() < MAX_CLIENTS)
				{
					AddClient(events[i]);
				}
				else
				{
					// Accept and immediately close to drain the kernel's pending connection queue,
					// otherwise the server socket keeps firing EPOLLIN endlessly.
					int tempFD = accept(events[i].data.fd, NULL, NULL);
					if (tempFD >= 0)
						close(tempFD);
					std::cerr << "Too many clients connected. Rejected new connection." << std::endl;
				}
			}
			else if (events[i].events & (EPOLLERR | EPOLLHUP | EPOLLRDHUP))	// If it's an error, remove Client
			{
				std::cout << "Remove client" << std::endl;
				RemoveClient(events[i].data.fd);
			}
			else if (events[i].events & EPOLLIN)		// If socket received input
			{
				// --- Read Request ---
				
				std::vector<char> data = ReadClient(events[i].data.fd); // using first server's max body size as reference for reading
				if (data.size() == 0)
				{
					// Empty data, client closed connection or already removed
					continue;
				}
				
				std::string rawRequest(data.data(), data.size());
				// Safety check: skip if request is empty (closed connection during keep-alive)
				if (rawRequest.empty())
				{
					continue;
				}
				
				if (data.size() > 0)
					std::cout << std::endl
							  << BOLDYELLOW << "Read FD: " << events[i].data.fd 
							  << " (data size: " << data.size() << " bytes)" << std::endl;

				// --- Parse Request ---
				HTTPRequest request(this);
				try
				{
					if (!request.parseRequest(rawRequest))
						continue;
					request.printRequest();
					std::cout << RESET << std::endl;
				}
				// Treat malformed requests as graceful disconnect, not an error
				catch (const HTTPRequest::HTTPRequestException &exc)
				{
					std::string excMsg = exc.what();
					// Skip error logging for common disconnect/malformed request cases
					if (excMsg != "Empty (raw) request string" && 
					    excMsg.find("Invalid request format") == std::string::npos)
					{
						std::cerr << "Failed to parse HTTP request: " << excMsg << std::endl;
					}
					// Send 400 Bad Request with error.html if available
					std::string body;
					std::string contentType = "text/plain";
					std::ifstream errorFile("www/html/error.html", std::ios::binary);
					if (errorFile.is_open())
					{
						std::ostringstream buf;
						buf << errorFile.rdbuf();
						body = buf.str();
						contentType = "text/html";
						auto replaceAll = [](std::string& s, const std::string& from, const std::string& to) {
							size_t pos = 0;
							while ((pos = s.find(from, pos)) != std::string::npos) { s.replace(pos, from.size(), to); pos += to.size(); }
						};
						replaceAll(body, "{{STATUS_CODE}}", "400");
						replaceAll(body, "{{REASON_PHRASE}}", "Bad Request");
						replaceAll(body, "{{DESCRIPTION}}", "The server could not understand the request due to invalid syntax.");
					}
					else
					{
						body = "400: Bad Request";
					}
					std::string response =
						"HTTP/1.1 400 Bad Request\r\nContent-Type: " + contentType +
						"\r\nContent-Length: " + std::to_string(body.size()) +
						"\r\nConnection: close\r\n\r\n" + body;
					ssize_t bw = write(events[i].data.fd, response.c_str(), response.size());
					(void)bw;
					RemoveClient(events[i].data.fd);
					continue;
				}

				// --- Select appropriate server based on which listening socket accepted the client ---
				const ServerParse* serverPtr = nullptr;
				
				// Looks up the client FD and which server is registrered with it.
				std::map<int, size_t>::iterator it = clientToServer.find(events[i].data.fd);
				// If we found the FD in the map, get corresponding server config.
				if (it != clientToServer.end())
				{
					// ->second is the index of the server.
					serverPtr = &_servers[it->second];
				}

				// If no server found, failed to find server of client FD.
				if (!serverPtr)
				{
					std::cerr << "Failed to find server for client FD: " << events[i].data.fd << std::endl;
					RemoveClient(events[i].data.fd);
					continue;
				}


				// --- Build and Send Response ---
				HTTPResponse response(*serverPtr);
				std::string responseStr = response.buildResponse(request);
				// send back HTTP Response to client
				ssize_t writeSize = write(events[i].data.fd, responseStr.c_str(), responseStr.size());
				if (writeSize < 0)
				{
					std::cerr << "Failed to write response to client." << std::endl;
					RemoveClient(events[i].data.fd);
				}
				else // Close connection if client requested it or if HTTP/1.0 (close-by-default)
				{
					auto connIt = request.headers.find("CONNECTION");
					bool clientWantsClose = (connIt != request.headers.end() &&
					                         connIt->second.find("close") != std::string::npos);
					bool http10 = (request.protocolVersion == HTTPProtocolVersion::HTTP_1_0);
					if (clientWantsClose || http10)
						RemoveClient(events[i].data.fd);
				}
			}
		}
	}

	running = false;

	Destroy();
}

bool Server::running = false;