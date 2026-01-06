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

// Checks if the file is empty by peeking at the first character
bool ConfigParser::isEmptyFile(const std::string& file) const
{
	std::ifstream infile(file);
	if (!infile.is_open())
	{
		std::cerr << "Error: Could not open config file: " << file << std::endl;
		return true;
	}
	if (infile.peek() == std::ifstream::traits_type::eof())
	{
		std::cerr << "Error: Config file is empty: " << file << std::endl;
		return true;
	}
	return false;
}

// Removes comments from a line (anything after '#' and/or '//')
void ConfigParser::removeComments(std::string& line)
{
	size_t hashPos = line.find('#');
	if (hashPos != std::string::npos)
		line.erase(hashPos);
	
	size_t slashPos = line.find("//");
	if (slashPos != std::string::npos)
		line.erase(slashPos);
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
std::vector<std::string> ConfigParser::splitByWhitespace(const std::string& line) const
{
	// Implementation goes here
}

// Splits a line by semicolon into tokens  -> parseServerBlock, parseLocationBlock
std::vector<std::string> ConfigParser::splitBySemicolon(const std::string& line) const
{
	// Implementation goes here
}


// printParsedConfig