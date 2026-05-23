#include "Server.hpp"
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
const int INDEFINITE_BLOCKING = -1;
std::ostream& ERR = std::cerr;

namespace
{
	bool strContains(const std::string& text, const char* needle)
	{
		return text.find(needle) != std::string::npos;
	}

	void logColored(std::ostream& out, const std::string& msg, const char* color = NULL)
	{
		if (color && color[0] != '\0')
			out << color;
		out << msg;
		if (color && color[0] != '\0')
			out << RESET;
		out << std::endl;
	}

	void logColored(const std::string& msg, const char* color = NULL)
	{
		logColored(std::cout, msg, color);
	}
}

void Interrupt(int sig)
{
	if (sig == SIGINT)
	{
		Server::running = 0;
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
 * - createSockets: listening sockets for each server configuration (one socket per ServerParse).
 * - createEpoll: the epoll instance and registers the listening sockets in it.
 * - If it fails, throw exception.
 */
Server::Server(const std::vector<ServerParse>& serverConfigs)
	: _servers(serverConfigs), epollFD(-1)
{
	signal(SIGINT, Interrupt);
	signal(SIGPIPE, SIG_IGN);

	try
	{
		createSockets();	// like a door for clients to connect to
		createEpoll();		// like a notification mechanism
	}
	catch (const std::exception& e)
	{
		logColored(ERR, "Failed to create server: " + std::string(e.what()), RED);
		destroy();
	}
}

Server::~Server()
{
	logColored("Closing server.", CYAN);

	destroy();
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
 *  - All listening sockets are stored in listeningSockets.
 *  - These sockets wlil be monitored by epoll.
 */
void Server::createSockets()
{
	destroySockets();

	if (!listeningSockets.empty())
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
		int opt = 1;	// turns on option to reuse address/port -> prevents "Address already in use"
		if (setsockopt(socketFD, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt)) < 0)
		{
			throw(std::runtime_error("Failed to set socket option,"));
		}
		if (setsockopt(socketFD, SOL_SOCKET, SO_REUSEPORT, &opt, sizeof(opt)) < 0)
		{
			throw(std::runtime_error("Failed to set SO_REUSEPORT socket option."));
		}
		
		// --- Make socket non-blocking ---
		setNonBlocking(socketFD);

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

		// --- Start listening for incoming connections ---
		if (listen(socketFD, SOMAXCONN) < 0)			// SOMAXCONN == max queue of pending connections
		{
			throw(std::runtime_error("Failed to listen on server socket."));
		}

		listeningSockets.push_back(socketFD);
	}
}

void Server::setMaxRequestSize(size_t size)
{
	maxRequestSize = size;
}

void Server::createEpoll()
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

	for (const int &socketFD : listeningSockets)
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

void Server::destroySockets()
{
	for (int &socketFD : listeningSockets)
	{
		if (socketFD < 0)
		{
			continue;
		}

		if (close(socketFD) < 0)
		{
			logColored(ERR, "Failed to close server socket.", RED);
		}

		socketFD = -1;
	}

	listeningSockets.clear();
}

void Server::destroyEpoll()
{
	for (size_t i = 0; i < clients.size(); i++)
	{
		if (clients[i] >= 0)
		{
			if (close(clients[i]) < 0)
			{
				logColored(ERR, "Failed to close client FD: " + std::to_string(clients[i]) + ".", RED);
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
		logColored(ERR, "Failed to close epoll instance.", RED);
	}

	epollFD = -1;
}

void Server::destroy()
{
	destroySockets();
	destroyEpoll();
}

/**
 * @brief Sets 
 * 
 * @param FD 
 */
void Server::setNonBlocking(const int &FD)
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

// Check if this FD is a listening socket (means a new client is connecting)
bool Server::isListeningSocket(const int &FD)
{
	for (const int &socketFD : listeningSockets)
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
void Server::addClient(const epoll_event &event)
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
				break;
			}
			throw(std::runtime_error("Failed to accept client connection."));
		}

		// Check to see if client was added
		// std::cout << "Accepted client FD: " << clientFD 
        //           << " from " << inet_ntoa(address.sin_addr)
        //           << ":" << ntohs(address.sin_port) << std::endl;
		
		clients.push_back(clientFD);
		setNonBlocking(clientFD);

		// Map/remember which server this client is connected to
		for (size_t i = 0; i < listeningSockets.size(); i++)
		{
			if (listeningSockets[i] == event.data.fd)
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
	}
}

void Server::removeClient(const int &clientFD)
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
		logColored(ERR, "Failed to close client FD: " + std::to_string(clientFD) + ".", RED);
	}

	clients.erase(clients.begin() + index);
	clientBuffers.erase(clientFD);  // Clean up incomplete request buffer
	clientToServer.erase(clientFD); // Remove stored mapping of client to server
	pendingWrites.erase(clientFD);  // Clean up any unsent response data
	writeOffsets.erase(clientFD);
	closeAfterWrite.erase(clientFD);

	logColored("Removed FD: " + std::to_string(clientFD), CYAN);
}

