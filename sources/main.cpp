#include "Server.hpp"
#include "ConfigParser.hpp"
#include <iostream>

int main(int argc, char **argv)
{
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
			std::cerr<< "Error while parsing config file" << std::endl;
			return 1;
		}

		// Gets vector of Parsed + Validated server configurations
		const std::vector<ServerParse>& servers = parser.getServers();
		
		// Print Servers
		// for (size_t i = 0; i < servers.size(); ++i)
		// 	parser.print_server(servers[i]);


		// Fill constructor and Start webserv
		Server webserv(servers);
		webserv.setMaxRequestSize(parser.getMaxBodySize(servers[0]));	// using first server's max body size as reference for reading
		webserv.start();
	}

	return (0);
}