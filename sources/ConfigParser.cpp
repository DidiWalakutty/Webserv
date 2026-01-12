#include "../includes/ConfigParser.hpp"

ConfigParser::ConfigParser() {}

ConfigParser::~ConfigParser() {}

bool ConfigParser::parseConfigFile(const std::string& file)
{
	// --- Clear previous _servers ---
	_servers.clear();

	// -- Check file validity and if possible to open file ---
	if (isConfFile(file) == false || isEmptyFile(file) == true)
		return false;
	
	// --- Open file ---
	std::ifstream infile(file);

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
	size_t servercount = 0;
	while (currentLine < processedLines.size())
	{
		if (processedLines[currentLine] == "server {")
		{
			++servercount;
			ServerConfig server = parseServerBlock(processedLines, currentLine);
			if (validateServerConfig(server)) // in here, validate LocationsConfig as well
				_servers.push_back(server);
			else
			{
				// or print the error in validateserverconfig
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

				// --- Handle key-value pairs ---
				if (key == "server_name")
					server.serverName = value;
				else if (key == "host")
					server.host = value;
				else if (key == "port")
					server.port = std::stoi(value);
				else if (key == "root")
					server.root = value;
				else if (key == "index")			// directory index filename
					server.index = value;
				else if (key == "autoindex")		// directory listing on/off
				{
					if (value == "on")
						server.autoIndex = true;
					else if (value == "off")
						server.autoIndex = false;
					else
						std::cerr << "Warning: Invalid autoindex value: '" << value << "' in server block at line: " << currentLine + 1 << std::endl;
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
					server.maxBodySize = std::stoul(value);
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
	// --- Apply default values to all parsed locations if empty ---
	for (size_t i = 0; i < locations.size(); ++i)
	{
		if (locations[i].root.empty())
			locations[i].root = server.root;
		if (locations[i].index.empty())
			locations[i].index = server.index;
		if (locations[i].allowedMethods.empty())
			locations[i].allowedMethods = server.allowedMethods;
		if (!locations[i].autoIndex)
			locations[i].autoIndex = server.autoIndex;
		if (!locations[i].uploadEnabled)
			locations[i].uploadEnabled = false;
		if (!locations[i].is_cgi)
			locations[i].is_cgi = false;
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
				location.autoIndex = (value == "on");	// if value is "on", set to true
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
					location.redirect.statusCode = std::stoi(tokens[0]);
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
