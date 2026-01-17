#include "../includes/ConfigParser.hpp"

ConfigParser::ConfigParser() {}

ConfigParser::~ConfigParser() {}

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
		std::cerr << "Error: COnfig File is empty: " << file << std::endl;
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
			ServerConfig server = parseServerBlock(processedLines, currentLine, parsing_error);
			if (validateServerConfig(server) && !parsing_error)
			{
				_servers.push_back(server);
				std::cerr << "--- Server nr: " << serverCount << " has been validated ---" << std::endl;
			}
			else
			{
				if (!parsing_error)
					std::cerr << "Error: Invalid server block starting at line: " << currentLine + 1 << std::endl;
				return false;
			}
		}
		else
		{
			std::cerr << "Error: Unexpected line: " << processedLines[currentLine] << " outside server block at line: " << currentLine + 1 << std::endl;
			return false;
		}
	}
	return true;
}

// This function also increments currentLine to the line after the server block
// Parses a server block and its nested location blocks
ServerConfig ConfigParser::parseServerBlock(const std::vector<std::string>& fileLines, size_t& currentLine, bool& parsing_error)
{
	ServerConfig server;
	std::vector<LocationConfig> locations; 	// Temporary storage for locations to update default location values if empty
	bool location_error = false;

	// -> loc. and serv. handle their own brackets 
	int brackOpen = 1;
	int brackclose = 0;
	++currentLine;

	while (currentLine < fileLines.size() && brackOpen > 0)
	{
		const std::string line = fileLines[currentLine]; // local copy we can modify
		// --- Location Parsing ---
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

			LocationConfig location = parseLocationBlock(fileLines, currentLine, location_error);
			locations.push_back(location);
			continue;
		}
		
		// --- Finds braces in each line and add ---
		size_t openCount = std::count(line.begin(), line.end(), '{');
		size_t closeCount = std::count(line.begin(), line.end(), '}');
		brackOpen += openCount;
		brackclose += closeCount;

		// --- Checks if we have more than one bracket of each ---
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
			// directive with key=value format
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
				std::cerr << "Error: Invalid string format. Missing closing ';' after value: " << value << " in Serverblock" << std::endl;
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
			else if (key == "autoindex")		// directory listing on/off
			{
				if (value == "true" || value == "on")
					server.autoIndex = true;
				else if (value == "false" || value == "off")
					server.autoIndex = false;
				else
				{
					std::cerr << "Warning: Invalid autoindex value: '" << value << "' in server block at line: " << currentLine + 1 << std::endl;
					server.autoIndex = false;
					std::cerr << "Auto Index was defaulted to " << server.autoIndex << std::endl;
				}
			}
			else if (key == "allowed_methods")
			{
				server.allowedMethods.clear();
				std::vector<std::string> tokens = splitByWhitespace(value);
				for (size_t i = 0; i < tokens.size(); ++i)
				{
					std::optional<HTTPMethod> method = stringToHTTPMethod(tokens[i]);
					if (method)
						server.allowedMethods.push_back(method.value());
					else
						std::cerr << "Warning: Invalid HTTP method: '" << tokens[i] << "' in server block at line: " << currentLine + 1 << std::endl;
					// !!!check what we want to do if invalid http method
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
					int errorCode = std::stoi(tokens[0]);
					std::string errorPath = tokens[1];
					server.errorPages[errorCode] = errorPath;
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
		++currentLine;
		if (brackclose == 1)
			break;
	}

	if (brackclose != 1)
	{
		std::cerr << "Error: Server block not closed before end of file" << std::endl;
		parsing_error = true;
	}

	if (location_error == true)
	parsing_error = true;

	server.locations = locations;

	return server; 
}

LocationConfig ConfigParser::parseLocationBlock(const std::vector<std::string>& fileLines, size_t& currentline, bool& location_error)
{
	LocationConfig location;

	// --- Extract the "location /path {" line ---
	const std::string line = fileLines[currentline];
	size_t pathStart = line.find("location") + 8; // gives what's after "location"
	size_t bracePos = line.find('{');
	location.path = line.substr(pathStart, bracePos - pathStart);
	trimWhitespace(location.path);
	
	int braceCount = 1;
	++currentline;
	
	while (currentline < fileLines.size())
	{
		const std::string line = fileLines[currentline];

		// --- Location Braces Count ---
		size_t openCount = std::count(line.begin(), line.end(), '{');
		size_t closeCount = std::count(line.begin(), line.end(), '}');
		braceCount += openCount;
		braceCount -= closeCount;
	
		if (braceCount > 1)
		{
			std::cerr << "Error: found extra '{' in location block at line: " << currentline + 1 << std::endl;
			location_error = true;
			return location;
		}
		if (closeCount > 1)
		{
			std::cerr << "Error: found extra '}' in location block at line: " << currentline + 1 << std::endl;
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
					std::cerr << "Warning: Invalid autoindex value: '" << value << "' in location block at line: " << currentline + 1 << std::endl;
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
					std::optional<HTTPMethod> method = stringToHTTPMethod(tokens[i]);
					if (method)
						location.allowedMethods.push_back(method.value());
					else
						std::cerr << "Warning: Invalid HTTP method: '" << tokens[i] << "' in location block at line: " << currentline + 1 << std::endl;					
						// what do we want to do???
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
						std::cerr << "Error: Return statuscode is not a number at line: " << currentline + 1 << std::endl;
						location_error = true;
						location.redirect.statusCode = 0;
					}
					catch (const std::out_of_range&) {
						std::cerr << "Error: Return status code is too big. Check line: " << currentline + 1 << std::endl;
						location_error = true;
						location.redirect.statusCode = 0;
					}
					location.redirect.targetURL = tokens[1];
				}
				else
				{
					std::cerr << "Error: Invalid return directive format in location block at line: " << currentline + 1 << std::endl;
					location_error = true;
				}
			}
			else if (key == "uploadEnabled")
			{
				if (value == "true")
					location.uploadEnabled = true;
				else if (value == "false")
					location.uploadEnabled = false;
				else
				{
					std::cerr << "Warning: Invalid choice for uploadEnabled in location block at line: " << currentline + 1 << std::endl;
					location.uploadEnabled = false;
					std::cerr << "UploadEnabled was defaulted to " << (location.uploadEnabled ? "true" : "false") << std::endl;
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
					std::cerr << "Warning: Invalid choice for is_cgi in location block at line: " << currentline + 1 << std::endl;
					location.is_cgi = false;
					std::cerr << "Is_cgi was defaulted to " << (location.is_cgi ? "true" : "false") << std::endl;
				}
			}
			else
			{
				std::cerr << "Error: Unknown directive: '" << key << "' in location block at line: " << currentline + 1 << std::endl;
				location_error = true;
			}
		}
		++currentline;

	}

	if (braceCount != 0)
	{
		std::cerr << "Error: Location block not closed before end of file block." << std::endl;
		location_error = true;
	}
	++currentline;	// move past closing }
	
	return location;
}
