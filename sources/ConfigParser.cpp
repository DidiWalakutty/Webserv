#include "../includes/ConfigParser.hpp"

ConfigParser::ConfigParser() {}

ConfigParser::~ConfigParser() {}

const std::vector<ServerParse>& ConfigParser::getServers() const
{
	return _servers;
}

// Verifies that no two servers share the same host and port.
// Logs an error message and returns true if a duplicate is detected.
static bool duplicatesAcrossServers(const std::vector<ServerParse>& servers)
{
	for (size_t i = 0; i < servers.size(); ++i)
	{
		for (size_t j = i + 1; j < servers.size(); ++j)
		{
			if (servers[i].host == servers[j].host && 
				servers[i].port == servers[j].port)
			{
				std::cerr << "Error: Duplicate server definition detected:\n" 
						  << " host: " << servers[i].host << "\n"
						  << " port: " << servers[i].port << std::endl;
				return true;
			}
		}
	}
	return false;
}

/**
 * @brief Parses the full configuration file and builds servers objects.
 *
 * @details
 * - Validates file type and accessibility.
 * - Preprocesses input (removes comments, trims whitespace, skips empty lines).
 * - Parses all server blocks and their nested location blocks, extracting directives and values.
 * - Validates each server configuration and checks for duplicate host/port combinations across servers.
 *
 * @return true if parsing succeeds and all server blocks are valid, false otherwise.
 */
bool ConfigParser::parseConfigFile(const std::string& file)
{
	// --- Clear previous _servers ---
	_servers.clear();

	// -- Check file validity and if possible to open file ---
	if (!isConfFile(file))
		return false;
	
	// --- Open file ---
	std::ifstream infile(file);
	if (!infile.is_open())
	{
		std::cerr << "Error: Could not open config file: " << file << std::endl;
		return false;
	}

	if (infile.peek() == std::ifstream::traits_type::eof())
	{
		std::cerr << "Error: Config File is empty: " << file << std::endl;
		return false;
	}

	// --- Preprocess Lines ---
	std::vector<std::string> processedLines;	// Stores preprocessed lines for parsing to Server/Location blocks
	std::string line;
	while (std::getline(infile, line))
	{
		removeComments(line);
		trimWhitespace(line);
		if (!isLineEmpty(line))
		{
			processedLines.push_back(line);
		}
	}

	// -- Detect Server and Location Blocks + Parse ---
	size_t 	currentLine = 0;
	size_t 	serverCount = 0;
	bool parsing_error = false;

	while (currentLine < processedLines.size())
	{
		if (processedLines[currentLine] == "server {")
		{
			++serverCount;
			ServerParse server = parseServerBlock(processedLines, currentLine, parsing_error);
			if (!parsing_error && validateServerParse(server))
			{
				_servers.push_back(server);
			}
			else
			{
				return false;
			}
		}
		else
		{
			std::cerr << "Error: Unexpected line: " << processedLines[currentLine] << " outside server block at line: " << currentLine + 1 << std::endl;
			return false;
		}
	}
	
	if (duplicatesAcrossServers(_servers))
		return false;
	return true;
}

/**
 * @brief Parses a single server block, including its nested location blocks.
 * 
 * @details 
 * - Reads key/value pairs inside the server block
 * - Detects and parses nested location blocks
 * - Validates syntax
 * - Handles server directives (server_name, host, port, root, index, autoindex etc)
 * - Collects LocationParse objects for each location block and assigns them to the server.
 * - Tracks opening and closing braces to ensure proper block structure and updates currentLine accordingly.
 * 
 * @return Parsed server configuration (may be invalid if parsing_error is true) 
 */
