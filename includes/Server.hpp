#pragma once

#include <sys/socket.h>
#include <sys/epoll.h>
#include <vector>
#include <memory>
#include <map>
#include "HTTPResponse.hpp"
#include "HTTPCommon.hpp"
#include "HTTPRequest.hpp"
#include "Config.hpp"
#include "CGI.hpp"

// Reset
#define RESET       "\033[0m"

// Text colors
#define BLACK       "\033[30m"
#define RED         "\033[31m"
#define GREEN       "\033[32m"
#define YELLOW      "\033[33m"
#define BLUE        "\033[34m"
#define MAGENTA     "\033[35m"
#define CYAN        "\033[36m"
#define WHITE       "\033[37m"

/**
 * @file server.hpp
 * @brief Server configuration and creation using sockets and epoll.
 *
 * @details
 * Provides configuration structures for sockets and web servers,
 * as well as the @ref Server class for creating, configuring and managing a web server using Epoll.
 */

struct CGIInfo
{
	std::shared_ptr<CGI> cgi;			// Pointer to CGI struct
	bool pipeIsInput;					// true if pipeToChild (server -> CGI), false if pipeFromChild (CGI -> server)
	int clientFD;						// which client this CGI belongs to
};

class Server
{
	private:
		std::vector<ServerParse> _servers;			/**< @brief Parsed server blocks from config file */
		std::map<int, CGIInfo> cgiProcesses;		/**< @brief Active CGI pipe FDs mapped to their CGI state and owning client. */

		int epollFD = -1;							/**< @brief Epoll instance of the file descriptor */
		std::vector<int> _listeningSockets;			/**< @brief Listening sockets (one per ServerParse) */
		std::vector<int> _clients;					/**< @brief Connected client sockets. */
		std::map<int, size_t> _clientToServer;		/**< @brief Tracks which server each client is connected to */
		std::map<int, std::string> _clientBuffers;	/**< @brief Incomplete request buffers for each client FD. */
		std::map<int, std::string> pendingWrites;	/**< @brief Full response data waiting to be sent, keyed by client FD. */
		std::map<int, size_t> writeOffsets;			/**< @brief Bytes already sent for each pending write, keyed by client FD. */
		std::map<int, bool> closeAfterWrite;		/**< @brief Whether to close the connection after the pending write completes. */
		const int _maxEvents = 64; 					/**< @brief Maximum number of events to process per epoll_wait call. */
		ssize_t _maxRequestSize = 1;				/**< @brief Maximum allowed size for incoming HTTP requests. */
		std::vector<HTTPMethod> _allowedMethods;	/**< @brief Default allowed HTTP methods for the server */
		
		void createSockets();
		void createEpoll();
		void cleanSockets();
		void cleanEpoll();
		void setNonBlocking(const int &FD);
		bool isListeningSocket(const int &FD);
		void addClient(const epoll_event &event);
		std::vector<char> readClient(const int &FD);
		void removeClient(const int &clientFD);
		void queueResponse(int clientFD, const HTTPRequest& request, const std::string& responseStr);
		void queueCloseResponse(int clientFD, const std::string& response);
		const ServerParse* findServerForClient(int clientFD) const;
		bool setClientReadEvents(int clientFD);
		bool handleRequestParseError(int clientFD, const HTTPRequest::HTTPRequestException& exc);
		void handleClientReadEvent(int clientFD);
		void handleClientWriteEvent(int clientFD);
		
		// --- Run CGI ---
		bool isCGIRequest(const HTTPRequest& request, const ServerParse& server, std::string& filePath, const LocationParse*& location);
		void startCGI(int clientFD, const HTTPRequest& request, const ServerParse& server, const std::string& filePath, const LocationParse& location);
		void handleCGIEvent(int fd, uint32_t events);
		void handleCGITimeOut(std::shared_ptr<CGI> cgi);
		void handleCGIError(std::shared_ptr<CGI> cgi);
		void handleCGIWrite(std::shared_ptr<CGI> cgi, int fd, uint32_t events);
		void handleCGIRead(std::shared_ptr<CGI> cgi, int fd, uint32_t events);
		void handleCGIWait(std::shared_ptr<CGI> cgi);
		void handleCGIResponse(std::shared_ptr<CGI> cgi);
		void handleCGIErrorResponse(std::shared_ptr<CGI> cgi);
		void queueCGIResponse(int clientFD, const std::string& response);
		HTTPState checkCGIAccess(const std::string& filePath);
		
	public:
		static volatile sig_atomic_t running; /**< @brief Describes if the server should close or keep running. */
		
		Server(const std::vector<ServerParse>& parsedServerConfigInfos);
		~Server();
		
		void shutDown();
		void setMaxRequestSize(size_t size);
		void start();
		std::vector<HTTPMethod> getAllowedMethods() const;
};