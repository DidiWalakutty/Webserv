#include <iostream>
#include <string>
#include <vector>
#include <sstream>
#include "../includes/Config.hpp"
#include "../includes/ConfigParser.hpp" // whatever contains your ConfigParser class

std::vector<std::string> ConfigParser::splitByWhitespace(const std::string& str) const
{
    std::vector<std::string> tokens;
    std::string token;
    std::istringstream iss(str);
    while (iss >> token)
        tokens.push_back(token);
    return tokens;
}

bool ConfigParser::validateServerConfig(const ServerConfig& server) const
{
    // Minimal validation: host, port, root, index
    if (server.host.empty() || server.port == 0 || server.root.empty() || server.index.empty())
        return false;

    // Validate locations
    for (const auto& loc : server.locations)
    {
        if (loc.path.empty())
            return false;
    }

    return true;
}


std::optional<HTTPMethod> ConfigParser::stringToHTTPMethod(const std::string& str) const
{
    if (str == "GET") return HTTPMethod::GET;
    if (str == "POST") return HTTPMethod::POST;
    if (str == "DELETE") return HTTPMethod::DELETE;
    return std::nullopt; // invalid method
}


void printHTTPMethods(const std::vector<HTTPMethod>& methods)
{
    for (size_t i = 0; i < methods.size(); ++i)
    {
        switch (methods[i])
        {
            case HTTPMethod::GET: std::cout << "GET"; break;
            case HTTPMethod::POST: std::cout << "POST"; break;
            case HTTPMethod::DELETE: std::cout << "DELETE"; break;
        }
        if (i != methods.size() - 1)
            std::cout << ", ";
    }
}

void printLocation(const LocationConfig& loc)
{
    std::cout << "  Location Path: " << loc.path << "\n";
    std::cout << "    Root: " << loc.root << "\n";
    std::cout << "    Index: " << loc.index << "\n";
    std::cout << "    Autoindex: " << (loc.autoIndex ? "on" : "off") << "\n";
    std::cout << "    Allowed Methods: ";
    printHTTPMethods(loc.allowedMethods);
    std::cout << "\n";
    if (loc.redirect.statusCode != 0)
        std::cout << "    Redirect: " << loc.redirect.statusCode << " -> " << loc.redirect.targetURL << "\n";
    std::cout << "\n";
}

void printServer(const ServerConfig& server)
{
    std::cout << "Server Name: " << server.serverName << "\n";
    std::cout << "Host: " << server.host << "\n";
    std::cout << "Port: " << server.port << "\n";
    std::cout << "Root: " << server.root << "\n";
    std::cout << "Index: " << server.index << "\n";
    std::cout << "Max Body Size: " << server.maxBodySize << "\n";

    std::cout << "Allowed Methods: ";
    printHTTPMethods(server.allowedMethods);
    std::cout << "\n";

    std::cout << "Error Pages:\n";
    for (std::map<int,std::string>::const_iterator it = server.errorPages.begin(); it != server.errorPages.end(); ++it)
    {
        std::cout << "  " << it->first << " -> " << it->second << "\n";
    }

    std::cout << "\nLocations:\n";
    for (size_t i = 0; i < server.locations.size(); ++i)
    {
        printLocation(server.locations[i]);
    }
    std::cout << "-------------------------------------\n";
}

int main()
{
    ConfigParser parser;
    std::string file = "../config/default.conf"; // your config file

    if (!parser.parseConfigFile(file))
    {
        std::cerr << "Failed to parse config file.\n";
        return 1;
    }

    std::vector<ServerConfig> servers = parser.getServers(); // or _servers depending on your class
    for (size_t i = 0; i < servers.size(); ++i)
    {
        printServer(servers[i]);
    }

    return 0;
}



// #include "../includes/ConfigParser.hpp"
// #include <iostream>

// // g++ -std=c++17 -Wall -Wextra -Werror ConfigParser.cpp ConfigParsingUtils.cpp configmaintester.cpp -o configTest
// // For testing if conf file, not empty, removing comments, whitespace and skip line if empty
// int main()
// {
//     ConfigParser parser;

//     std::string filepath = "../config/default.conf";

//     if (!parser.parseConfigFile(filepath)) {
//         std::cerr << "Failed to parse config file: " << filepath << std::endl;
//         return 1;
//     }

//     std::cout << "Parse successful!" << std::endl;
//     return 0;
// }



// #include "ConfigParser.hpp"

// // argc <= 2, if 2, argv[1] is config file path
// // otherwise use default path "./config/default.conf" at argv[1]

// // For actual main function
// // if (argc <= 2)
// // {
// // 	ConfigParser config;
// // 	std::string input;
// // 	if (argc == 1)
// // 		input = "./config/default.conf";
// // 	else
// // 		input = argv[1];
// // 	if (!config.parseConfigFile(input))
// // 		return 1;
// // 	config.printParsedConfig();
// // 	return 0;
// // }

// // Tester for ConfigParser
// int	main()
// {
// 	ConfigParser file;
// 	std::string input = "./config/default.conf";

// 	if (!file.parseConfigFile(input))
// 		return 1;
// 	file.printParsedConfig();
// 	return 0;
// }