ServerParse ConfigParser::parseServerBlock(const std::vector<std::string>& fileLines, size_t& currentLine, bool& parsing_error)
{
	// --- Initialize server parsing state and temporary storage for locations ---
	ServerParse server;
	std::vector<LocationParse> locations;
	bool location_error = false;

	int brackOpen = 1;
	int brackclose = 0;
	++currentLine;

	// --- Enter server block parsing loop, continue until matching closing brace is found ---
	while (currentLine < fileLines.size() && brackOpen > 0)
	{
		const std::string line = fileLines[currentLine]; // local copy we can modify
		// --- Detect and parse nested location blocks ---
		if (line.find("location") == 0)
		{
			// Counts brackets in location header
			size_t locOpen = std::count(line.begin(), line.end(), '{');
			size_t locClose = std::count(line.begin(), line.end(), '}');
			if (locOpen != 1 || locClose != 0 || line.back() != '{')
			{
				std::cerr << "Error: Invalid location block header at line: " << currentLine + 1 << ": " << std::endl;
				parsing_error = true;
				return server;
			}

			LocationParse location = parseLocationBlock(fileLines, currentLine, location_error);
			locations.push_back(location);
			continue;
		}
		
		// --- Count and track brackets in server block line ---
		size_t openCount = std::count(line.begin(), line.end(), '{');
		size_t closeCount = std::count(line.begin(), line.end(), '}');
		brackOpen += openCount;
		brackclose += closeCount;

		// --- Validate that no extra braces exist within the server block ---
		if (brackOpen > 1)
		{
			std::cerr << "Error: found extra '{' in Server block at line: " << currentLine + 1 << std::endl;
			parsing_error = true;
			return server;
		}
		if (brackclose > 1)
		{
			std::cerr << "Error: found extra '}' in Server block at line " << currentLine + 1 << std::endl;
			parsing_error = true;
			return server; 
		}

		// --- Server Parsing ---
		size_t equalPos = line.find("=");
		if (equalPos != std::string::npos)
		{
			// --- Parse key=value directives within the server block ---
			std::string key = line.substr(0, equalPos);
			std::string value = line.substr(equalPos + 1);

			// Trim whitespaces
			trimWhitespace(key);
			trimWhitespace(value);

			// Remove last char of value if it's a semicolon
			if (!value.empty() && value.back() == ';')
				value.pop_back();
			else
			{
				std::cerr << "Error: Invalid string format. Missing closing ';' after value: '" << value << "' in Serverblock" << std::endl;
				parsing_error = true;
			}

			// Strip quotes around value
			if (!value.empty() && (value.front() == '"' || value.front() == '\'') &&
									(value.back() == '"' || value.back() == '\''))
				value = value.substr(1, value.size() - 2);

				
			// --- Handle key-value pairs ---
			if (key == "server_name")
				server.serverName = value;
			else if (key == "host")
				server.host = value;
			else if (key == "port")
			{
				try {
					server.port = std::stoi(value);
				} 
				catch (const std::invalid_argument&) {
					std::cerr << "Error: Invalid Port - not a number - at line " << currentLine + 1 << std::endl;
					parsing_error = true;
					server.port = 0;
				} 
				catch (const std::out_of_range&) {
					std::cerr << "Error: Invalid Port - out of range - at line " << currentLine + 1 << std::endl;
					parsing_error = true;
					server.port = 0;
				}
			}
			else if (key == "root")
				server.root = value;
			else if (key == "index")
				server.index = value;
			else if (key == "autoindex")
			{ 
				if (value == "true" || value == "on")
					server.autoIndex = true;
				else if (value == "false" || value == "off")
					server.autoIndex = false;
				else
				{
					std::cerr << "Warning: Invalid autoindex value: '" << value << "' in server block at line: " << currentLine + 1 << std::endl;
					server.autoIndex = false;
					std::cerr << "Auto Index was defaulted to " << (server.autoIndex ? "true" : "false") << std::endl;
				}
			}
			else if (key == "allowed_methods")
			{
				server.allowedMethods.clear();
				std::vector<std::string> tokens = splitByWhitespace(value);
				for (size_t i = 0; i < tokens.size(); ++i)
				{
					HTTPMethod method; 
					if (stringToHTTPMethod(tokens[i], method))
					{
						server.allowedMethods.push_back(method);
					}
					else
					{
						std::cerr << "Warning: Invalid HTTP method: '" << tokens[i] << "' in server block at line: " << currentLine + 1 << std::endl;
					}
				}
			}
			else if (key == "max_body_size")
			{
				try {
					server.maxBodySize = std::stoul(value);
				}
				catch (const std::invalid_argument&) {
					std::cerr << "Error: Invalid Max_Body_size at line " << currentLine + 1 << std::endl;
					parsing_error = true;
					server.maxBodySize = 0;
				}
				catch (const std::out_of_range&) {
					std::cerr << "Error: Invalid Max_Body_size - too big - at line " << currentLine + 1 << std::endl;
					parsing_error = true;
					server.maxBodySize = 0;
				}
			}
			else if (key == "error_page")
			{
				std::vector<std::string> tokens = splitByWhitespace(value);
				
				// value format: <error_code> <path>
				if (tokens.size() == 2)
				{
					try {
						int errorCode = std::stoi(tokens[0]);
						std::string errorPath = tokens[1];
						
						if (server.errorPages.count(errorCode))
						{
							std::cerr << "Error: Duplicate error_page for code: " << errorCode << " at line: " << currentLine + 1 << std::endl;
							parsing_error = true;
						}
						else
						{
							server.errorPages[errorCode] = errorPath;
						}
					} 
					catch (const std::invalid_argument&) {
						std::cerr << "Error: Error Page - not a number - at line " << currentLine + 1 << std::endl;
						parsing_error = true;
					}
					catch (const std::out_of_range&) {
						std::cerr << "Error: Error Page - out of range - at line " << currentLine + 1 << std::endl;
						parsing_error = true;
					}
				}
				else
				{
					std::cerr << "Error: Invalid error_page directive format in server block at line: " << currentLine + 1 << std::endl;
					parsing_error = true;
				}
			}
			else
			{
				std::cerr << "Error: Unknown directive: '" << key << "' in server block at line: " << currentLine + 1 << std::endl;
				parsing_error = true;
			}
		}
		// --- Advance to next line and exit if closing brace is reached ---
		++currentLine;
		if (brackclose == 1)
			break;
	}

	// --- Ensure server block is properly closed ---
	if (brackclose != 1)
	{
		std::cerr << "Error: Server block not closed before end of file" << std::endl;
		parsing_error = true;
	}

	// --- Propagate location parsing errors to server level ---
	if (location_error == true)
	parsing_error = true;

	server.locations = locations;

	return server; 
}

