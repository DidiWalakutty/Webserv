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
 * @brief Construct the server engine with parsed configs.
 *
 * @param serverConfigs A list of parsed ServerParse structures,
 *        each representing a server block from the .conf file.
 *
 * @details
 * - Stores all parsed server configurations in '_server',
 * - Registers the SIGINT handler so server can shut down cleanly (Ctrl+C).
 * - CreateSockets: listening sockets for each server configuration (one socket per ServerParse).
 * - CreateEpoll: the epoll instance and registers the listening sockets in it.
 * - If it fails, throw exception.
 */
Server::Server(const std::vector<ServerParse>& serverConfigs)
	: _servers(serverConfigs), epollFD(-1)
{
	signal(SIGINT, Interrupt);

	try
	{
		CreateSockets();	// like a door for clients to connect to
		CreateEpoll();		// like a notification mechanism
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
 * @brief Creates listening sockets for all servers.
 *
 * @details
 * A listening socket waits for incoming client connections.
 * It only accepts new connections and does not send/receive HTTP data itself.
 * Each accepted client gets its own separate client socket.
 * 
 * For each ServerParse in _servers:
 *  - Creates a non-blocking TCP socket.
 * 	- Set socket options (SO_REUSEADDR) to allow quick restart.
 * 	- Bind the socket to the configured host and port.
 *  - Start listening for incoming connections.
 * 
 *  Summarized:
 *  - One listening socket per server block (ServerParse).
 *  - All listening sockets are stored in serverSockets.
 *  - These sockets wlil be monitored by epoll.
 */
void Server::CreateSockets()
{
	// Clean up any existing sockets before creating new ones
	DestroySockets();

	if (!serverSockets.empty())
	{
		throw(std::runtime_error("Server sockets already exists."));
	}

	for (size_t i = 0; i < _servers.size(); ++i)
	{
		const ServerParse& server = _servers[i];

		// --- Create non-blocking TCP socket ---
		// AF_INET = IPv4, SOCK_STREAM = TCP (reliable connection), SOCK_NONBLOCK = non-blocking mode
		int socketFD = socket(AF_INET, SOCK_STREAM | SOCK_NONBLOCK, 0);
		if (socketFD < 0)
		{
			throw(std::runtime_error("Failed to create server socket."));
		}

		// --- Allow quick reuse of address/port after server restart ---
		int opt = 1;	// sets socket options -> prevents "Address already in use"
		if (setsockopt(socketFD, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt)) < 0)
		{
			throw(std::runtime_error("Failed to set socket option,"));
		}
		
		// --- Make socket non-blocking ---
		SetNonBlocking(socketFD);

		// --- Bind socket to configured host and port ---
		sockaddr_in address{};							// struct that holds IP + port for the socket
		address.sin_family = AF_INET;					// IPv4
		address.sin_port = htons(server.port);			// converts port num to network byte order for the socket
		address.sin_addr.s_addr = server.host.empty() 	// the IP address the socket listens on
									? INADDR_ANY		// if !host, INADDR_ANY listens on all network interfaces
									: inet_addr(server.host.c_str());	// converts string to numeric format for the socket

		// --- Bind socket to IP and port ---							
		if (bind(socketFD, (sockaddr*)&address, sizeof(address)) < 0)	// Associates the socket with a specific IP + port
		{
			throw(std::runtime_error("Failed to bind server socket."));
		}

		// Check to which IP the socket is bound.
		// char buf[INET_ADDRSTRLEN];
		// inet_ntop(AF_INET, &address.sin_addr, buf, sizeof(buf));
		// std::cout << "Bound socketFD " << socketFD << " to " << buf << ":" << ntohs(address.sin_port) << std::endl;

		// --- Start listening for incoming connections ---
		if (listen(socketFD, SOMAXCONN) < 0)			// SOMAXCONN == max queue of pending connections
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

// Check if incoming FD is one of the server sockets (indicates new client connection)
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

/**
 * @brief Accepts new client connections on a listening socket + adds them to epoll for monitoring.
 * 
 * @param event The epoll event triggered on a server socket indicating a new incoming connection.
 * @details
 * Calls accept() in a loop to handle all pending connections.
 * For each accepted client:
 * 	- Sets the client socket to non-blocking mode.
 *  - Stores the client FD in the server's clients list.
 *  - Maps which server accepted the client in clientToServer map for later reference when processing requests.
 *  - Registers the client socket in epoll for EPOLLIN events to read incoming requests.
 * 
 *  Because the listening socket is non-blocking, accept() must be called until it returns EAGAIN/EWOULDBLOCK
 *  which means the kernel has no more pending connections to accept.
 */
void Server::AddClient(const epoll_event &event)
{
	while (true)
	{
		sockaddr_in address{};
		socklen_t length = sizeof(address);

		int clientFD = accept(event.data.fd, (sockaddr *)&address, &length);
		if (clientFD < 0)
		{
			if (errno == EAGAIN || errno == EWOULDBLOCK)
			{
				// No more pending connections to accept
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

		// Map/remember which server this client is connected to
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
	pendingWrites.erase(clientFD);  // Clean up any unsent response data
	writeOffsets.erase(clientFD);
	closeAfterWrite.erase(clientFD);

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
 *  @brief Main server loop that handles connections and I/O events/requests.
 * 
 * @details
 * - Uses epoll to wait for events on all registered file descriptors 
 * 	 (server sockets, client sockets and CGI pipes).
 * - Waits for I/O using epoll_wait(), then processes each triggered event:
 * 		- Server Socket (EPOLLIN):
 * 			-> Accept new client connection(s) and add them to epoll for monitoring.
 * 		- Client Socket (EPOLLIN):
 * 			-> EPOLLIN: read incoming data, parse HTTP request, build response and store it in write buffer.
* 			-> EPOLLOUT: send buffered response data (handle partial writes because of non-blocking sockets) 
						 and clean up or keep connection alive when done.
 * 		- CGI pipe (EPOLLIN):
 * 			-> Read CGI output and convert it into a response.
 * - Handles disconnects and socket errors by removing clients and cleaning up resources.
 * - Continues running until Server::running is set to false (SIGINT handler).
 * */ 
void Server::Start()
{
	std::cout << CYAN << "--- Welcome to Webserv ---" << RESET << std::endl;
	std::cout << CYAN << "--- Server Side ---" << RESET << std::endl;
	running = true;

	// Buffer for epoll events added by epoll_wait
	epoll_event events[_maxEvents];

	// std::cout << "Server FDs in epoll: ";
	// for (size_t i = 0; i < serverSockets.size(); i++)
    // std::cout << serverSockets[i] << " ";
	// std::cout << std::endl;

	while (running)
	{
		/// --- Wait for events on registered FDs ---
		int count = epoll_wait(epollFD, events, _maxEvents, -1);
		if (count < 0) 
		{
			std::cerr << "epoll_wait failed: " << strerror(errno) << std::endl;
			break;
		}
		
		// --- Loop through all triggered events ---
		for (int i = 0; i < count; i++)
		{
			int fd = events[i].data.fd;

			// --- New Client Connection ---
			if (IsServerSocket(fd))	// Accept new connection + add client
			{
				if (clients.size() < MAX_CLIENTS)
				{
					AddClient(events[i]);
				}
				else
				{
					// If too many clients, accept + close immediately to prevent hanging connections and free up kernel queue
					// otherwise the server socket keeps firing EPOLLIN endlessly.
					int tempFD = accept(events[i].data.fd, NULL, NULL);
					if (tempFD >= 0)
						close(tempFD);
					std::cerr << "Too many clients connected. Rejected new connection." << std::endl;
				}
			}
			// --- Socket Error or Hang Up ---
			else if (events[i].events & (EPOLLERR | EPOLLHUP | EPOLLRDHUP))	// If it's an error, remove Client
			{
				std::cout << "Remove client" << std::endl;
				RemoveClient(events[i].data.fd);
			}
			// --- CGI Output from Child Process ---
			// else if (cgiProcesses.count(fd) && (events[i].events & EPOLLIN))
			// {
				// Handle CGI output from child process
				// 1. Read CGI output
				// 2. Append to cgiProcess->cgiOutput
				// 3. If EOF, parse output and send to client
				// 4. Clean up

			// }
			// --- Drain Pending Write ---
			else if (events[i].events & EPOLLOUT)
			{
				int fd = events[i].data.fd;

				if (!pendingWrites.count(fd))
				{
					// Nothing to send — switch back to reading
					epoll_event modEv{};
					modEv.events = EPOLLIN;
					modEv.data.fd = fd;
					epoll_ctl(epollFD, EPOLL_CTL_MOD, fd, &modEv);
					continue;
				}

				const std::string& data = pendingWrites[fd];
				size_t& offset = writeOffsets[fd];

				while (offset < data.size())
				{
					ssize_t sent = write(fd, data.c_str() + offset, data.size() - offset);
					if (sent < 0)
					{
						if (errno == EAGAIN || errno == EWOULDBLOCK)
							break; // Kernel buffer full — EPOLLOUT will fire again
						std::cerr << "Write error for client FD: " << fd << std::endl;
						RemoveClient(fd);
						goto next_event;
					}
					offset += static_cast<size_t>(sent);
					std::cout << "Bytes written: " << sent << " | Total sent: " << offset
					          << " / " << data.size() << std::endl;
				}

				if (offset >= data.size())
				{
					// All data sent — clean up and decide whether to keep alive
					bool shouldClose = closeAfterWrite.count(fd) && closeAfterWrite[fd];
					pendingWrites.erase(fd);
					writeOffsets.erase(fd);
					closeAfterWrite.erase(fd);

					if (shouldClose)
					{
						RemoveClient(fd);
					}
					else
					{
						// Go back to waiting for next request
						epoll_event modEv{};
						modEv.events = EPOLLIN;
						modEv.data.fd = fd;
						if (epoll_ctl(epollFD, EPOLL_CTL_MOD, fd, &modEv) < 0)
						{
							std::cerr << "Failed to re-register EPOLLIN for client: " << fd << std::endl;
							RemoveClient(fd);
						}
					}
				}
				next_event:;
			}
			// --- Regular Client Request ---
			else if (events[i].events & EPOLLIN)
			{
				// --- Read Request ---
				std::vector<char> data = ReadClient(events[i].data.fd);
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
					bool isPayloadTooLarge =
						excMsg == "Content-Length exceeds maximum allowed size" ||
						excMsg == "Body size exceeds maximum limit";
					HTTPState errorState = isPayloadTooLarge ? HTTPState::RequestTooLarge : HTTPState::BadRequest;
					HTTPMessage statusMessage = HTTPCommon::HTTPStatusMap.at(errorState);
					int statusCodeInt = static_cast<int>(errorState);

					const ServerParse* parserErrorServer = nullptr;
					std::map<int, size_t>::iterator mapIt = clientToServer.find(events[i].data.fd);
					if (mapIt != clientToServer.end())
						parserErrorServer = &_servers[mapIt->second];

					// Skip error logging for common disconnect/malformed request cases
					if (excMsg != "Empty (raw) request string" && 
					    excMsg.find("Invalid request format") == std::string::npos)
					{
						std::cerr << "Failed to parse HTTP request: " << excMsg << std::endl;
					}

					// Send parser error response with configured error page path if available.
					std::string body;
					std::string contentType = "text/plain";
					std::string errorPagePath = HTTPCommon::defaultErrorPagePath(statusMessage);
					if (parserErrorServer)
					{
						const std::string* customErrorPath = parserErrorServer->get_error_page(statusCodeInt);
						if (customErrorPath)
							errorPagePath = *customErrorPath;
					}

					std::ifstream errorFile(errorPagePath.c_str(), std::ios::binary);
					if (errorFile.is_open())
					{
						std::ostringstream buf;
						buf << errorFile.rdbuf();
						body = buf.str();
						contentType = "text/html";
						HTTPCommon::fillErrorPageTemplate(body, statusMessage);
					}
					else
					{
						body = statusMessage.code + ": " + statusMessage.message;
					}
					std::string response =
						"HTTP/1.1 " + statusMessage.code + " " + statusMessage.message + "\r\nContent-Type: " + contentType +
						"\r\nContent-Length: " + std::to_string(body.size()) +
						"\r\nConnection: close\r\n\r\n" + body;
					ssize_t bw = write(events[i].data.fd, response.c_str(), response.size());
					(void)bw;
					RemoveClient(events[i].data.fd);
					continue;
				}

				// --- Find corresponding server config for this client FD ---
				const ServerParse* serverPtr = nullptr;
				
				// Find which server this client is connected to using the clientToServer map
				std::map<int, size_t>::iterator it = clientToServer.find(events[i].data.fd);
				// If found, get the corresponding ServerParse pointer
				if (it != clientToServer.end())
				{
					// ->second is the index of the server in _servers vector
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
				// std::cout << "Response total size: " << responseStr.size() << std::endl;

				// --- Determine if connection should close after sending ---
				auto connIt = request.headers.find("CONNECTION");
				bool clientWantsClose = (connIt != request.headers.end() &&
				                         connIt->second.find("close") != std::string::npos);
				bool http10 = (request.protocolVersion == HTTPProtocolVersion::HTTP_1_0);
				closeAfterWrite[events[i].data.fd] = (clientWantsClose || http10);

				// --- Buffer response and register EPOLLOUT to drain it ---
				pendingWrites[events[i].data.fd] = responseStr;
				writeOffsets[events[i].data.fd] = 0;

				epoll_event writeEv{};
				writeEv.events = EPOLLOUT;
				writeEv.data.fd = events[i].data.fd;
				if (epoll_ctl(epollFD, EPOLL_CTL_MOD, events[i].data.fd, &writeEv) < 0)
				{
					std::cerr << "Failed to register EPOLLOUT for client: " << events[i].data.fd << std::endl;
					RemoveClient(events[i].data.fd);
				}
			}
		}
	}

	running = false;

	Destroy();
}

bool Server::running = false;