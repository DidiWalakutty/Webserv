#include "../includes/ConfigParser.hpp"

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
			std::cerr << "Invalid character found in server name: '" << c << "'. Please avoid spaces and special characters." << std::endl;
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

static bool isValidHost(const std::string& host)
{
	if (host.empty())
	{
		std::cerr << "Host cannot be empty." << std::endl;
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
			std::cerr << "Host name - IP is invalid:\nHost is ID'd as a possible IP (digits and dots).\nIP contains 3 dots, no leading '0's in a block and no +/- operators." << std::endl;
			return false;
		}
		return true;
	}

	// Else, validate domain name
	if (host.size() > 42)
	{
		std::cerr << "Host name cannot be more than 42 characters" << std::endl;
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

static bool isValidLocationPath(const std::string& path)
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

static bool isValidRedirectTarget(const std::string& path)
{
	if (path.empty())
		return false;
	
	if (path.find("..") != std::string::npos)
		return false;
	
	if (path.find(' ') != std::string::npos)
		return false;

	if (path.find("http://") == 0 || path.find("https://") == 0)
		return true;

	return isValidLocationPath(path);
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

		if (!isValidRedirectTarget(path))
		{
			std::cerr << "Invalid syntax for Redirect Target: " << path << std::endl;
			return false;
		}
		++current;
	}
	return true;
} 

// Checks root syntax
static bool isValidRoot(const std::string& root)
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
static bool isValidIndex(const std::string& name)
{
	if (name.empty())
	return false;
	
	if (name.find('/') != std::string::npos)
	{
		std::cerr << "Index should not contain a '\'' character. It's not a path." << std::endl;
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
	return false;
}

// static bool isValidLocationMethodsCombo(const LocationConfig&loc, const ServerConfig& server)
// {

// }

// Checks all server + location paths on syntax.
// We'll send a true boolean if the file is a filename, and false for directory/root.
static bool validatePathsAndMethods(const ServerConfig& server)
{
	if (!isValidRoot(server.root))
	{
		std::cerr << "Invalid syntax for server root: " << server.root << std::endl;
		return false;
	}

	if (!isValidIndex(server.index))
	{
		std::cerr << "Invalid syntax for server index: " << server.index << std::endl;
		return false;
	}

	for (size_t i = 0; i < server.locations.size(); ++i)
	{
		const LocationConfig& loc = server.locations[i];
		if (!isValidLocationPath(loc.path))
		{
			std::cerr << "Invalid syntax for location path: " << loc.path << std::endl;
			return false;
		}

		if (!isValidRoot(loc.root))
		{
			std::cerr << "Invalid syntax for location root: " << loc.root << std::endl;
			return false;
		}

		if (!isValidIndex(loc.index))
		{
			std::cerr << "Invalid syntax for location index: " << loc.index << std::endl;
			return false;
		}
		
		if (!loc.redirect.targetURL.empty())
		{
			if (!isValidRedirectTarget(loc.redirect.targetURL))
			{
				std::cerr << "Invalid syntax for location's Redirect url: '" << loc.redirect.targetURL << std::endl;
				return false;
			}
		}
	}

	// if (!isValidLocationMethodsCombo(server))
	// 	return false;

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
	if (server.root.empty())
	{
		std::cerr << "Server root is required and cannot be empty." << std::endl;
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
		std::cerr << "max_body_size must be between " << MIN_CONFIG_BODY_SIZE << " and " << MAX_CONFIG_BODY_SIZE << " bytes" << std::endl;
		return false;
	}
	
	// --- Fix empty Location Inheritance + updated with valid Server config if empty ---
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
	}
	// Checks Syntax
	if (!validatePathsAndMethods(server))
		return false;
	
		// Check duplicates across servers.
		// // --- VALIDATE Location --- 
		// if (!validateLocationConfig(location))
		// {
		// 	std::cerr << "Invalid location at path: " << location.path << "in server: " << server.serverName << std::endl;
		// 	return false;
		// }
	return true;
}