/**
 * @brief Parses a location block inside a server configuration.
 *
 * @details  
 * - Extract location path and initialize default values.
 * - Parses directives within the location block (root, index, autoindex, allowed_methods etc).
 * - Validates syntax and tracks opening/closing braces to ensure proper block structure.
 * - Detects  invalid syntax and unsupported directives.
 * 
 * Tracks opening and closing braces to ensure the location block is
 * properly structured. Updates currentLine while parsing and advances it
 * to the line after the closing '}' of the location block.
 *
 * @param fileLines Preprocessed configuration lines.
 * @param currentLine Index of the current parsing position (updated during parsing).
 * @param location_error Flag set to true if a parsing error occurs.
 *
 * @return A LocationParse object containing parsed values
 *         (may be partially filled on error).
 */
LocationParse ConfigParser::parseLocationBlock(const std::vector<std::string>& fileLines, size_t& currentLine, bool& location_error)
{
	LocationParse location;

	// --- Extract the "location /path {" line ---
	const std::string line = fileLines[currentLine];
	size_t pathStart = line.find("location") + 8; // gives what's after "location"
	size_t bracePos = line.find('{');

	// --- Set default values ---
	location.autoIndex = false;
	location.is_cgi = false;
	location.maxBodySize = 0;
	location.cgi_executable = {};
	location.cgi_extension = {};

	// --- Validate location header format ---
	location.path = line.substr(pathStart, bracePos - pathStart);
	trimWhitespace(location.path);
	
	int braceCount = 1;
	++currentLine;
	
	// --- Enter location block and start parsing inner directives ---
	while (currentLine < fileLines.size())
	{
		const std::string line = fileLines[currentLine];

		// --- Braces Count for tracking ---
		size_t openCount = std::count(line.begin(), line.end(), '{');
		size_t closeCount = std::count(line.begin(), line.end(), '}');
		braceCount += openCount;
		braceCount -= closeCount;
	
		if (braceCount > 1)
		{
			std::cerr << "Error: found extra '{' in location block at line: " << currentLine + 1 << std::endl;
			location_error = true;
			return location;
		}
		if (closeCount > 1)
		{
			std::cerr << "Error: found extra '}' in location block at line: " << currentLine + 1 << std::endl;
			location_error = true;
			return location;
		}
		if (braceCount == 0)
			break;

		size_t equalPos = line.find("=");
		if (equalPos != std::string::npos)
		{
			std::string key = line.substr(0, equalPos);
			std::string value = line.substr(equalPos + 1);

			trimWhitespace(key);
			trimWhitespace(value);

			// Remove last char of value if it's a semicolon
			if (!value.empty() && value.back() == ';')
				value.pop_back();
			else
			{
				std::cerr << "Error: Invalid string format. Missing closing ';' after value: " << value << " in Locationblock" << std::endl;
				location_error = true;
			}

			// Strip quotes around value
			if (!value.empty() && (value.front() == '"' || value.front() == '\'') &&
									(value.back() == '"' || value.back() == '\''))
				value = value.substr(1, value.size() - 2);					
	
			// --- Handle key-value pairs ---
			if (key == "root")
				location.root = value;
			else if (key == "index")
				location.index = value;
			else if (key == "autoindex")
			{
				if (value == "on" || value == "true")
					location.autoIndex = true;
				else if (value == "off" || value == "false")
					location.autoIndex = false;
				else
				{
					std::cerr << "Warning: Invalid autoindex value: '" << value << "' in location block at line: " << currentLine + 1 << std::endl;
					location.autoIndex = false;
					std::cerr << "Auto Index was defaulted to " << (location.autoIndex ? "true" : "false") << std::endl;
				}
			}
			else if (key == "allowed_methods")
			{
				location.allowedMethods.clear();
				std::vector<std::string> tokens = splitByWhitespace(value);
				for (size_t i = 0; i < tokens.size(); ++i)
				{
					HTTPMethod method;
					if (stringToHTTPMethod(tokens[i], method))
					{
						location.allowedMethods.push_back(method);
					}
					else
					{
						std::cerr << "Warning: Invalid HTTP method: '" << tokens[i] << "' in location block at line: " << currentLine + 1 << std::endl;					
						// what do we want to do???
					}
				}
			}
			else if (key == "return")
			{
				std::vector<std::string> tokens = splitByWhitespace(value);
				if (tokens.size() == 2)
				{
					try {
						location.redirect.statusCode = std::stoi(tokens[0]);
					}
					catch (const std::invalid_argument&) {
						std::cerr << "Error: Return statuscode is not a number at line: " << currentLine + 1 << std::endl;
						location_error = true;
						location.redirect.statusCode = 0;
					}
					catch (const std::out_of_range&) {
						std::cerr << "Error: Return status code is too big. Check line: " << currentLine + 1 << std::endl;
						location_error = true;
						location.redirect.statusCode = 0;
					}
					location.redirect.targetURL = tokens[1];
				}
				else
				{
					std::cerr << "Error: Invalid return directive format in location block at line: " << currentLine + 1 << std::endl;
					location_error = true;
				}
			}
			else if (key == "is_cgi")
			{
				if (value == "true")
					location.is_cgi = true;
				else if (value == "false")
					location.is_cgi = false;
				else
				{
					std::cerr << "Warning: Invalid choice for is_cgi in location block at line: " << currentLine + 1 << std::endl;
					location.is_cgi = false;
					std::cerr << "Is_cgi was defaulted to " << (location.is_cgi ? "true" : "false") << std::endl;
				}
	
			}
			else if (key == "cgi_executable")
			{
				location.cgi_executable.clear();
				std::vector<std::string> tokens = splitByWhitespace(value);
				for (size_t i = 0; i < tokens.size(); ++i)
				{
					if (cgiExecutableAllowed(tokens[i]))
						location.cgi_executable.push_back(tokens[i]);
					else
						std::cerr << "Warning: Invalid Executable: '" << tokens[i] << "' in location block at line: " << currentLine + 1 << std::endl;					
				}
			}
			else if (key == "cgi_extension")
			{
				location.cgi_extension.clear();
				std::vector<std::string> tokens = splitByWhitespace(value);
				for (size_t i = 0; i < tokens.size(); ++i)
				{
					if (cgiExtensionAllowed(tokens[i]))
						location.cgi_extension.push_back(tokens[i]);
					else
						std::cerr << "Warning: Invalid Extension: '" << tokens[i] << "' in location block at line: " << currentLine + 1 << std::endl;					
				}
			}
			else if (key == "max_body_size")
			{
				try {
					location.maxBodySize = std::stoul(value);
				}
				catch (const std::invalid_argument&) {
					std::cerr << "Error: Invalid max_body_size at line " << currentLine + 1 << std::endl;
					location_error = true;
					location.maxBodySize = 0;
				}
				catch (const std::out_of_range&) {
					std::cerr << "Error: Invalid max_body_size - too big - at line " << currentLine + 1 << std::endl;
					location_error = true;
					location.maxBodySize = 0;
				}
			}
			else
			{
				std::cerr << "Error: Unknown directive: '" << key << "' in location block at line: " << currentLine + 1 << std::endl;
				location_error = true;
			}
		}
		++currentLine;

	}

	if (braceCount != 0)
	{
		std::cerr << "Error: Location block not closed before end of file block." << std::endl;
		location_error = true;
	}
	++currentLine;	// move past closing }
	
	return location;
}

