#include "server.hpp"
#include "ConfigParser.hpp"
#include <iostream>

int main(int argc, char **argv)
{
	// handle signal - Ctrl

	if (argc <= 2)
	{
		ConfigParser	parser;
		std::string		filepath;
		
		if (argc == 1)
			filepath = "config/default.conf";
		else
			filepath = argv[1];
		
		if (!parser.parseConfigFile(filepath))
		{
			std::cout << "Error while parsing config file" << std::endl;
			return 1;
		}

		// Gets parsed + validated server configurations from the .conf file
		// Gives a vector of ServerParse objects
		const std::vector<ServerParse>& servers = parser.getServers();
		
		// Print Servers
		for (size_t i = 0; i < servers.size(); ++i)
			parser.print_server(servers[i]);

		// --- !!! ---
		// Probably need to convert my ServerParse to ServerConfig, which we need to pass to Server
		// But his ServerConfig contains other info my ServerParse doesn't have:
		// maxEvents, SocketConfig and a vector of int ports.
		// -----------

		// --- 1) Start/init the servers + (epoll??)
		// ServerConfig serverConfig{};
		// Server webserv(serverConfig);

		// webserv.Start();

	}

	return (0);
}