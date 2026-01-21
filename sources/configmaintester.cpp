// #include <iostream>
// #include <string>
// #include <vector>
// #include "../includes/Config.hpp"
// #include "../includes/ConfigParser.hpp"

// // g++ -std=c++17 -Wall -Wextra -Werror ConfigParser.cpp ConfigParsingUtils.cpp configmaintester.cpp ConfigValidation.cpp -o configTest



// // Tester for paths to simulate requests
// int main()
// {
//     ConfigParser parser;
//     std::string filepath = "../config/default.conf";

// 	// --- Parse + Validate .conf file ---
//     if (!parser.parseConfigFile(filepath))
// 	{
// 		std::cout << "Error parsing config" << std::endl;
// 		return 1;
// 	}
    
	
// 	// --- Print Servers ---
// 	const std::vector<ServerParse>& servers = parser.getServers();
// 	for (size_t i = 0; i < servers.size(); ++i)
// 		parser.print_server(servers[i]);

// 		// --- Test paths to simulate requests ---
// 		std::vector<std::string> testPaths = {
//         "/", 
//         "/images", 
//         "/images/logo.png", 
//         "/uploads/file.txt", 
//         "/cgi-bin/script.php",
//         "/nothing/hi",
//         "images/logo.png/",     // missing leading slash
//         "/images//logo.png",    // double slashes
//         "/uploads/evil?.txt",   // forbidden character
//         "/cgi-bin/../etc/passwd"// directory traversal
//     };

// 	 for (size_t s = 0; s < servers.size(); ++s)
//     {
//         const ServerParse& server = servers[s];

//         std::cout << "\n====================================\n";
//         std::cout << "Server: " << server.serverName
//                   << " (" << server.host << ":" << server.port << ")\n";
//         std::cout << "====================================\n";

//         for (size_t i = 0; i < testPaths.size(); ++i)
// 		{
// 			const std::string& path = testPaths[i];
// 			std::cout << "\nRequested Path: " << path << "\n";

// 			std::string fsPath = server.build_filesystem_path(path);
// 			if (!fsPath.empty())
// 				std::cout << "Filesystem path: " << fsPath << "\n";
// 			else
// 				std::cout << "No matching location found\n";

// 			std::cout << "------------------------------------\n";
// 		}
//     }

//     return 0;
// }