ssize_t ConfigParser::getMaxBodySize(const ServerParse& server)
{
	if (server.maxBodySize > 0)
		return server.maxBodySize;
	else
		return MAX_CONFIG_BODY_SIZE; // default max body size if not set in config
}

bool ConfigParser::cgiExecutableAllowed(const std::string& executable)
{
	if (executable == "/opt/pyenv/shims/python3" || executable == "/usr/bin/bash" || executable == "/usr/bin/php-cgi")
		return true;
	return false;
}

bool ConfigParser::cgiExtensionAllowed(const std::string& extension)
{
	if (extension == ".py" || extension == ".sh" || extension == ".php")
		return true;
	return false;
}

// Checks if the file has a .conf extension.
// extension is substring after last '.', then compare to ".conf"
bool ConfigParser::isConfFile(const std::string& file) const
{
	size_t dotPos = file.find_last_of('.');
	if (dotPos == std::string::npos)
	{
		std::cerr << "Error: Selected config file has no extension." << std::endl;
		return false;
	}
	std::string extension = file.substr(dotPos);
	if (extension != ".conf")
	{
		std::cerr << "Error: Config file must have a '.conf' extension." << std::endl;
		return false;
	}
	return true;
}

// Removes all # comments, and // comments only if they appear after a semicolon.
void ConfigParser::removeComments(std::string& line)
{
	// Init to npos, will hold the earliest comment position found
	size_t commentPos = std::string::npos;

	size_t hashPos = line.find('#');
	if (hashPos != std::string::npos)
		commentPos = hashPos;

	// finds '//' if after semicolon and updates commentPos
	size_t semicolonPos = line.find(';');
	if (semicolonPos != std::string::npos)
	{
		size_t slashPos = line.find("//", semicolonPos + 1);
		if (slashPos != std::string::npos)
		{
			if (commentPos == std::string::npos || slashPos < commentPos)
				commentPos = slashPos;
		}
	}

	if (commentPos != std::string::npos)
		line.erase(commentPos);
}

