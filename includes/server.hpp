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

/**
 * @brief Web server class.
 *
 * @details
 * Initiates, creates and manages a web server. It uses Epoll for communication and is non-blocking.
 * Handles all needed resources and manages their lifetime and cleanup.
 * Manages client connecting and disconnecting as well as any client errors or inactivity.
 *
 * Typical usage:
 * - Create a server by instantiating it with a @ref ServerConfig.
 * - Start the server by using the @ref Start() function.
 * - Destroy resources with @ref Destroy() when no longer needed. (Also handled automatically.)
 */
class Server
{
	private:
		std::vector<ServerParse> _servers;			/**< @brief Parsed server blocks from config file */
		std::map<int, CGIInfo> cgiProcesses;		/**< @brief Active CGI pipe FDs mapped to their CGI state and owning client. */

		int epollFD = -1;							/**< @brief Epoll instance of the file descriptor */
		std::vector<int> listeningSockets; 			/**< @brief Listening sockets (one per ServerParse) */
		std::vector<int> clients; 					/**< @brief Connected client sockets. */
		std::map<int, size_t> clientToServer;		/**< @brief Tracks which server each client is connected to */
		std::map<int, std::string> clientBuffers;	/**< @brief Incomplete request buffers for each client FD. */
		std::map<int, std::string> pendingWrites;	/**< @brief Full response data waiting to be sent, keyed by client FD. */
		std::map<int, size_t> writeOffsets;			/**< @brief Bytes already sent for each pending write, keyed by client FD. */
		std::map<int, bool> closeAfterWrite;		/**< @brief Whether to close the connection after the pending write completes. */
		const int _maxEvents = 64; 					/**< @brief Maximum number of events to process per epoll_wait call. */
		ssize_t maxRequestSize = 1;					/**< @brief Maximum allowed size for incoming HTTP requests. */
		std::vector<HTTPMethod> allowedMethods; 	/**< @brief Default allowed HTTP methods for the server */
		
		void QueueResponse(int clientFD, const HTTPRequest& request, const std::string& responseStr);
		
		// --- Run CGI ---
		bool IsCGIRequest(const HTTPRequest& request, const ServerParse& server, std::string& filePath, const LocationParse*& location);
		void startCGI(int clientFD, const HTTPRequest& request, const ServerParse& server, const std::string& filePath, const LocationParse& location);
		void handleCGIEvent(int fd, uint32_t events);
		void handleCGITimeOut(std::shared_ptr<CGI> cgi);
		void handleCGIError(std::shared_ptr<CGI> cgi);
		void handleCGIWrite(std::shared_ptr<CGI> cgi, uint32_t events);
		void handleCGIRead(std::shared_ptr<CGI> cgi, uint32_t events);
		void handleCGIWait(std::shared_ptr<CGI> cgi);
		void handleCGIResponse(std::shared_ptr<CGI> cgi);
		void handleCGIErrorResponse(std::shared_ptr<CGI> cgi);
		void queueCGIResponse(int clientFD, const std::string& response);
		HTTPState checkCGIAccess(const std::string& filePath);

	public:
		std::vector<HTTPMethod> getAllowedMethods() const { return allowedMethods; } /**< @brief Getter for allowed HTTP methods. */

	private:
		void CreateSockets();	/**< @brief Creates and configures the server's sockets. */
		void CreateEpoll();		/**< @brief Creates and configures the server's Epoll instance. */

		void DestroySockets();	/**< @brief Destroys and closes the server's sockets. */
		void DestroyEpoll();	/**< @brief Destroys and closes the server's Epoll instance. */


		/**
		 * @brief Configures the file descriptor to be non blocking.
		 * @param FD The file descriptor to configure.
		 */
		void SetNonBlocking(const int &FD);

		/**
		 * @brief Checks if the file descriptor is a server socket.
		 * @param FD The file descriptor to check.
		 * @return True if the file descriptor is a server socket.
		 */
		bool isListeningSocket(const int &FD);

		/**
		 * @brief Adds and configures a new client to the server.
		 * @param event The Epoll request event.
		 */
		void AddClient(const epoll_event &event);

		/**
		 * @brief Removes an existing client from the server.
		 * @param clientFD The file descriptor of the client.
		 */
		void RemoveClient(const int &clientFD);

		/**
		 * @brief Reads data from a client.
		 * @param FD The file descriptor of the client to read from.
		 * @return A buffer containing the data read from the client.
		 */
		std::vector<char> ReadClient(const int &FD);
	
	public:
		static volatile sig_atomic_t running; /**< @brief Describes if the server should close or keep running. */
		
		/**
		 * @brief Initiates and configures the server.
		 * @param ServerParse The configuration for the server.
		 */
		/**< @brief Construct server engine with parsed configs */
		Server(const std::vector<ServerParse>& serverConfigs);
		
		~Server();
		
		void Destroy(); /**< @brief Destroys and closes the server. All server and associated resources are cleaned up. */
		void setMaxRequestSize(size_t size);

		/**
		 * @brief Starts the main server loop.
		 * @note This will block the rest of the program until the server is closed again.
		 * @warning Should not be called after the server is destroyed.
		 */
		void Start();
};