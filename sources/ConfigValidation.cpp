#include "../includes/ConfigParser.hpp"

static bool isValidServerName(const std::string& name)
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

	// we will check here later
	if (path.find("www/") == 0)
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

		std::string filename = path.substr(ErrorPagePrefix.size());	// removes the prefix, so we keep 404.html: "www/errors/404.html"
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

static bool validateMethodsAndBools(const LocationParse& loc)
{
	bool hasGet = false;
	bool hasPost = false;
	bool hasDelete = false;

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

	if (loc.path.empty())
	{
		std::cerr << "Error: Location path cannot be empty" << std::endl;
		return false;
	}

	if (loc.path == "/upload")
	{
		if (!loc.uploadEnabled || !loc.autoIndex)
		{
			std::cerr << "Error: Location '/upload' must have uploadEnabled and autoindex set to true" << std::endl;
			return false;
		}
		if (!hasPost)
		{
			std::cerr << "Error: Location '/upload', must have atleast HTTPMethod POST" << std::endl;
			return false;
		}
	}

	if (loc.path == "/cgi-bin")
	{
		if (!loc.is_cgi)
		{
			std::cerr << "Error: Location '/cgi-bin' must have is_cgi set to true" << std::endl;
			return false;
		}
		if (!hasGet)
		{
			std::cerr << "Error: Location 'cgi-bin' must have atleast HTTPMethod GET" << std::endl;
			return false;
		}
	}

	if (loc.path == "/images")
	{
		if (!hasGet)
		{
			std::cerr << "Error: Location /images' must have atleast HTTPMethod GET" << std::endl;
			return false;
		}
	}

	if (loc.is_cgi)
	{
		if (hasDelete)
		{
			std::cerr << "Error: CGI location cannot allow DELETE method." << std::endl;
			return false;
		}
		if (!hasGet && !hasPost)
		{
			std::cerr << "Error: CGI Location must allow GET and/or POST." << std::endl;
			return false;
		}
		if (loc.uploadEnabled || loc.autoIndex)
		{
			std::cerr << "Error: CGI location cannot have uploadEnabled or autoIndex set to true." << std::endl;
			return false;
		}
	}

	if (loc.uploadEnabled)
	{
		if (!hasPost || !hasDelete)
		{
			std::cerr << "Error: UploadEnable requires both POST and DELETE methods" << std::endl;
			return false;
		}
		if (loc.is_cgi)
		{
			std::cerr << "Error: UploadEnabled cannot have is_cgi set to true." << std::endl;
			return false;
		}
	}

	if (!loc.redirect.targetURL.empty())
	{
		if (!loc.allowedMethods.empty() || loc.uploadEnabled || loc.autoIndex || loc.is_cgi || !loc.root.empty() || !loc.index.empty())
		{
			std::cerr << "Error: Redirect location should be empty, except for status code and target url" << std::endl;
			return false;
		}
	}

	if (loc.redirect.targetURL.empty() && loc.path == "/")
	{
		if (!hasGet)
		{
			std::cout << "Error: Location: " << loc.path << " should have HTTPMethod GET." << std::endl;
			return false;
		}
	}

	for (HTTPMethod m : loc.allowedMethods)
	{
		if (m != HTTPMethod::GET && m != HTTPMethod::POST && m != HTTPMethod::DELETE)
		{
			std::cerr << "Error: only HTTP methods GET, POST and DELETE are allowed." << std::endl;
			return false;
		}
	}

	return true;
}

// Checks all server + location paths on syntax.
// We'll send a true boolean if the file is a filename, and false for directory/root.
static bool validatePathsAndMethods(const ServerParse& server)
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
			if (!loc.allowedMethods.empty() || loc.uploadEnabled || loc.autoIndex || \
				loc.is_cgi || !loc.root.empty() || !loc.index.empty())
			{
				std::cerr << "Error: Redirection location should be empty, except for status code and target url" << std::endl;
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

static bool duplicateLocations(const ServerParse& server)
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
			loc.uploadEnabled = false;
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