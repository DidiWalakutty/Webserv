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
