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

volatile sig_atomic_t Server::running = 0;
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

	void Interrupt(int sig)
	{
		if (sig == SIGINT)
		{
			Server::running = 0;
		}
	}
}

/**
 * @brief Construct the server engine with parsed configs.
 */
Server::Server(const std::vector<ServerParse>& parsedServerConfigInfos)
	: _servers(parsedServerConfigInfos), epollFD(-1)
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
		shutDown();
	}
}

/**
 * @brief Destroy the server and release all owned resources.
 */
Server::~Server()
{
	logColored("Closing server.", CYAN);

	shutDown();
}



/**
 * @brief Creates and configures the server's listening sockets.
 */
void Server::createSockets()
{
	cleanSockets();

	if (!_listeningSockets.empty())
	{
		throw(std::runtime_error("Server sockets still exists."));
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
		logColored("Created socket fd " + std::to_string(socketFD) + " for server " + server.host + ":" + std::to_string(server.port), GREEN);

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

		_listeningSockets.push_back(socketFD);
	}
}

/**
 * @brief Set the maximum allowed request size.
 */
void Server::setMaxRequestSize(size_t size)
{
	_maxRequestSize = size;
}

/**
 * @brief Create the epoll instance and register listening sockets.
 */
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

	for (const int &socketFD : _listeningSockets)
	{
		epoll_event event{};
		event.events = EPOLLIN;
		event.data.fd = socketFD;

		if (epoll_ctl(epollFD, EPOLL_CTL_ADD, socketFD, &event) < 0)
		{
			throw(std::runtime_error("Failed to add server socket to epoll."));
		}
		logColored("Socket fd " + std::to_string(socketFD) + " is added inside the epoll interface " + std::to_string(epollFD), GREEN);
	}
}

/**
 * @brief Return the server's allowed HTTP methods.
 */
std::vector<HTTPMethod> Server::getAllowedMethods() const
{
	return _allowedMethods;
}

/**
 * @brief Close and clear all listening sockets.
 */
void Server::cleanSockets()
{
	for (int &socketFD : _listeningSockets)
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

	_listeningSockets.clear();
}

/**
 * @brief Close the epoll instance and any tracked client sockets.
 */
