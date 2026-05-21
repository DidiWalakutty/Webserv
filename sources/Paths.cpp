#include "ConfigParser.hpp"
#include "utilities.hpp"

// Checks if anything exists at that path and if a normal file (not directory)
bool ServerParse::fileExists(const std::string& path) const
{
	struct stat buffer;
	return (stat(path.c_str(), &buffer) == 0 && S_ISREG(buffer.st_mode));
}

bool ServerParse::isDirectory(const std::string& path) const
{
	struct stat buffer;
	return (stat(path.c_str(), &buffer) == 0 && S_ISDIR(buffer.st_mode));
}

// Returns a pointer to the best-matching LocationParse path.
// If file doesn't exist, example: '/nothing/hi', serving_errorpages will
// see it doesnt exist and return an appropriate error_page.
/**
 * @brief Finds the best matching location block for a given URL path, using the longest-prefix matching.
 * 
 * The function iterates through all configured locations and checks if the URL path starts with the location's path.
 * It keeps track of the longest matching location path to ensure the most specific match is returned.
 * 
 * - Each configured location path is compared against the request path.
 * - The location with the longest matching prefix is selected as the best match.
 * 
 * Trailing slashes are normalized to ensure consistent matching ("/images" and "/images/" are treated the same).
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
 *  @brief Decodes percent-encoded URLS into normal characters in a URL path.
 * 
 * @details Browsers encode special chars using percent encoding (space = %20).
 * 			When we request or delete a file that contains special chars, the
 * 			server receives the encoded form. This function decodes it to the OG chars
 * Example: "/upload/My%20File.txt" -> "/upload/My File.txt"
 * 
 * Handles:
 * - %XX where XX are two hex digits, converts to the corresponding char
 * - + is converted to space (used mainly in query strings)
 * - Rejects invalid percent encodings (e.g., %ZZ) and null-byte injections (%00)
 * - Rejects null-byte injections (%00) to prevent security issues
 * 
 * If invalid encoding is detected, the function returns an empty string to signal that
 * the path should be rejected.
 */
std::string urlDecode(const std::string& str)
{
	std::string result;

	for (size_t i = 0; i < str.length(); i++)
	{
		if (str[i] == '%' && i < str.length())
		{
			if (str[i] == '%')
			{
				if (i + 2 > str.length() || !isHex(str[i + 1]) || !isHex(str[i + 2]))
					return ""; // reject invalid encoding
			
				std::string hex = str.substr(i + 1, 2);
				int value = std::stoi(hex, nullptr, 16);

				if (value == 0)
					return ""; // reject null-byte injection

				result += static_cast<char>(value);
				i += 2; // Skip the next two hex characters
			}
		}
		else if (str[i] == '+')
			result += ' '; // Convert '+' to space
		else
			result += str[i];
	}

	return result;
}

/**
 * @brief Normalizes a URL path by resolving '.' and '..' segments and removing redundant slashes.
 * 
 * Removes redundant/unsafe path segments, like:
 * - "." -> current directory
 * - ".." -> parent directory
 * - multiple slashes
 * 
 * ss = stringstream to split the path by '/' -> "/a/b../c" -> ["", "a", "b..", "c"]
 * item = current segment of ss
 * parts = stores valid segments after processing "." and ".."
 * 
 * This prevents directory traversal attacks, ensures a consistent path matching for routing and makes
 * path comparison reliable for matching against configured location paths.
 */
std::string normalizePath(const std::string& path)
{
	std::vector<std::string> parts;
	std::stringstream ss(path);
	std::string item;

	while (std::getline(ss, item, '/'))
	{
		if (item == "" || item == ".")
			continue; // Skip empty and current directory parts
		if (item == "..")
		{
			if (!parts.empty())
				parts.pop_back(); // Go up one directory
			continue;
		}
		parts.push_back(item);
	}

	// Reconstruct normalized path, starting by '/' (root)
	std::string result = "/";
	for (size_t i = 0; i < parts.size(); i++)
	{
		result += parts[i];
		if (i + 1 < parts.size())
			result += '/';
	}

	return result;
}

// request url: /images/logo.png, location path: /images.
// We want the full path: www/html/images/logo.png	

/**
 * @brief Converts a URL path into a safe filesystem path based on the server's root and location configuration.
 * 
 * This function maps an incoming HTTP request path to a real filesystem path.
 * 
 * 1. Url decoding: 
 * 		converts percent-encoded chars and rejects malformed/unsafe encodings.
 * 2. Path Normalization: 
 * 		prevents directory traversal and ensures consistent path format for matching.
 * 3. Location Matching: 
 * 		finds the best matching location block from the config and uses longest-prefix matching.
 * 4. Path Mapping:
 * 		removes the location prefix from the requested path and appends it to the location's root directory
 * 
 *  Final result:
 *   URL:  /images/logo.png
 *   root: /var/www/html
 *        → filesystem: /var/www/html/logo.png
 */
std::string ServerParse::build_filesystem_path(const std::string& reqPath) const
{
	std::string urlPath = urlDecode(reqPath);

	if (urlPath.empty())
		return ""; // reject invalid encoding

	// ensure leading slash before normalization
	if (urlPath[0] != '/')
		urlPath = "/" + urlPath;

	urlPath = normalizePath(urlPath);

	// Reject forbidden characters
	for (size_t i = 0; i < urlPath.size(); ++i)
	{
		// casting is needed to properly check for non-printable characters and avoid signed char issues
		unsigned char c = static_cast<unsigned char>(urlPath[i]);
		if (c == '\\' || c == '*' || c == '?' || c == '<' || c == '>' || c == '|' || c == ':' || c < 32)
		{
			std::cerr << "Error: forbidden character in path: " << c << std::endl;
			return ""; // reject path
		}
	}
	
	// Find best matching location
	const LocationParse* location = get_best_location(urlPath);
	
	if (!location)
		return "";
	
	// Remove location prefix (/images) from urlPath to get the remainder (/logo.png)
	std::string urlRemainder = urlPath;
	if (urlPath.compare(0, location->path.size(), location->path) == 0)
		urlRemainder = urlPath.substr(location->path.size());

	if (urlRemainder.empty())
		urlRemainder = "/";

	// Join paths safely
	std::string fullPath = joinPaths(location->root, urlRemainder);

	return fullPath;
}