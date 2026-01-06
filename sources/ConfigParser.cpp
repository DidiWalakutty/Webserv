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
	while (currentLine < processedLines.size())
	{
		if (processedLines[currentLine] == "server {")
		{
			ServerConfig server = parseServerBlock(processedLines, currentLine);
			if (validateServerConfig(server))
				_servers.push_back(server);
			else
			{
				std::cerr << "Error: Invalid server block starting at line: " << currentLine + 1 << std::endl;
				return false;
			}
		}
		else
		{
			std::cerr << "Error: Unexpected line outside server block at line: " << currentLine + 1 << std::endl;
			return false;
		}
		currentLine++;
	}

	// 4. Iterate through lines:
	//		- detect server blocks
	//		- call parseServerBlock()
	//		- within parseServerBlock(), detect location blocks
	//		- call parseLocationBlock()
	// 5. Validate parsed servers
	// 6. Store valid servers in _servers vector
	return true; // Return true if parsing was successful
}