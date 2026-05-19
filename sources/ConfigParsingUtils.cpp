#include "../includes/ConfigParser.hpp"

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
	// --- Create a stringstream (which is)
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

std::string method_to_string(HTTPMethod m)
{
    switch (m)
    {
        case HTTPMethod::GET: return "GET";
        case HTTPMethod::POST: return "POST";
        case HTTPMethod::DELETE: return "DELETE";
		case HTTPMethod::PUT: return "PUT";
		case HTTPMethod::HEAD: return "HEAD";
		case HTTPMethod::PATCH: return "PATCH";
		default: return "UNSUPPORTED";
    }
}

static void print_methods(const std::vector<HTTPMethod>& methods)
{
    for (size_t i = 0; i < methods.size(); ++i)
    {
        std::cout << method_to_string(methods[i]);
        if (i + 1 < methods.size())
            std::cout << ", ";
    }
}

// Print LocationParse details
static void print_location(const LocationParse& loc)
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

    std::cout << "    uploadEnabled: ";
    if (loc.uploadEnabled)
        std::cout << "true" << "\n";
    else
        std::cout << "false\n";

    std::cout << "    allowed methods: ";
    print_methods(loc.allowedMethods);
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
    print_methods(server.allowedMethods);
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
        print_location(server.locations[i]);
    }
    std::cout << "=================================\n\n";
}