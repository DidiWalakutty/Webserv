#include <iostream>
#include <string>
#include <vector>
#include "../includes/Config.hpp"
#include "../includes/ConfigParser.hpp"

// g++ -std=c++17 -Wall -Wextra -Werror ConfigParser.cpp ConfigParsingUtils.cpp configmaintester.cpp ConfigValidation.cpp -o configTest


// Helper function to print HTTPMethod
std::string method_to_string(HTTPMethod m)
{
    switch (m)
    {
        case HTTPMethod::GET: return "GET";
        case HTTPMethod::POST: return "POST";
        case HTTPMethod::DELETE: return "DELETE";
        default: return "UNKNOWN";
    }
}

void print_methods(const std::vector<HTTPMethod>& methods)
{
    for (size_t i = 0; i < methods.size(); ++i)
    {
        std::cout << method_to_string(methods[i]);
        if (i + 1 < methods.size())
            std::cout << ", ";
    }
}

// Print LocationConfig details
void print_location(const LocationConfig& loc)
{
    std::cout << "  Location:\n";
    std::cout << "    path: " << loc.path << "\n";
    std::cout << "    root: " << loc.root << "\n";
    std::cout << "    index: " << loc.index << "\n";

    std::cout << "    autoindex: ";
    if (loc.autoIndex.has_value())
        std::cout << (*loc.autoIndex ? "on" : "off") << "\n";
    else
        std::cout << "n/a\n";

    std::cout << "    is_cgi: ";
    if (loc.is_cgi.has_value())
        std::cout << (*loc.is_cgi ? "true" : "false") << "\n";
    else
        std::cout << "n/a\n";

    std::cout << "    uploadEnabled: ";
    if (loc.uploadEnabled.has_value())
        std::cout << (*loc.uploadEnabled ? "true" : "false") << "\n";
    else
        std::cout << "n/a\n";

    std::cout << "    allowed methods: ";
    print_methods(loc.allowedMethods);
    std::cout << "\n";

    if (loc.redirect.statusCode != 0)
    {
        std::cout << "    return/redirect:\n";
        std::cout << "      status: " << loc.redirect.statusCode << "\n";
        std::cout << "      target: " << loc.redirect.targetURL << "\n";
    }
}

// Print Serverconfig details
void print_server(const ServerConfig& server)
{
    std::cout << "=================================\n";
    std::cout << "ServerConfig\n";
    std::cout << "---------------------------------\n";
    std::cout << "server_name: " << server.serverName << "\n";
    std::cout << "host: " << server.host << "\n";
    std::cout << "port: " << server.port << "\n";
    std::cout << "root: " << server.root << "\n";
    std::cout << "index: " << server.index << "\n";
    std::cout << "allowed methods: ";
    print_methods(server.allowedMethods);
    std::cout << "\n";
	std::cout << "autoindex: " << (server.autoIndex ? "on" : "off") << "\n";
    std::cout << "max_body_size: " << server.maxBodySize << "\n";
	std::cout << std::endl;
	
    if (!server.errorPages.empty())
    {
        std::cout << "error pages:\n";
        for (std::map<int, std::string>::const_iterator it = server.errorPages.begin();
             it != server.errorPages.end(); ++it)
        {
            std::cout << "  " << it->first << " -> " << it->second << "\n";
        }
    }

    std::cout << "\nlocations:\n";
    for (size_t i = 0; i < server.locations.size(); ++i)
    {
        print_location(server.locations[i]);
    }
    std::cout << "=================================\n\n";
}

// Tester for Parsing Config File
int main()
{
    ConfigParser parser;
    std::string filepath = "../config/default.conf";

	// --- Parse + Validate .conf file ---
    if (!parser.parseConfigFile(filepath))
	{
		std::cout << "Error parsing config" << std::endl;
		return 1;
	}
    
	const std::vector<ServerConfig>& servers = parser.getServers();
	
	// --- Print Servers ---
	// for (size_t i = 0; i < servers.size(); ++i)
	// 	print_server(servers[i]);

	// --- Test paths to simulate requests ---
	std::vector<std::string> testPaths = {
        "/", 
        "/images", 
        "/images/logo.png", 
        "/uploads/file.txt", 
        "/cgi-bin/script.php",
        "/nothing/hi",
        "images/logo.png/",     // missing leading slash
        "/images//logo.png",    // double slashes
        "/uploads/evil?.txt",   // forbidden character
        "/cgi-bin/../etc/passwd"// directory traversal
    };

	 for (size_t s = 0; s < servers.size(); ++s)
    {
        const ServerConfig& server = servers[s];

        std::cout << "\n====================================\n";
        std::cout << "Server: " << server.serverName
                  << " (" << server.host << ":" << server.port << ")\n";
        std::cout << "====================================\n";

        for (size_t i = 0; i < testPaths.size(); ++i)
		{
			const std::string& path = testPaths[i];
			std::cout << "\nRequested Path: " << path << "\n";

			std::string fsPath = server.build_filesystem_path(path);
			if (!fsPath.empty())
				std::cout << "Filesystem path: " << fsPath << "\n";
			else
				std::cout << "No matching location found\n";

			std::cout << "------------------------------------\n";
		}
    }

    return 0;
}
