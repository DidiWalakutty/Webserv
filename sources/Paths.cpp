#include "ConfigParser.hpp"

// Checks if anything exists at that path and if a normal file (not directory)
bool ServerParse::file_exists(const std::string& path) const
{
	struct stat buffer;
	return (stat(path.c_str(), &buffer) == 0 && S_ISREG(buffer.st_mode));
}

bool ServerParse::is_directory(const std::string& path) const
{
	struct stat buffer;
	return (stat(path.c_str(), &buffer) == 0 && S_ISDIR(buffer.st_mode));
}

// Returns a pointer to the best-matching LocationParse path.
// If file doesn't exist, example: '/nothing/hi', serving_errorpages will
// see it doesnt exist and return an appropriate error_page.
const LocationParse* ServerParse::get_best_location(const std::string& urlPath) const
{
	const LocationParse* bestMatch = nullptr;
	size_t longestMatch = 0;

	for (size_t i = 0; i < locations.size(); ++i)
	{
		const LocationParse& loc = locations[i];
		std::string locationPath = loc.path;		// location path from the config
		std::string requestPath = urlPath;			// incoming request path

		// Ensure both paths ends with '/' for directory matching
		if (!locationPath.empty() && locationPath.back() != '/')
			locationPath += '/';

		if (!requestPath.empty() && requestPath.back() != '/')
			requestPath += '/';

		// Compares up to the length of size of locationPath for the longest match of all loc.paths. 
		if (requestPath.compare(0, locationPath.size(), locationPath) == 0)
		{
			// Choose the longest matching path
			if (locationPath.size() > longestMatch)
			{
				longestMatch = locationPath.size();
				bestMatch = &loc;
			}
		}
	}
	return bestMatch;
}

const std::string* ServerParse::get_error_page(int errorCode) const
{
	std::map<int, std::string>::const_iterator it = errorPages.find(errorCode);
	if (it != errorPages.end())
		return &(it->second);
	return nullptr;
}

// Joins two parts together that normalizes the slash between them
std::string ServerParse::joinPaths(const std::string& root, const std::string& url) const
{
	if (root.empty())
		return (url);
	if (url.empty())
		return (root);

	bool rootEndSlash = root[root.size() - 1] == '/';
	bool urlStartSlash = url[0] == '/';
	
	// if root ends with /, and url starts with /, make sure we only have one /
	if (rootEndSlash && urlStartSlash)
		return (root + url.substr(1));

		// if url doesn't end with /, and root doesn't start with /, insert a /
	if (!rootEndSlash && !urlStartSlash)
		return (root + "/" + url);
	
	return (root + url);
}

/**
 *  @brief Decodes percent-encoded characters in a URL path.
 * 
 * @details Browsers encode special chars using percent encoding (space = %20).
 * 			When we request or delete a file that contains special chars, the
 * 			server receives the encoded form. This function decodes it to the OG chars
 * Example: "/upload/My%20File.txt" -> "/upload/My File.txt"
 */
std::string urlDecode(const std::string& str)
{
	std::string result;
	for (size_t i = 0; i < str.length(); i++)
	{
		if (str[i] == '%' && i + 2 < str.length())
		{
			std::string hex = str.substr(i + 1, 2);
			char decodedChar = static_cast<char>(std::stoi(hex, nullptr, 16));
			result += decodedChar;
			i += 2; // Skip the next two hex characters
		}
		else if (str[i] == '+')
			result += ' ';
		else
			result += str[i];
	}
	return result;
}

// request url: /images/logo.png, location path: /images.
// We want the full path: www/html/images/logo.png	
std::string ServerParse::build_filesystem_path(const std::string& reqPath) const
{
	std::string urlPath = urlDecode(reqPath);

	// Ensure path starts with '/'
	if (urlPath.empty())
		urlPath = "/";
	
	if (urlPath[0] != '/')
		urlPath = "/" + urlPath;

	// remove duplicate slashes '//' in path
	size_t pos = 0;
	while ((pos = urlPath.find("//", pos)) != std::string::npos)
		urlPath.replace(pos, 2, "/");

	// Reject forbidden characters
	for (size_t i = 0; i < urlPath.size(); ++i)
	{
		// casting is needed to properly check for non-printable characters and avoid signed char issues
		unsigned char c = static_cast<unsigned char>(urlPath[i]);
		if (c == '\\' || c == '*' || c == '?' || c == '<' || c == '>' || c == '|' || c == ':' || c < 32)
		{
			std::cerr << "Error: forbidden character in path: " << c << std::endl;
			return (""); // reject path
		}
	}

	// Reject directory traversal attempts
	if (urlPath.find("/../") != std::string::npos || urlPath.rfind("/..", urlPath.size() - 1) != std::string::npos)
	{
		std::cerr << "Error: directory traversal attempt detected" << std::endl;
		return ("");
	}
	
	// Find best matching location
	const LocationParse* location = get_best_location(urlPath);
	
	if (!location)
		return ("");
	
	// Remove the matching location prefix from the request path
	std::string urlRemainder = urlPath;
	if (urlPath.compare(0, location->path.size(), location->path) == 0)
		urlRemainder = urlPath.substr(location->path.size());
	else
		urlRemainder = urlPath;

	if (urlRemainder.empty())
		urlRemainder = "/";

	// Join paths safely
	std::string fullPath = joinPaths(location->root, urlRemainder);

	return fullPath;
}