const ServerParse* Server::findServerForClient(int clientFD) const
{
	std::map<int, size_t>::const_iterator it = clientToServer.find(clientFD);
	if (it == clientToServer.end())
		return NULL;
	return &_servers[it->second];
}

bool Server::setClientReadEvents(int clientFD)
{
	epoll_event modEv{};
	modEv.events = EPOLLIN;
	modEv.data.fd = clientFD;
	if (epoll_ctl(epollFD, EPOLL_CTL_MOD, clientFD, &modEv) < 0)
	{
		logColored(ERR, "Failed to re-register EPOLLIN for client: " + std::to_string(clientFD), RED);
		return false;
	}
	return true;
}

bool Server::handleRequestParseError(int clientFD, const HTTPRequest::HTTPRequestException& exc)
{
	// Map parser errors to the most appropriate HTTP status.
	std::string excMsg = exc.what();
	bool isPayloadTooLarge =
		strContains(excMsg, "Content-Length exceeds maximum allowed size") ||
		strContains(excMsg, "Body size exceeds maximum limit");
	bool isLengthRequired =
		strContains(excMsg, "Missing required Content-Length header for method") ||
		strContains(excMsg, "Invalid Content-Length header value");
	HTTPState errorState = HTTPState::BadRequest;
	if (isPayloadTooLarge)
		errorState = HTTPState::RequestTooLarge;
	else if (isLengthRequired)
		errorState = HTTPState::LengthRequired;
	HTTPMessage statusMessage = HTTPCommon::HTTPStatusMap.at(errorState);
	int statusCodeInt = static_cast<int>(errorState);

	// Find server config for client-specific custom error pages.
	const ServerParse* parserErrorServer = findServerForClient(clientFD);

	// Skip noisy logging for common disconnect/malformed formatting paths.
	if (!strContains(excMsg, "Empty (raw) request string") &&
	    !strContains(excMsg, "Invalid request format"))
	{
		logColored(ERR, "Failed to parse HTTP request: " + excMsg, RED);
	}

	// Build parser error response body from configured error page if available.
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

	// Queue a close-delimited parser error response so non-blocking writes can finish safely.
	std::string response =
		"HTTP/1.1 " + statusMessage.code + " " + statusMessage.message + "\r\nContent-Type: " + contentType +
		"\r\nContent-Length: " + std::to_string(body.size()) +
		"\r\nConnection: close\r\n\r\n" + body;
	queueCloseResponse(clientFD, response);
	return true;
}

