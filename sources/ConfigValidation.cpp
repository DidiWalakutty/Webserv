#include "../includes/ConfigParser.hpp"

// Underscores _ are invalid in official DNS hostnames.
static bool isValidServerName(const std::string& name)
{
	if (name.empty())
	{
		std::cerr << "Server name is empty" << std::endl;
		return false;
	}
	for (char c : name)
	{
		if (!isalnum(c) && c != '-' && c != '.' && c != '_')
		{
			std::cerr << "Invalid character found in server name: " << c << ". Please avoid spaces and special characters." << std::endl;
			return false;
		}
	}
	return true;
}

static bool isValidIPv4(const std::string& ip)
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
		// if block-length > 1, it can't start with 0.
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

static bool isValidHost(const std::string& host)
{
	if (host.empty())
	{
		std::cerr << "Host cannot be empty." << std::endl;
		return (false);
	}
	
	// Decide if host looks like an IPv4 (digits and dots only)
	bool looksIPv4 = true;
	for (char c : host)
	{
		if (!isdigit(c) && c != '.')
		{
			looksIPv4 = false;
			break;
		}
	}

	// If so, validate it
	if (looksIPv4)
	{
		if (!isValidIPv4(host))
		{
			std::cerr << "Host name - IP is invalid" << std::endl;
			return false;
		}
		return true;
	}

	// Else, validate domain name
	if (host.size() > 63)
	{
		std::cerr << "Host name cannot be more than 63 characters" << std::endl;
		return false;
	}
	for (char c : host)
	{
		if (!isalnum(c) && c != '.' && c != '-')
		{
			std::cerr << "Host name contains forbidden characters (spaces or special symbols)." << std::endl;
			return false;
		}
	}
	return true;
}

static bool isValidIndex(const std::string& index)
{
	if (!index.empty() && index.find('/') != std::string::npos)
	{
		std::cerr << "Index must be a filename, not a path" << std::endl;
		return false;
	}
	if (index.size() >= 5 && index.substr(index.size()-5) == ".html")
		return true;
	else
	{
		std::cerr << "Warning: index should end with .html";
		return false;
	}
	return false;	
}

// isFile: true = filename, false = directory/root
static bool isValidPath(const std::string& path, bool isFilename)
{

}

// Checks if all paths (root, index + locations) in the server are valid (not if they're existing)
// Checks per server every loop in parse configline
static bool validatePaths(const ServerConfig& server)
{

}

// Check each code and path pair.
// Iterator that points to first element and loops until the last.
// Each map element is a pair: current -> (404 -> "www/errors/404.html")
static bool isValidErrorPages(const std::map<int, std::string>& errorPages)
{
	const std::string ErrorPagePrefix = "www/errors/";

	std::map<int, std::string>::const_iterator current = errorPages.begin();

	while (current != errorPages.end())
	{
		int statusCode = current->first;		// map KEY -> HTTP status code
		std::string path = current->second;		// map VALUE -> file path

		if (statusCode < 400 || statusCode > 599)
		{
			std::cerr << "Invalid error_page status code: " << statusCode << 
			". Status code should be between 400 and 599." << std::endl;
			return false;
		}
		if (path.empty())
		{
			std::cerr << "Path for status code: " << statusCode << " is empty." << std::endl;
			return false;
		}
		
		if (path.substr(0, ErrorPagePrefix.size()) != ErrorPagePrefix)
		{
			std::cerr << "Error page must start with '" << ErrorPagePrefix << ": " << path << std::endl;
			return false;
		}

		std::string filename = path.substr(ErrorPagePrefix.size());	// removes the prefix, so we keep 404.html: "www/errors/404.html"
		std::string match = std::to_string(statusCode) + ".html";
		if (filename != match)
		{
			std::cerr << "Error page: filename doesn't match status code. Filename: " << filename << ", Status code: " << statusCode << std::endl;
			return false;
		}
		++current;
	}
	return true;
} 

bool ConfigParser::validateServerConfig(ServerConfig& server)
{
	// --- VALIDATE Server ---
	if (!isValidServerName(server.serverName))
		return (false);
	
	if (!isValidHost(server.host))
		return false;
	
	if (server.port < 1 || server.port > 65535)
	{
		std::cerr << "Invalid Port: " << server.port << " for server: " << server.serverName << std::endl;
		return false;
	}
	
	// Without a root, URL -> filesystem mapping is impossible.
	// Existence should be check at request time, not startup.
	if (server.root.empty())
	{
		std::cerr << "Server root is required and cannot be empty." << std::endl;
		return false;
	}
	
	// Index should have a default index.html.
	if (server.index.empty())
	{
		server.index = "index.html";
		std::cerr << "Index was empty in .conf file. Now set to default:" << server.index << std::endl;
		std::cout << "!!!Index file was set to a default" << std::endl;
	}
	if (!isValidIndex(server.index))
		return false;
	
	if (!isValidErrorPages(server.errorPages))
		return false;
	
	if (server.allowedMethods.empty())
	{
		server.allowedMethods.push_back(HTTPMethod::GET);
		std::cerr << "No (valid) allowed methods specified. Default set to GET." << std::endl;
		std::cout << "!!! Allowed methods was set to a default" << std::endl;
	}
	
	if (server.maxBodySize < MIN_CONFIG_BODY_SIZE || server.maxBodySize > MAX_CONFIG_BODY_SIZE)
	{
		std::cerr << "max_body_size must be between " << MIN_CONFIG_BODY_SIZE << " and " << MAX_CONFIG_BODY_SIZE << " bytes" << std::endl;
		return false;
	}
	
	// --- Fix Location Inheritance after parsing + validating Server ---
	for (size_t i = 0; i < server.locations.size(); ++i)
	{
		LocationConfig& location = server.locations[i];

		if (location.root.empty())
			location.root = server.root;
		if (location.index.empty())
			location.index = server.index;
		if (location.allowedMethods.empty())
			location.allowedMethods = server.allowedMethods;

		if (!location.autoIndex.has_value())
			location.autoIndex = server.autoIndex;
		if (!location.uploadEnabled.has_value())
			location.uploadEnabled = false;
		if (!location.is_cgi.has_value())
			location.is_cgi = false;

		// --- VALIDATE Location --- 
		// if (!validateLocationConfig(location))
		// {
		// 	std::cerr << "Invalid location at path: " << location.path << "in server: " << server.serverName << std::endl;
		// 	return false;
		// }

		// Check if all paths could be valid (sanity check, so no '//', or '..' etc)
		// if (!validatePaths(server))
		// 	return false;
		// Check duplicates across servers.
	}
	return true;
}