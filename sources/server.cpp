#include "server.hpp"

#include <iostream>
#include <stdexcept>
#include <netinet/in.h>
#include <unistd.h>
#include <fcntl.h>
#include <signal.h>
#include <string>

void Interrupt(int sig)
{
	if (sig == SIGINT) {Server::running = false;}
}

Server::Server(const ServerConfig& serverConfig)
{
	config = serverConfig;

	signal(SIGINT, Interrupt);

	try
	{
		CreateSockets();
		CreateEpoll();
	}
	catch(const std::exception& e)
	{
		std::cerr << "Failed to create server: " << e.what() << std::endl;

		Destroy();
	}
}

Server::~Server()
{
	std::cout << std::endl << "Closing server." << std::endl;

	Destroy();
}

void Server::CreateSockets()
{
	DestroySockets();

	if (serverSockets.size() != 0) {throw (std::runtime_error("Server sockets already exists."));}

	for (const int& port : config.ports)
	{
		int socketFD = socket(config.socketConfig.domain, config.socketConfig.type, config.socketConfig.protocol);

		if (socketFD < 0) {throw (std::runtime_error("Failed to create server socket."));}

		serverSockets.push_back(socketFD);

		int opt = 1;
		if (setsockopt(socketFD, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt)) < 0)
			{throw (std::runtime_error("Failed to set socket option."));}

		sockaddr_in address{};
		address.sin_family = config.socketConfig.domain;
		address.sin_port = htons(port);
		address.sin_addr.s_addr = INADDR_ANY;

		if (bind(socketFD, (sockaddr*)&address, sizeof(address)) < 0)
			{throw (std::runtime_error("Failed to bind server socket."));}

		if (listen(socketFD, SOMAXCONN) < 0) {throw (std::runtime_error("Failed to listen on server socket."));}

		SetNonBlocking(socketFD);
	}
}

void Server::CreateEpoll()
{
	if (epollFD >= 0) {throw (std::runtime_error("Epoll instance already exists."));}

	epollFD = epoll_create(1);

	if (epollFD < 0) {throw (std::runtime_error("Failed to create epoll instance."));}

	for (const int& socketFD : serverSockets)
	{
		epoll_event event{};
		event.events = EPOLLIN;
		event.data.fd = socketFD;

		if (epoll_ctl(epollFD, EPOLL_CTL_ADD, socketFD, &event) < 0)
			{throw (std::runtime_error("Failed to add server socket to epoll."));}
	}
}

void Server::DestroySockets()
{
	for (int& socketFD : serverSockets)
	{
		if (socketFD < 0) {continue;}

		if (close(socketFD) < 0) {std::cerr << "Failed to close server socket." << std::endl;}

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
			if (close(clients[i]) < 0) {std::cerr << "Failed to close client FD: " << clients[i] << "." << std::endl;}
		}
	}

	clients.clear();

	if (epollFD < 0) {return;}

	if (close(epollFD) < 0) {std::cerr << "Failed to close epoll instance." << std::endl;}

	epollFD = -1;
}

void Server::Destroy()
{
	DestroySockets();
	DestroyEpoll();
}

void Server::SetNonBlocking(const int& FD)
{
	int flags = fcntl(FD, F_GETFL, 0); // Remove 0

	if (flags < 0) {throw (std::runtime_error("Failed to retrieve FD flags."));}

	if (fcntl(FD, F_SETFL, flags | O_NONBLOCK) < 0) {throw (std::runtime_error("Failed to set FD flags."));}
}

bool Server::IsServerSocket(const int& FD)
{
	for (const int& socketFD : serverSockets) {if (socketFD == FD && socketFD >= 0) {return (true);}}

	return (false);
}

void Server::AddClient(const epoll_event& event)
{
	while (true)
	{
		sockaddr_in address{};
		socklen_t length = sizeof(in_addr);

		int clientFD = accept(event.data.fd, (sockaddr*)&address, &length);

		if (clientFD < 0)
		{
			if (errno == EAGAIN || errno == EWOULDBLOCK) {break;}
			
			throw (std::runtime_error("Failed to accept client connection."));
		}

		clients.push_back(clientFD);

		SetNonBlocking(clientFD);

		epoll_event event{};
		event.events = EPOLLIN;
		event.data.fd = clientFD;

		if (epoll_ctl(epollFD, EPOLL_CTL_ADD, clientFD, &event) < 0)
			{throw (std::runtime_error("Failed to add client socket to epoll."));}

		std::cout << std::endl << "Added FD: " << event.data.fd << std::endl;
	}
}

void Server::RemoveClient(const int& clientFD)
{
	if (clientFD < 0) {return;}

	int index = -1;
	for (size_t i = 0; i < clients.size(); i++)
	{
		if (clients[i] == clientFD)
		{
			index = (int)i;
			break;
		}
	}

	if (index < 0) {return;}

	if (close(clientFD) < 0) {std::cerr << "Failed to close client FD: " << clientFD << "." << std::endl;}

	clients[index] = -1;

	std::cout << std::endl << "Removed FD: " << clientFD << std::endl;
}

std::vector<char> Server::ReadClient(const int& FD, const size_t size)
{
	std::vector<char> result;
	result.resize(size);

	ssize_t readSize = read(FD, result.data(), size);

	if (readSize == 0)
	{
		RemoveClient(FD);
		result.clear();
	}

	if (readSize < 0 && errno != EAGAIN) {throw (std::runtime_error("Failed to read client."));}

	return (result);
}

void Server::Start()
{
	running = true;

	epoll_event events[config.maxEvents];

	while (running)
	{
		int count = epoll_wait(epollFD, events, config.maxEvents, -1);
		if (count < 0) {break;}

		for (int i = 0; i < count; i++)
		{
			if (IsServerSocket(events[i].data.fd))
			{
				AddClient(events[i]);
				continue;
			}

			if (events[i].events & (EPOLLERR | EPOLLHUP | EPOLLRDHUP))
			{
				RemoveClient(events[i].data.fd);
				continue;
			}

			if (events[i].events & EPOLLIN)
			{
				std::vector<char> data = ReadClient(events[i].data.fd, 512);
				if (data.size() > 0) 
					std::cout << std::endl << "Read FD: " << events[i].data.fd << std::endl << std::string(data.data()) << std::endl;

				MessageType messageType = getMessageType(data);				
				if (messageType == MessageType::HTTPRequest)
				{
					HTTPRequest request;
					try
					{
						request.parseRequest(std::string(data.data(), data.size()));
						request.printRequest();
					}
					catch (const HTTPRequest::HTTPRequestException& exc)
					{
						std::cerr << "Failed to parse HTTP request: " << exc.what() << std::endl;
					}
				}
				else if (messageType == MessageType::HTTPResponse)
				{
					HTTPResponse response;
					try
					{
						response.parseResponse(std::string(data.data(), data.size()));
						response.printResponse();
					}
					catch (const HTTPResponse::HTTPResponseException& exc)
					{
						std::cerr << "Failed to parse HTTP response: " << exc.what() << std::endl;
					}
				}
				continue;
			}
		}
	}
	
	running = false;

	Destroy();
}

bool Server::running = false;