void Server::handleClientReadEvent(int clientFD)
{
	// 4.1) --- Read Request ---
	std::vector<char> data = readClient(clientFD);
	if (data.size() == 0)
	{
		// Empty data means: incomplete request, EOF, or already removed.
		return;
	}

	std::string rawRequest(data.data(), data.size());
	if (rawRequest.empty())
	{
		// Safety check: skip if request is empty (closed during keep-alive).
		return;
	}

	logColored(
		"Read FD: " + std::to_string(clientFD) +
		" (data size: " + std::to_string(data.size()) + " bytes)",
		BOLDYELLOW);

	// 4.2) --- Parse Request ---
	HTTPRequest request(this);
	try
	{
		request.parseRequest(rawRequest);
		request.printRequest();
		std::cout << RESET << std::endl;
	}
	catch (const HTTPRequest::HTTPRequestException &exc)
	{
		handleRequestParseError(clientFD, exc);
		return;
	}

	// 4.3) --- Find corresponding server config for this client FD ---
	const ServerParse* serverPtr = findServerForClient(clientFD);
	if (!serverPtr)
	{
		logColored(ERR, "Failed to find server for client FD: " + std::to_string(clientFD), RED);
		removeClient(clientFD);
		return;
	}

	// 4.4) --- CGI routing ---
	std::string filePath;
	const LocationParse* loc = NULL;
	if (isCGIRequest(request, *serverPtr, filePath, loc))
	{
		HTTPState cgiAccessState = checkCGIAccess(filePath);
		// Access wasn't good, return error page.
		if (cgiAccessState != HTTPState::Ok)
		{
			HTTPResponse response(*serverPtr);
			std::string responseStr = response.buildErrorResponse(request, cgiAccessState);
			queueResponse(clientFD, request, responseStr);
			return;
		}

		// Access was OK, handle CGI.
		logColored("Handling CGI request for: " + filePath, CYAN);
		startCGI(clientFD, request, *serverPtr, filePath, *loc);
		return;
	}

	// 4.5) --- Normal non-CGI Response ---
	HTTPResponse response(*serverPtr);
	std::string responseStr = response.buildResponse(request);
	queueResponse(clientFD, request, responseStr);

	// 4.6) --- If client wants to close, mark close-after-write ---
	if (request.headers.count("Connection") && request.headers["Connection"] == "close")
	{
		closeAfterWrite[clientFD] = true;
		logColored("Client requested Connection: close. Will close after response is sent.", CYAN);
	}
}

void Server::handleClientWriteEvent(int clientFD)
{
	if (!pendingWrites.count(clientFD))
	{
		// Nothing to send: switch this fd back to read monitoring.
		if (!setClientReadEvents(clientFD))
			removeClient(clientFD);
		return;
	}

	const std::string& data = pendingWrites[clientFD];
	size_t& offset = writeOffsets[clientFD];
	bool writeError = false;

	while (offset < data.size())
	{
		ssize_t sent = write(clientFD, data.c_str() + offset, data.size() - offset);
		if (sent < 0)
		{
			if (errno == EAGAIN || errno == EWOULDBLOCK)
				// Kernel buffer full — EPOLLOUT will fire again.
				break;
			logColored(ERR,
				"Write error for client FD: " + std::to_string(clientFD) +
				" | errno: " + std::to_string(errno) +
				" (" + std::string(std::strerror(errno)) + ")",
				RED);
			removeClient(clientFD);
			writeError = true;
			break;
		}
		offset += static_cast<size_t>(sent);
		logColored(
			"Bytes written: " + std::to_string(sent) +
			" | Total sent: " + std::to_string(offset) +
			" / " + std::to_string(data.size()),
			GREEN);
	}

	if (!writeError && offset >= data.size())
	{
		// All data sent — clean up and decide whether to keep alive.
		bool shouldClose = closeAfterWrite.count(clientFD) && closeAfterWrite[clientFD];
		pendingWrites.erase(clientFD);
		writeOffsets.erase(clientFD);
		closeAfterWrite.erase(clientFD);

		if (shouldClose)
		{
			removeClient(clientFD);
		}
		else if (!setClientReadEvents(clientFD))
		{
			removeClient(clientFD);
		}
	}
}