// Finds first and last non-whitespace characters and trims the line accordingly
void ConfigParser::trimWhitespace(std::string& line)
{
	size_t start = line.find_first_not_of(" \t");
	size_t end = line.find_last_not_of(" \t");

	if (start == std::string::npos)
	{
		line.clear(); // Clear line if it contains only whitespace
		return;
	}
	line = line.substr(start, end - start + 1);	// keeps only the content between start and end
}

// Checks if line length is zero
bool ConfigParser::isLineEmpty(const std::string& line) const
{
	return line.empty();
}

// Splits a line by whitespace into tokens -> parseServerBlock, parseLocationBlock
std::vector<std::string> ConfigParser::splitByWhitespace(const std::string& str) const
{
    std::vector<std::string> tokens;
    std::string token;
	// --- Create a stringstream ---
    std::istringstream iss(str);
    while (iss >> token)
        tokens.push_back(token);
    return tokens;
}

bool ConfigParser::stringToHTTPMethod(const std::string& method, HTTPMethod& outmethod)
{
	if (method == "GET")
	{
		outmethod = HTTPMethod::GET;
		return true;
	}
	else if (method == "POST")
	{
		outmethod = HTTPMethod::POST;
		return true;
	}
	else if (method == "DELETE")
	{
		outmethod = HTTPMethod::DELETE;
		return true;
	}
	return false;
}

void ConfigParser::print_methods(const std::vector<HTTPMethod>& methods) const
{
    for (size_t i = 0; i < methods.size(); ++i)
    {
        std::cout << HTTPCommon::methodToString(methods[i]);
        if (i + 1 < methods.size())
            std::cout << ", ";
    }
}

// Print LocationParse details
void ConfigParser::print_location(const LocationParse& loc) const
{
    std::cout << "  \nLocation:\n";
    std::cout << "    path: " << loc.path << "\n";
    std::cout << "    root: " << loc.root << "\n";
    std::cout << "    index: " << loc.index << "\n";

    std::cout << "    autoindex: ";
    if (loc.autoIndex)
        std::cout << "true" << "\n";
    else
        std::cout << "false\n";

    std::cout << "    is_cgi: ";
    if (loc.is_cgi)
        std::cout << "true" << "\n";
    else
        std::cout << "false\n";

    std::cout << "    allowed methods: ";
    this->print_methods(loc.allowedMethods);
    std::cout << "\n";

    if (loc.redirect.statusCode != 0)
    {
        std::cout << "    return/redirect:\n";
        std::cout << "      status: " << loc.redirect.statusCode << "\n";
        std::cout << "      target: " << loc.redirect.targetURL << "\n";
    }
}

// Print ServerParse details
void ConfigParser::print_server(const ServerParse& server) const
{
    std::cout << "=================================\n";
    std::cout << "ServerParse\n";
    std::cout << "---------------------------------\n";
    std::cout << "server_name: " << server.serverName << "\n";
    std::cout << "host: " << server.host << "\n";
    std::cout << "port: " << server.port << "\n";
    std::cout << "root: " << server.root << "\n";
    std::cout << "index: " << server.index << "\n";
    std::cout << "allowed methods: ";
	this->print_methods(server.allowedMethods);
    std::cout << "\n";
	std::cout << "autoindex: " << server.autoIndex << "\n";
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
		this->print_location(server.locations[i]);
		}
		std::cout << "=================================\n\n";
}

bool ConfigParser::isValidServerName(const std::string& name) const
{
	if (name.empty())
	{
		std::cerr << "Error: Server name is empty" << std::endl;
		return false;
	}
	for (char c : name)
	{
		if (!isalnum(c) && c != '-' && c != '.' && c != '_')
		{
			std::cerr << "Error: Invalid character found in server name: '" << c << "'. Please avoid spaces and special characters." << std::endl;
			return false;
		}
	}
	return true;
}

