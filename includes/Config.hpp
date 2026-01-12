#pragma once

#include <string>
#include <vector>
#include <map>

enum class HTTPMethod
{
	GET,
	POST,
	DELETE
};

/* Represents an HTTP redirect for a location */
struct Redirect
{
	int statusCode = 0;          /* HTTP status code for redirection (301, 302. 0 = none) */
	std::string targetURL;       /* URL to direct to */
};

/* Configuration related to a single URL path */
/* Each LocationConfig corresponds to a single "route" in your server */
struct LocationConfig
{
	std::string path;   		/* URL path for this location "/", "/upload"*, "/images" */
	std::string root;   		/* Filesystem/Root directory for this location to serve from "www/uploads"*/
	std::string index;  		/* Index file (default index.html). Default file to serve if URL is a directory */

	bool autoIndex = false; 	/* Enable or disable directory listing */
	bool is_cgi = false;   		/* True if this location executes CGI scripts */
	bool uploadEnabled = false; /* True if file uploads are allowed in this location */
	
	std::vector<HTTPMethod> allowedMethods; 	/* Allowed HTTP methods */
	Redirect redirect;							/* Optional redirect for this location (status code + target URL)*/

	bool method_allowed(HTTPMethod m) const;	/* Checks if a specific HTTP method is allowed */
};

/* Configuration for a server block */
/* Contains all the locations for a server */
struct ServerConfig
{
	std::string serverName;         		/* Server name for virtual hosting */
	std::string host;              			/* Server host (IP or domain)*/
	std::string root;                		/* Root directory for the server */
	int port;                       		/* Server port */

	std::string index;               		/* Index file for the server: filename */
	bool autoIndex = false;         		/* Enable or disable directory listing for the server */
	std::vector<HTTPMethod> allowedMethods; /* Default allowed HTTP methods for the server */
	size_t maxBodySize = 10485760; 			/* Max allowed body size in bytes for requests to this server: 10 mb. */

	std::map<int, std::string> errorPages; 	/* Custom error pages mapped by HTTP status code */
	std::vector<LocationConfig> locations; 	/* List of location configurations*/

	const LocationConfig* get_best_location(const std::string& urlPath) const; /* Returns best matching location for a URL path */
	const std::string* get_error_page(int errorCode) const; 					/* Returns custom error page for a given HTTP error code. */
};