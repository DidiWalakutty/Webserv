#include "../includes/ConfigParser.hpp"

ConfigParser::ConfigParser() {}

ConfigParser::~ConfigParser() {}

bool ConfigParser::parseConfigFile(const std::string& file)
{
	// --- Clear previous _servers ---
	_servers.clear();

	// -- Check file validity and if possible to open file ---
	if (isConfFile(file) == false)
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

	// -- Detect Server and Location Blocks and Parse ---
	size_t currentLine = 0;
	size_t serverCount = 0;
	while (currentLine < processedLines.size())
	{
		if (processedLines[currentLine] == "server {")
		{
			++serverCount;
			ServerConfig server = parseServerBlock(processedLines, currentLine);
			if (validateServerConfig(server)) // passed by reference
			{
				_servers.push_back(server);
				std::cerr << "--- Server nr: " << serverCount << " has been validated ---" << std::endl;
			}
			else
			{
				std::cerr << "Error: Invalid server block starting at line: " << currentLine + 1 << std::endl;
				return false;
			}
		}
		else
		{
			std::cerr << "Error: Unexpected line: " << processedLines[currentLine] << " outside server block at line: " << currentLine + 1 << std::endl;
			return false;
		}
		currentLine++;
	}
	// 5. Validate parsed servers
	return true;
}

// This function also increments currentLine to the line after the server block
// Parses a server block and its nested location blocks
ServerConfig ConfigParser::parseServerBlock(const std::vector<std::string>& fileLines, size_t& currentLine)
{
	ServerConfig server;
	std::vector<LocationConfig> locations; 	// Temporary storage for locations to update default location values if empty

	++currentLine;
	while (currentLine < fileLines.size() && fileLines[currentLine] != "}")
	{
		const std::string line = fileLines[currentLine]; // local copy we can modify
		// --- Check and parse location block ---
		if (line.find("location") == 0 && line.back() == '{')
		{
			LocationConfig location = parseLocationBlock(fileLines, currentLine);
			locations.push_back(location);
		}
		else
		{
			// --- Parse server directives ---
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
						std::cerr << "Invalid Port - not a number - at line " << currentLine + 1 << std::endl;
						server.port = 0;
					} 
					catch (const std::out_of_range&) {
						std::cerr << "Invalid Port - out of range - at line " << currentLine + 1 << std::endl;
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
					}
				}
				else if (key == "max_body_size")
				{
					try {
						server.maxBodySize = std::stoul(value);
					}
					catch (const std::invalid_argument&) {
						std::cerr << "Invalid Max_Body_size at line " << currentLine + 1 << std::endl;
						server.maxBodySize = 0;
					}
					catch (const std::out_of_range&) {
						std::cerr << "Invalid Max_Body_size - too big - at line " << currentLine + 1 << std::endl;
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
						std::cerr << "Warning: Invalid error_page directive format in server block at line: " << currentLine + 1 << std::endl;
				}
				else
					std::cerr << "Warning: Unknown directive: '" << key << "' in server block at line: " << currentLine + 1 << std::endl;
			}
		}
		++currentLine;
	}
	server.locations = locations;
	return server; 
}

LocationConfig ConfigParser::parseLocationBlock(const std::vector<std::string>& fileLines, size_t& currentline)
{
	LocationConfig location;

	// --- Extract the "location /path {" line ---
	const std::string line = fileLines[currentline];
	size_t pathStart = line.find("location") + 8; // gives what's after "location"
	size_t bracePos = line.find('{');
	location.path = line.substr(pathStart, bracePos - pathStart);
	trimWhitespace(location.path);
	++currentline;
	
	// --- Parse the info inside the location block ---
	while (currentline < fileLines.size() && fileLines[currentline] != "}")
	{
		const std::string line = fileLines[currentline];
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
					std::cerr << "Warning: Invalid autoindex at line " << currentline + 1 << std::endl;
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
						std::cerr << "Warning: Return statuscode is not a number at line: " << currentline + 1 << std::endl;
						location.redirect.statusCode = 0;
					}
					catch (const std::out_of_range&) {
						std::cerr << "Warning: Return status code is too big. Check line: " << currentline + 1 << std::endl;
						location.redirect.statusCode = 0;
					}
					location.redirect.targetURL = tokens[1];
				}
				else
					std::cerr << "Warning: Invalid return directive format in location block at line: " << currentline + 1 << std::endl;
			}
			else if (key == "uploadEnabled")
			{
				if (value == "true")
					location.uploadEnabled = true;
				else if (value == "false")
					location.uploadEnabled = false;
				else
					std::cerr << "Warning: Invalid choice for uploadEnabled in location block at line: " << currentline + 1 << std::endl;
			}
			else if (key == "is_cgi")
			{
				if (value == "true")
					location.is_cgi = true;
				else if (value == "false")
					location.is_cgi = false;
				else
					std::cerr << "Warning: Invalid choice for is_cgi in location block at line: " << currentline + 1 << std::endl;
			}
			else
				std::cerr << "Warning: Unknown directive: '" << key << "' in location block at line: " << currentline + 1 << std::endl;
		}
		++currentline;
	}
	return location;
}