bool ConfigParser::isValidIPv4(const std::string& ip) const
{
	int dotCount = 0;
	for (char c : ip)
		if (c == '.')
			dotCount++;
	if (dotCount != 3)
		return false;

	std::istringstream string(ip);
	std::string block;
	// reads from string up to the next dot, and saves it in block
	while (std::getline(string, block, '.'))
	{
		if (block.empty())
			return false;
		// if block-length > 1, it can't start with 0 -> '001.8.10.30'.
		if (block.size() > 1 && block[0] == '0')
			return false;
		for (char c : block)
			if (!isdigit(c))
				return false;
		int number = std::stoi(block);
		if (number < 0 || number > 255)
			return (false);
	}
	return true;
}

bool ConfigParser::isValidHost(const std::string& host) const
{
	if (host.empty())
	{
		std::cerr << "Error: Host cannot be empty." << std::endl;
		return (false);
	}
	
	// Decide if host looks like an IPv4 (digits and dots only).
	bool looksIPv4 = true;
	for (char c : host)
	{
		if (!isdigit(c) && c != '.' && c != '-' && c != '+' )
		{
			looksIPv4 = false;
			break;
		}
	}

	// If so, validate it.
	if (looksIPv4)
	{
		if (!isValidIPv4(host))
		{
			std::cerr << "Error: Host name - IP is invalid:\nHost is ID'd as a possible IP (digits and dots).\nIP contains 3 dots, no leading '0's in a block and no +/- operators." << std::endl;
			return false;
		}
		return true;
	}

	// Else, validate domain name
	if (host.size() > 42)
	{
		std::cerr << "Error: Host name cannot be more than 42 characters" << std::endl;
		return false;
	}
	for (char c : host)
	{
		if (!isalnum(c) && c != '.' && c != '-')
		{
			std::cerr << "Error: Host name contains forbidden characters (spaces or special symbols)." << std::endl;
			return false;
		}
	}
	return true;
}

bool ConfigParser::isValidLocationPath(const std::string& path) const
{
	if (path.empty())
	return false;
	
	if (path.find("..") != std::string::npos)
	return false;
	
	if (path.find("//") != std::string::npos)
	return false;
		
	for (char c : path)
	{
		if (!isalnum(c) && c != '/' && c != '-' && c != '_' && c != '.')
			return false;
	}
	return true;
}

bool ConfigParser::isValidRedirectTarget(const std::string& path) const
{
	if (path.empty())
		return false;
	
	if (path.find("..") != std::string::npos)
		return false;
	
	if (path.find(' ') != std::string::npos)
		return false;

	if (path.find("http://") == 0 || path.find("https://") == 0)
		return true;

	// we will check here later
	if (path.find("www/") == 0)
		return true;

	return isValidLocationPath(path);
}

// Check each code and path pair.
// Iterator that points to first element and loops until the last.
// Each map element is a pair: current -> (404 -> "www/html/errors/404.html")
bool ConfigParser::isValidErrorPages(const std::map<int, std::string>& errorPages) const
{
	const std::string ErrorPagePrefix = "www/html/errors/";

	std::map<int, std::string>::const_iterator current = errorPages.begin();

	while (current != errorPages.end())
	{
		int statusCode = current->first;		// map KEY -> HTTP status code
		std::string path = current->second;		// map VALUE -> file path

		if (statusCode < 400 || statusCode > 599)
		{
			std::cerr << "Error: Invalid error_page status code: " << statusCode << 
			". Status code should be between 400 and 599." << std::endl;
			return false;
		}
		if (path.empty())
		{
			std::cerr << "Error: Path for status code: " << statusCode << " is empty." << std::endl;
			return false;
		}
		
		if (path.substr(0, ErrorPagePrefix.size()) != ErrorPagePrefix)
		{
			std::cerr << "Error: Error page must start with '" << ErrorPagePrefix << ": " << path << std::endl;
			return false;
		}

		std::string filename = path.substr(ErrorPagePrefix.size());	// removes the prefix, so we keep 404.html: "www/html/errors/404.html"
		std::string match = std::to_string(statusCode) + ".html";
		if (filename != match)
		{
			std::cerr << "Error: Error page: filename doesn't match status code. Filename: " << filename << ", Status code: " << statusCode << std::endl;
			return false;
		}

		if (!isValidRedirectTarget(path))
		{
			std::cerr << "Error: Invalid syntax for Redirect Target: " << path << std::endl;
			return false;
		}
		++current;
	}
	return true;
} 