void Server::cleanEpoll()
{
	for (size_t i = 0; i < _clients.size(); i++)
	{
		if (_clients[i] >= 0)
		{
			if (close(_clients[i]) < 0)
			{
				logColored(ERR, "Failed to close client FD: " + std::to_string(_clients[i]) + ".", RED);
			}
		}
	}

	_clients.clear();

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

/**
 * @brief Shut down the server and release all owned file descriptors.
 */
void Server::shutDown()
{
	cleanSockets();
	cleanEpoll();
}

/**
 * @brief Configure a file descriptor for non-blocking I/O.
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

/**
 * @brief Check whether the file descriptor belongs to a listening socket.
 */
bool Server::isListeningSocket(const int &FD)
{
	for (const int &socketFD : _listeningSockets)
	{
		if (socketFD == FD && socketFD >= 0)
		{
			return (true);
		}
	}
	return (false);
}

/**
 * @brief Accept new client connections and register them in epoll.
 */
void Server::addClient(const epoll_event &event)
{
	while (true)
	{
		sockaddr_in address{};
		socklen_t length = sizeof(address);

		// The event here is for what epoll told us happened.
		int clientFD = accept(event.data.fd, (sockaddr *)&address, &length);
		if (clientFD < 0)
		{
			if (errno == EAGAIN || errno == EWOULDBLOCK)
			{
				// There are no more pending connections to accept, we have accepted them all.
				break;
			}
			throw(std::runtime_error("Failed to accept client connection."));
		}

		logColored("Accepted client FD: " + std::to_string(clientFD) + " from " + inet_ntoa(address.sin_addr) + ":" + std::to_string(ntohs(address.sin_port)), GREEN);
		
		_clients.push_back(clientFD);
		setNonBlocking(clientFD);

		// Map/remember which server this client is connected to
		for (size_t i = 0; i < _listeningSockets.size(); i++)
		{
			if (_listeningSockets[i] == event.data.fd)
			{
				_clientToServer[clientFD] = i;
				break;
			}
		}

		// The new event for what we want epoll to watch for this new client.
		epoll_event newClientEvent{};
		newClientEvent.events = EPOLLIN;
		newClientEvent.data.fd = clientFD;

		if (epoll_ctl(epollFD, EPOLL_CTL_ADD, clientFD, &newClientEvent) < 0)
		{
			throw(std::runtime_error("Failed to add client socket to epoll."));
		}
		logColored("Client FD " + std::to_string(clientFD) + " is added inside the epoll interface " + std::to_string(epollFD), GREEN);
	}
}

/**
 * @brief Remove a client and clean up all per-client state.
 */
void Server::removeClient(const int &clientFD)
{
	if (clientFD < 0)
	{
		return;
	}

	int index = -1;
	for (size_t i = 0; i < _clients.size(); i++)
	{
		if (_clients[i] == clientFD)
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

	_clients.erase(_clients.begin() + index);
	_clientBuffers.erase(clientFD);  // Clean up incomplete request buffer
	_clientToServer.erase(clientFD); // Remove stored mapping of client to server
	pendingWrites.erase(clientFD);  // Clean up any unsent response data
	writeOffsets.erase(clientFD);
	closeAfterWrite.erase(clientFD);

	logColored("Removed FD: " + std::to_string(clientFD), CYAN);
}

const ServerParse* Server::findServerForClient(int clientFD) const
{
	std::map<int, size_t>::const_iterator it = _clientToServer.find(clientFD);
	if (it == _clientToServer.end())
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
		if (sent == 0)
		{
			logColored(ERR, "Write returned 0 for client FD: " + std::to_string(clientFD), RED);
			removeClient(clientFD);
			writeError = true;
			break;
		}
		else if (sent < 0)
		{
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

// Returns -1 if Content-Length is absent, -2 if malformed, otherwise the parsed value.
static ssize_t parseContentLength(const std::string& raw, size_t headersEnd)
{
	std::string headerBlock = raw.substr(0, headersEnd);
	std::transform(headerBlock.begin(), headerBlock.end(), headerBlock.begin(), ::tolower);

	size_t pos = headerBlock.find("content-length:");
	if (pos == std::string::npos)
		return -1; // not present

	pos += 15; // skip "content-length:"
	while (pos < raw.size() && (raw[pos] == ' ' || raw[pos] == '\t'))
		pos++;

	size_t end = pos;
	while (end < raw.size() && std::isdigit(raw[end]))
		end++;

	if (end == pos)
		return -2; // no digits — malformed

	try {
		return std::stoll(raw.substr(pos, end - pos));
	} catch (...) {
		return -2; // overflow or garbage — malformed
	}
}

/**
 * @brief Accumulate request bytes until a complete HTTP request is available.
 */
std::vector<char> Server::readClient(const int &FD)
{
	std::vector<char> tempBuffer(READ_BUFFER_SIZE);
	std::vector<char> result;

	// --- 1. Read raw bytes from the socket ---
	ssize_t bytesRead = read(FD, tempBuffer.data(), READ_BUFFER_SIZE);
	if (bytesRead < 0)
	{
		logColored(ERR, "Read error on FD " + std::to_string(FD), RED);
		_clientBuffers.erase(FD);
		removeClient(FD);
		return result;
	}
	if (bytesRead == 0)
	{
		// EOF — client closed the connection
		_clientBuffers.erase(FD);
		removeClient(FD);
		return result;
	}

	// --- 2. Append to the per-client accumulation buffer ---
	_clientBuffers[FD].append(tempBuffer.data(), bytesRead);
	std::string& buffer = _clientBuffers[FD];

	// --- 3. Wait until we have a complete header block ---
	size_t headersEnd = buffer.find("\r\n\r\n");
	if (headersEnd == std::string::npos)
		return result; // headers still incomplete, keep accumulating

	// --- 4. If there is a body, wait until it is fully buffered ---
	ssize_t contentLength = parseContentLength(buffer, headersEnd);
	if (contentLength == -2)
	{
		// Malformed Content-Length — pass the data upstream and let the parser error
		result.assign(buffer.begin(), buffer.end());
		_clientBuffers.erase(FD);
		return result;
	}
	if (contentLength > 0)
	{
		size_t bodyReceived = buffer.size() - (headersEnd + 4);
		if (bodyReceived < static_cast<size_t>(contentLength))
			return result; // body still incomplete, keep accumulating
	}

	// --- 5. Complete request — hand it off and clear the buffer ---
	result.assign(buffer.begin(), buffer.end());
	_clientBuffers.erase(FD);
	return result;
}

/**
 * @brief Main event loop for the server.
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
				if (_clients.size() < MAX_CLIENTS)
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

	shutDown();
}


HTTPState Server::checkCGIAccess(const std::string& filePath)
{
	struct stat st;

	// --- 1. Check if file exists ---
	if (access(filePath.c_str(), F_OK) != 0)
		return HTTPState::NotFound;

	// --- 2. Get file info ---
	if (stat(filePath.c_str(), &st) != 0)
		return HTTPState::InternalServerError;

	// --- 3. Reject directories ---
	if (S_ISDIR(st.st_mode))
		return HTTPState::Forbidden;
		
	// --- 4. Check read permission ---
	if (access(filePath.c_str(), R_OK) != 0)
		return HTTPState::Forbidden;

	// --- 5. Check execute permission ---
	if (access(filePath.c_str(), X_OK) != 0)
		return HTTPState::Forbidden;

	// Needed??? --- 6. Prevents checking sockets, pipes etc
	if (!S_ISREG(st.st_mode))
		return HTTPState::Forbidden;
		
	return HTTPState::Ok;
}

bool Server::isCGIRequest(const HTTPRequest& request, const ServerParse& server,
                          std::string& filePath, const LocationParse*& location)
{
	// --- Only GET and POST are considered CGI requests ---
	if (HTTPMethod::GET != request.method && HTTPMethod::POST != request.method)
		return false;
		
	// --- 1. Find matching location ---
	location = server.get_best_location(request.resourcePath);
	if (!location)
		return false;

	// --- 2. Must be marked as CGI ---
	if (!location->is_cgi)
		return false;

	// --- 3. Build filesystem path ---
	filePath = server.build_filesystem_path(request.resourcePath);
	if (filePath.empty())
		return false;

	// --- 4. Check extension ---
	size_t dot = filePath.find_last_of('.');
	if (dot == std::string::npos)
		return false;

	std::string ext = filePath.substr(dot);
	if (ext != location->cgi_extension)
		return false;

	return true;
}

void Server::queueResponse(int clientFD, const HTTPRequest& request, const std::string& responseStr)
{
	auto connIt = request.headers.find("CONNECTION");
	bool clientWantsClose = (connIt != request.headers.end() &&
	                         connIt->second.find("close") != std::string::npos);
	bool http10 = (request.protocolVersion == HTTPProtocolVersion::HTTP_1_0);
	closeAfterWrite[clientFD] = (clientWantsClose || http10);

	pendingWrites[clientFD] = responseStr;
	writeOffsets[clientFD] = 0;

	epoll_event writeEv{};
	writeEv.events = EPOLLOUT;
	writeEv.data.fd = clientFD;
	if (epoll_ctl(epollFD, EPOLL_CTL_MOD, clientFD, &writeEv) < 0)
	{
		std::cerr << "Failed to register EPOLLOUT for client: " << clientFD << std::endl;
		removeClient(clientFD);
	}
}

void Server::queueCloseResponse(int clientFD, const std::string& response)
{
	epoll_event writeEv{};

	pendingWrites[clientFD] = response;
	writeOffsets[clientFD] = 0;
	closeAfterWrite[clientFD] = true;
	writeEv.events = EPOLLOUT;
	writeEv.data.fd = clientFD;
	if (epoll_ctl(epollFD, EPOLL_CTL_MOD, clientFD, &writeEv) < 0)
	{
		std::cerr << "Failed to register EPOLLOUT for client: " << clientFD << std::endl;
		removeClient(clientFD);
	}
}