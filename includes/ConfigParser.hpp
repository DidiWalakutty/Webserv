#pragma once

#include "Config.hpp"
#include <sys/stat.h>
#include <string>
#include <vector>
#include <fstream>
#include <algorithm>
#include <cctype>
#include <iostream>
#include <sstream>
#include <set>

/* The ConfigParser class is responsible for:
* - Reading a configuration file
* - Parsing server and location blocks from config file
* - Validating values and settings (host, port, paths, methods, etc.)
* - Filling ServerParse and LocationParse structures
*/
// Min and max body_size
static const size_t MIN_CONFIG_BODY_SIZE = 1;							// 1 byte
static const size_t MAX_CONFIG_BODY_SIZE = 10 * 1024 * 1024;	// 10 MB

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
		std::vector<std::string> splitBySemicolon(const std::string& line) const;

		// --- Parse Server and Location Blocks ---
		ServerParse parseServerBlock(const std::vector<std::string>& fileLines, size_t& currentLine, bool& parsing_error);
		LocationParse parseLocationBlock(const std::vector<std::string>& fileLines, size_t& currentLine, bool& parsing_error);

		// --- Validation Data ---
		bool validateServerParse(ServerParse& server);
		// bool validateLocationParse(const LocationParse& location) const;
		bool isValidHTTPConfMeth(const std::string& method) const;
		bool stringToHTTPConfMeth(const std::string& method, HTTPConfMeth& outMethod);

	public:
		ConfigParser();
		~ConfigParser();
		
		// --- Read, Parse and retrieve servers ---
		bool parseConfigFile(const std::string& file);
		const std::vector<ServerParse>& getServers() const { return _servers; } // Returns the parsed server configurations

		// --- Accessors for best location and error pages ---
		const LocationParse* getBestLocation(const ServerParse& server, const std::string& path) const;
		const std::string* getErrorPage(const ServerParse& server, int errorCode) const;
		
		// For debugging: print parsed config
		void print_server(const ServerParse& server) const;
};