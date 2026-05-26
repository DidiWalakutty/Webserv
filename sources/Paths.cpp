#include "ConfigParser.hpp"

// Checks if a path exists and is a regular file (not a directory)
bool ServerParse::file_exists(const std::string& path) const
{
	struct stat buffer;
	return (stat(path.c_str(), &buffer) == 0 && S_ISREG(buffer.st_mode));
}

// Checks if a path exists and is a directory
bool ServerParse::is_directory(const std::string& path) const
{
	struct stat buffer;
	return (stat(path.c_str(), &buffer) == 0 && S_ISDIR(buffer.st_mode));
}

/**
 * @brief Finds the best matching location block for a given URL path.
 *
 * @details
 * Selects the most specific (longest prefix match) location from the server
 * configuration. If multiple locations match, the most specific one wins.
 *
 * @param urlPath Requested URL path
 * @return Pointer to best matching LocationParse, or nullptr if none match
 */
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

/**
 * @brief Retrieves a custom error page for a given HTTP error code.
 *
 * @details
 * Looks up the error page path in the server configuration based on the error code.
 *
 * @return Pointer to the error page path, or nullptr if not found
 */
const std::string* ServerParse::get_error_page(int errorCode) const
{
	std::map<int, std::string>::const_iterator it = errorPages.find(errorCode);
	if (it != errorPages.end())
		return &(it->second);
	return nullptr;
}

/**
 * @brief Safely joins two filesystem path components.
 *
 * @details
 * Ensures exactly one '/' between root and URL segment while avoiding
 * duplicate or missing separators.
 *
 * @param root Base directory path
 * @param url Relative URL path
 * @return Normalized combined filesystem path
 */
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
 *  @brief Decodes a percent-encoded URL string.
 * 
 * @details 
 * Convert URL-encoded characters (%20 -> space) and handles '+' as space.
 * Rejects null-byte injection (%00) for security. 
 * Used to decode request paths before filesystem access.
 * 
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
			if (decodedChar == '\0')
				return ""; // reject null-byte injection
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

/**
 * @brief Converts a URL path into a safe filesystem path.
 *
 * @details
 * - Decodes URL encoding
 * - Normalizes slashes
 * - Rejects illegal characters and traversal attempts
 * - Matches request to best location block
 * - Builds final filesystem path using location root
 *
 * We want the full path: www/html/images/logo.png	
 * request url: /images/logo.png, location path: /images.
 *
 * @param reqPath Raw request path from HTTP request
 * @return Safe filesystem path, or empty string if invalid
 */
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