#pragma once

#include "Config.hpp"
#include <string>
#include <vector>
#include <fstream>
#include <optional>

/* The ConfigParser class is responsible for:
* - Reading a configuration file
* - Parsing server and location blocks from config file
* - Validating values and settings (host, port, paths, methods, etc.)
* - Filling ServerConfig and LocationConfig structures
*/

class ConfigParser {
	private:
		std::vector<ServerConfig> _servers; // Stores all parsed servers

		// --- Text Helpers ---
		void removeComments(std::string& line);
		void trimWhitespace(std::string& line);
		bool isLineEmpty(const std::string& line) const;

		// --- Split by Token ---
		std::vector<std::string> splitByWhitespace(const std::string& line) const;
		std::vector<std::string> splitBySemicolon(const std::string& line) const;

		// --- Parse Server and Location Blocks ---
		ServerConfig parseServerBlock(const std::vector<std::string>& configLines, size_t& currentLine);
		LocationConfig parseLocationBlock(const std::vector<std::string>& configLines, size_t& currentLine);

		// --- Validation Data ---
		bool validateServerConfig(const ServerConfig& server) const;
		bool validateLocationConfig(const LocationConfig& location) const;
		bool isValidHTTPMethod(const std::string& method) const;
		std::optional<HTTPMethod> stringToHTTPMethod(const std::string& method) const;

	public:
		ConfigParser() = default;
		~ConfigParser() = default;

		// --- Read, Parse and retrieve servers ---
		bool parseConfigFile(const std::string& filepath);
		const std::vector<ServerConfig>& getServers() const { return _servers; } // Returns the parsed server configurations

		// --- Accessors for best location and error pages ---
		const LocationConfig* getBestLocation(const ServerConfig& server, const std::string& path) const;
		const std::string* getErrorPage(const ServerConfig& server, int errorCode) const;
		
		// For debugging: print parsed config
		void printParsedConfig() const;
};