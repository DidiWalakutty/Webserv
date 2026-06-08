#pragma once

#include "ServerParse.hpp"
#include <sys/stat.h>
#include <string>
#include <vector>
#include <fstream>
#include <algorithm>
#include <cctype>
#include <iostream>
#include <iomanip>
#include <sstream>
#include <set>

/* The ConfigParser class is responsible for:
* - Reading a configuration file
* - Parsing server and location blocks from config file
* - Validating values and settings (host, port, paths, methods, etc.)
* - Filling ServerParse and LocationParse structures
*/
// Min and max body_size
constexpr size_t KB = 1024;
constexpr size_t MB = 1024 * KB;

static const size_t MIN_CONFIG_BODY_SIZE = 1;		// 1 byte
static const size_t MAX_CONFIG_BODY_SIZE = 10 * MB;	// 10 MB

// --- Reads + Validates .conf file ---
class ConfigParser {	
	private:
		std::vector<ServerParse> _servers; // Stores all parsed servers

		// --- File/Line Helpers ---
		bool isConfFile(const std::string& file) const;		// Check if file has .conf extension
		void removeComments(std::string& line);
		void trimWhitespace(std::string& line);
		bool isLineEmpty(const std::string& line) const;

		// --- Split by Token ---
		std::vector<std::string> splitByWhitespace(const std::string& line) const;

		// --- Parse Server and Location Blocks ---
		ServerParse parseServerBlock(const std::vector<std::string>& fileLines, size_t& currentLine, bool& parsing_error);
		LocationParse parseLocationBlock(const std::vector<std::string>& fileLines, size_t& currentLine, bool& parsing_error);

		// --- Validation Data ---
		bool validateServerParse(ServerParse& server);
		bool stringToHTTPMethod(const std::string& method, HTTPMethod& outMethod);
		bool cgiExecutableAllowed(const std::string& executable);
		bool cgiExtensionAllowed(const std::string& executable);
		bool isValidServerName(const std::string& name) const;
		bool isValidIPv4(const std::string& ip) const;
		bool isValidHost(const std::string& host) const;
		bool isValidLocationPath(const std::string& path) const;
		bool isValidRedirectTarget(const std::string& path) const;
		bool isValidErrorPages(const std::map<int, std::string>& errorPages) const;
		bool isValidRoot(const std::string& root) const;
		bool isValidIndex(const std::string& name) const;
		bool validateMethodsAndBools(const LocationParse& loc) const;
		bool validatePathsAndMethods(const ServerParse& server) const;
		bool duplicateLocations(const ServerParse& server) const;
		void print_methods(const std::vector<HTTPMethod>& methods) const;
		void print_location(const LocationParse& loc) const;

	public:
		ConfigParser();
		~ConfigParser();
		
		// --- Read, Parse and retrieve servers ---
		bool parseConfigFile(const std::string& file);
		const std::vector<ServerParse>& getServers() const; // Returns the parsed server configurations

		// --- Accessors for best location and error pages ---
		ssize_t getMaxBodySize(const ServerParse& server);
		// For debugging: print parsed config
		void print_server(const ServerParse& server) const;
};