// Accumulates request data from a client until a complete request is available.
// Returns empty vector if incomplete, full request vector if complete.
std::vector<char> Server::readClient(const int &FD)
{
	std::vector<char> tempBuffer(READ_BUFFER_SIZE);
	std::vector<char> result;
	
	// Read new data from socket
	ssize_t bytesRead = read(FD, tempBuffer.data(), READ_BUFFER_SIZE);
	
	if (bytesRead < 0)
	{
		if (errno == EAGAIN || errno == EWOULDBLOCK)
			return result;
		else
		{
			// Read error - remove this client instead of crashing the server
			logColored(ERR,
				"Read error on FD " + std::to_string(FD) + ": " + std::string(strerror(errno)),
				RED);
			clientBuffers.erase(FD);
			removeClient(FD);
			return result;
		}
	}
	else if (bytesRead == 0)
	{
		// EOF - client closed connection
		clientBuffers.erase(FD);
		removeClient(FD);
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
		// Found headers end, check Content-Length (case-insensitive search in raw buffer)
		std::string dataLower = data_str.substr(0, headersEnd);
		std::transform(dataLower.begin(), dataLower.end(), dataLower.begin(), ::tolower);
		size_t contentLengthPos = dataLower.find("content-length:");
		if (contentLengthPos != std::string::npos)
		{
			contentLengthPos += 15;  // strlen("content-length:")
			// Skip whitespace
			while (contentLengthPos < data_str.size() && 
			       (data_str[contentLengthPos] == ' ' || data_str[contentLengthPos] == '\t'))
			{
				contentLengthPos++;
			}
			// Extract the number — only digits, then parse safely
			size_t endPos = contentLengthPos;
			while (endPos < data_str.size() && std::isdigit(data_str[endPos]))
			{
				endPos++;
			}
			if (endPos == contentLengthPos)
			{
				// No digits found — malformed header, treat as no body
				result = std::vector<char>(data_str.begin(), data_str.end());
				clientBuffers.erase(FD);
				return result;
			}
			ssize_t contentLength;
			try {
				contentLength = std::stoll(data_str.substr(contentLengthPos, endPos - contentLengthPos));
			} catch (...) {
				// Malformed Content-Length: treat as no body
				result = std::vector<char>(data_str.begin(), data_str.end());
				clientBuffers.erase(FD);
				return result;
			}
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
 * @brief Main event loop of the server.
 *
 * @details
 * - Uses epoll to wait for activity on any FD.
 *     → Listening socket → accept new clients
 *     → Client socket:
 *         - EPOLLIN  → read request, parse it, build response
 *         - EPOLLOUT → send response (may require multiple writes)
 *     → CGI pipe → read CGI output
 *     → Error → remove client
 *
 * - Uses non-blocking sockets, so responses may be sent in parts.
 */
void Server::start()
{
	if (epollFD < 0)
	{
		logColored(ERR, "Server failed to initialize. Aborting.", RED);
		return;
	}
	logColored("--- Welcome to Webserv ---", CYAN);
	logColored("--- Server Side ---", CYAN);
	running = 1;

	// Buffer for epoll events added by epoll_wait
	epoll_event events[_maxEvents];

	while (running)
	{
		/// --- Wait for events on registered FDs ---
		// epoll_wait() will block and not return until 
		// at least one file descriptor is ready, or an error occurs. 
		// This is useful when we want the program to be event-driven and only proceed 
		// when there is actual activity, without polling or using a fixed timeout.
		int count = epoll_wait(epollFD, events, _maxEvents, INDEFINITE_BLOCKING);
		if (count < 0) 
		{
			if (errno == EINTR)
				continue; // interrupted by signal (e.g. SIGCHLD) — not an error
			logColored(ERR, "epoll_wait failed: " + std::string(strerror(errno)), RED);
			break;
		}
		
		// --- Loop through all triggered events ---
		for (int i = 0; i < count; i++)
		{
			int fd = events[i].data.fd;
			uint32_t ev = events[i].events;

			// 1) --- New Client Connection ---
			if (isListeningSocket(fd))	// Accept new connection + add client
			{
				if (clients.size() < MAX_CLIENTS)
				{
					addClient(events[i]);
				}
				else
				{
					// If too many clients: accept and immediately close
					// to clear the pending connection queue (otherwise EPOLLIN keeps firing)
					int tempFD = accept(events[i].data.fd, NULL, NULL);
					if (tempFD >= 0)
						close(tempFD);
					logColored(ERR, "Too many clients connected. Rejected new connection.", RED);
				}
			}

			// 2) --- CGI Pipe Events ---
			else if (cgiProcesses.count(fd))
			{
				handleCGIEvent(fd, ev);
			}

			// 3)--- Socket Error or Disconnect ---
			else if (ev & (EPOLLERR | EPOLLHUP | EPOLLRDHUP))	// If it's an error, remove Client
			{
				removeClient(events[i].data.fd);
			}

			// 4) --- Regular Client Request ---
			else if (ev & EPOLLIN)
			{
				handleClientReadEvent(fd);
			}

			// 5) --- Drain Pending Write ---
			else if (ev & EPOLLOUT)
			{
				handleClientWriteEvent(fd);
			}

		}
	}

	running = false;

	destroy();
}

volatile sig_atomic_t Server::running = 0;