// Checks root syntax
bool ConfigParser::isValidRoot(const std::string& root) const
{
	if (root.empty())
		return false;
	
	if (root.find("..") != std::string::npos)
		return false;
		
	if (root.find("//") != std::string::npos)
		return false;
	
	if (root.find("\\") != std::string::npos)
		return false;
	
	for (size_t i = 0; i < root.size(); ++i)
	{
		unsigned char c = root[i];
		if (!std::isprint(c))
			return false;
	}
	return true;
}

// Checks index syntax (filename).
// Will only accept isalnum() + special chars.
// Must end with .html and contain 1 dot. 
bool ConfigParser::isValidIndex(const std::string& name) const
{
	if (name.empty())
	return false;
	
	if (name.find('/') != std::string::npos)
	{
		std::cerr << "Error: Index should not contain a '/'' character. It's not a path." << std::endl;
		return false;
	}

	if (name.find("..") != std::string::npos)
		return false;	

	// finds last position of dot
	size_t dot = name.rfind('.');
	if (dot == std::string::npos || dot == 0 || dot == name.size() -1)
		return false;
	
	for (char c : name)
	{
		if (!isalnum(c) && c != '-' && c != '_' && c != '.')
		return false;
	}

	if (name.size() >= 5 && name.substr(name.size()-5) == ".html")
		return true;

	// we will check here later
	if (name.size() >= 15 && name.substr(name.size()-14) == ".bad_extension")
		return true;

	return false;
}

/**
 * @brief Checks that each location's settings are consistent and valid.
 *  - Verifies redirect-only locations do not mix with other settings.
 *  - Checks correct usage of CGI settings (is_cgi, executable, extension).
 *  - Ensures upload and CGI features are not combined.
 *  - Requires the root ("/") location to allow GET for basic site access.
 */
bool ConfigParser::validateMethodsAndBools(const LocationParse& loc) const
{
	bool hasGet = false;
	bool hasPost = false;
	bool hasDelete = false;

	// --- Path must exist ---
	if (loc.path.empty())
	{
		std::cerr << "Error: Location path cannot be empty" << std::endl;
		return false;
	}

	// --- Validate allowed methods ---
	for (size_t i = 0; i < loc.allowedMethods.size(); ++i)
	{
		HTTPMethod m = loc.allowedMethods[i];
		switch (m)
		{
			case HTTPMethod::GET:    hasGet = true; break;
			case HTTPMethod::POST:   hasPost = true; break;
			case HTTPMethod::DELETE: hasDelete = true; break;
			default:
				std::cerr << "Error: Unknown HTTP method found in location." << std::endl;
				return false;
		}
	}

	// --- Redirect locations should not mix with other settings ---
	if (!loc.redirect.targetURL.empty())
	{
		if (!loc.allowedMethods.empty() || loc.autoIndex 
		    || loc.is_cgi || !loc.root.empty() || !loc.index.empty()
			|| !loc.cgi_extension.empty() || !loc.cgi_executable.empty())
		{
			std::cerr << "Error: Redirect location should only define redirect status code and target URL." << std::endl;
			return false;
		}
	}

	// --- If CGI-related fields are set, is_cgi must be true ---
	if ((!loc.cgi_extension.empty() || !loc.cgi_executable.empty()) && !loc.is_cgi)
	{
		std::cerr << "Error: CGI extension/executable should only be defined if is_cgi is true." << std::endl;
		return false;
	}

	// --- Validate CGI settings ---
	if (loc.is_cgi)
	{
		if (loc.cgi_extension.empty() || loc.cgi_executable.empty())
		{
			std::cerr << "Error: CGI location must have cgi_extension and cgi_executable defined if is_cgi is true." << std::endl;
			return false;
		}
		if (!hasGet && !hasPost && !hasDelete)
		{
			std::cerr << "Error: CGI Location must allow at least one of GET, POST or DELETE." << std::endl;
			return false;
		}
	}

	if (loc.is_cgi && loc.autoIndex)
	{
		std::cerr << "Error: CGI execution and autoindex cannot be enabled at the same time." << std::endl;
		return false;
	}

	// --- Make sure homepage is accessible ---
	if (loc.path == "/" && !hasGet)
	{
		std::cerr << "Error: Location with path '/' must allow GET method." << std::endl;
		return false;
	}

	return true;
}

