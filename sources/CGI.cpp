#include "HTTPResponse.hpp"
#include "CGI.hpp"

void HTTPResponse::handleCGI(const HTTPRequest& request, const std::string& filePath, const LocationParse& location)
{
	// 1. Create pipes to communicate with the CGI process
	// 2. Fork a child process (to run the CGI script)
	// 3. set env variables
	// 4. Execute the CGI script in the child process
	// 5. Send the request body to the CGI (POST only)
	// 6. Read CGI output (headers + body)
	// 7. return output as HTTP response
	// 8. when GET request, we can also pass query parameters in the env variables (QUERY_STRING)
	// 9. Handle errors (script not found, exec failure, timeout) + return appropriate HTTP error pages
	// 10. Security: ensure the CGI script is within the server root and has appropriate permissions (not world-writable, etc.)
	// We already checked if extension is .py, and if it's is_cgi.
}