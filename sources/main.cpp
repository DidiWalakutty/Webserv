#include "server.hpp"

#include <iostream>

int main()
{
	ServerConfig serverConfig{};
	//serverConfig.ports.push_back(8000);
	
	Server server(serverConfig);

	server.Start();

	return (0);
}