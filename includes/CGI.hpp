#pragma once

struct CGI
{
	std::string scriptPath;				// full path to CGI script
	std::string cgiExecutable;  		// path to CGI executable
	pid_t pid;							// PID of CGI process

	int pipeToChild[2];					// server -> CGI
	int pipeFromChild[2];				// CGI -> server


	std::vector<std::string> tempEnv;	// temporary storage or env variables ("KEY=VALUE")
	std::vector<char*> env;				// final/converted env vector for execve

	std::string cgiOutput; 				// Data read from CGI (headers + body)
	std::string inputData; 				// POST body for CGI input (if applicable)

	bool cgiComplete = false; 			// Flag to indicate if CGI process is complete
	int cgiExitStatus = -1;				// Exit status of CGI process (for error handling)
};