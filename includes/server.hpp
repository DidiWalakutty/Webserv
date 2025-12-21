#include <sys/socket.h>
#include <sys/epoll.h>
#include <vector>
#include "HTTPResponse.hpp"
#include "HTTPCommon.hpp"
#include "HTTPRequest.hpp"

#pragma once

/**
 * @file server.hpp
 * @brief Server configuration and creation using sockets and epoll.
 *
 * @details
 * Provides configuration structures for sockets and web servers,
 * as well as the @ref Server class for creating, configuring and managing a web server using Epoll.
 */

/** @brief Configuration for the server sockets. */
struct SocketConfig
{
	int domain = AF_INET;					/**< @brief Specifies what kind of addresses or network the server will use. */
	int type = SOCK_STREAM | SOCK_NONBLOCK; /**< @brief Specifies what kind of communication type the server will use. */
	int protocol = 0;						/**< @brief Specifies what kind of protocol to use within the domain. A value of 0 will choose a protocol automatically. */
};

/** @brief Configuration for the server. */
struct ServerConfig
{
	int maxEvents = 64; /**< @brief Specifies the maximum number of events that the server will poll for at a time. */

	SocketConfig socketConfig{}; /**< @brief Specifies how the server's sockets will work. */

	std::vector<int> ports = {8080}; /**< @brief Specifies on which ports the server will listen to. */
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
	ServerConfig config{};

	int epollFD = -1;				/**< @brief Contains the file descriptor of the Epoll instance. */
	std::vector<int> serverSockets; /**< @brief Contains the server's socket file descriptors. */

	std::vector<int> clients; /**< @brief Contains the client file descriptors that are connected to the server. */

	void CreateSockets(); /**< @brief Creates and configures the server's sockets. */
	void CreateEpoll();	  /**< @brief Creates and configures the server's Epoll instance. */

	void DestroySockets(); /**< @brief Destroys and closes the server's sockets. */
	void DestroyEpoll();   /**< @brief Destroys and closes the server's Epoll instance. */

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
	bool IsServerSocket(const int &FD);

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
	 * @param size The maximum size in bytes to read.
	 * @return A buffer containing the data read from the client.
	 */
	std::vector<char> ReadClient(const int &FD, const size_t size);

public:
	static bool running; /**< @brief Describes if the server should close or keep running. */

	/**
	 * @brief Initiates and configures the server.
	 * @param serverConfig The configuration for the server.
	 */
	Server(const ServerConfig &serverConfig);

	~Server();

	void Destroy(); /**< @brief Destroys and closes the server. All server and associated resources are cleaned up. */

	/**
	 * @brief Starts the main server loop.
	 * @note This will block the rest of the program until the server is closed again.
	 * @warning Should not be called after the server is destroyed.
	 */
	void Start();
};