// Checks all server + location paths on syntax.
// We'll send a true boolean if the file is a filename, and false for directory/root.
bool ConfigParser::validatePathsAndMethods(const ServerParse& server) const
{
	if (!isValidRoot(server.root))
	{
		std::cerr << "Error: Invalid syntax for server root: " << server.root << std::endl;
		return false;
	}

	if (!isValidIndex(server.index))
	{
		std::cerr << "Error: Invalid syntax for server index: " << server.index << std::endl;
		return false;
	}

	for (size_t i = 0; i < server.locations.size(); ++i)
	{
		const LocationParse& loc = server.locations[i];

		// --- Check Location Path ---
		if (!isValidLocationPath(loc.path))
		{
			std::cerr << "Error: Invalid syntax for location path: " << loc.path << std::endl;
			return false;
		}

		// --- Check Redirect Consistency First ---
		if ((loc.redirect.statusCode != 0 && loc.redirect.targetURL.empty()) || \
			(loc.redirect.statusCode == 0 && !loc.redirect.targetURL.empty()))
		{
			std::cerr << "Error: Redirect must define both status code and target url in location block: " << loc.path << std::endl;
			return false;
		}
		if (loc.redirect.statusCode != 0)
		{
			if (!loc.allowedMethods.empty() || loc.autoIndex || \
				loc.is_cgi || !loc.root.empty() || !loc.index.empty())
			{
				std::cerr << "Error: Redirection location should be empty, except for status code and target url" << std::endl;
				return false;
			}
			if (loc.redirect.statusCode != 301 && loc.redirect.statusCode != 302
				&& loc.redirect.statusCode != 307 && loc.redirect.statusCode != 308)
			{
				std::cerr << "Error: Redirect status code must be either 301, 302, 307 or 308." << std::endl;
				return false;
			}

			if (!isValidRedirectTarget(loc.redirect.targetURL))
			{
				std::cerr << "Error: Invalid syntax for location's Redirect url: " << loc.redirect.targetURL << std::endl;
				return false;
			}
			continue;
		}

		// --- Check regular Location ---
		if (!isValidRoot(loc.root))
		{
			std::cerr << "Error: Invalid syntax for location root: " << loc.root << std::endl;
			return false;
		}

		// --- Check Index if not redirect ---
		if (!isValidIndex(loc.index))
		{
			std::cerr << "Error: Invalid syntax for location index: " << loc.index << std::endl;
			return false;
		}

		// --- Check Syntax for Redirects ---
		if (!validateMethodsAndBools(loc))
			return false;
	}

	return true;
}

bool ConfigParser::duplicateLocations(const ServerParse& server) const
{
	std::set<std::string> seen;

	for (size_t i = 0; i < server.locations.size(); ++i)
	{
		const std::string& path = server.locations[i].path;

		if (!seen.insert(path).second)
		{
			std::cerr << "Error: Duplicate location path detected: " << path << " in server: " << server.serverName << std::endl;
			return true;
		}
	}
	return false;
}

bool ConfigParser::validateServerParse(ServerParse& server)
{
	// --- VALIDATE Server ---

	if (!isValidServerName(server.serverName))
		return (false);
	
	if (!isValidHost(server.host))
		return false;
	
	if (server.port < 1 || server.port > 65535)
	{
		std::cerr << "Error: Invalid Port: " << server.port << " for server: " << server.serverName << std::endl;
		return false;
	}
	
	// Without a root, URL -> filesystem mapping is impossible.
	if (server.root.empty())
	{
		std::cerr << "Error: Server root is required and cannot be empty." << std::endl;
		return false;
	}
	
	// Index should have a default index.html.
	if (server.index.empty())
	{
		server.index = "index.html";
		std::cerr << "Index was empty in .conf file. Now set to default: " << server.index << std::endl;
	}
	
	if (!isValidErrorPages(server.errorPages))
		return false;
	
	if (server.allowedMethods.empty())
	{
		server.allowedMethods.push_back(HTTPMethod::GET);
		std::cerr << "No (valid) allowed methods specified. Now set to default: GET." << std::endl;
	}
	
	if (server.maxBodySize < MIN_CONFIG_BODY_SIZE || server.maxBodySize > MAX_CONFIG_BODY_SIZE)
	{
		std::cerr << "Error: max_body_size must be between " << MIN_CONFIG_BODY_SIZE << " and " << MAX_CONFIG_BODY_SIZE << " bytes" << std::endl;
		return false;
	}
	
	// --- Fix empty Location Inheritance + updated with valid Server config if empty ---
	for (size_t i = 0; i < server.locations.size(); ++i)
	{
		LocationParse& location = server.locations[i];

		if (location.root.empty())
			location.root = server.root;
		if (location.index.empty())
			location.index = server.index;
		if (location.allowedMethods.empty())
			location.allowedMethods = server.allowedMethods;
	}

	// If location /redirect, all other info must be empty
	for (size_t i = 0; i < server.locations.size(); ++i)
	{
		LocationParse& loc = server.locations[i];

		if (!loc.redirect.targetURL.empty())
		{
			loc.root.clear();
			loc.index.clear();
			loc.allowedMethods.clear();
			loc.autoIndex = false;
			loc.is_cgi = false;
		}
	}

	// Check duplicate location paths
	if (duplicateLocations(server))
		return false;

	// Checks Syntax
	if (!validatePathsAndMethods(server))
		return false;

	return true;
}