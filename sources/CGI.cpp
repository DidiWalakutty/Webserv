#include "HTTPResponse.hpp"
#include "CGI.hpp"

// void HTTPResponse::RunCGI(const HTTPRequest& request, const std::string& filePath, const LocationParse& location)
// {

	// 		- for a simple blocking/synchronous CGI, your current parameters are enough
	//		- for a real epoll-based CGI, you probably also need the client fd or another stored link to the client
	// 0. prepare CGI struct with needed info (script path, executable, env variables)

	// 1. Create 2 pipes to communicate with the CGI process
			// pipeToChild -> send POST body
			// pipeFromChild -> read CGI output (headers + body)
		
	// 2. Fork  a child process (to run the CGI script).
			// In the child process == CGI script, we will execute the CGI script.
			// In the parent process == server, we will send input (if POST) and read output from the CGI.

	// 3. Child process setup: 
			// if pid == 0 -> in child process: redirect stdin/stdout to pipes
			// close unused pipes
			// change directory to the CGI script's directory (b/o relative paths)

	// 4. Set environment variables (must be inside the child before execve)
			// Store them in tempENV as "KEY=VALUE" strings
			// Then convert to char* array for execve (env vector in CGI struct)

	// 5. Execute the CGI script in the child process
			// If it fails, exit(1) + handle error in parent process

	// 6. Parent Process:
			// Close unused pipe ends
			// If POST, write request body to pipeToChild + close (signals EOF to CGI)
			// Read CGI output from pipeFromChild until EOF (returns 0) (CGI process ends and closes)
			// Wait for child to finish (waitpid) and check exit status for errors. Prevents zombie processes

	// 7. Parse CGI output: seperate headers and body (split by \r\n\r\n)
			// Set CGI output headers in HTTP response headers
			// Set CGI output body as HTTP response body

	// --- !!! May have to store http repsonse in client output buffer until fully written !!! ---

// }