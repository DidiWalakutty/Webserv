#include "../includes/ConfigParser.hpp"

// Returns a pointer to the best-matching LocationConfig path.
const LocationConfig* ServerConfig::get_best_location(const std::string& urlPath) const
{
	const LocationConfig* bestMatch = nullptr;
	size_t longestMatch = 0;

	for (size_t i = 0; i < locations.size(); ++i)
	{
		const LocationConfig& loc = locations[i];
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