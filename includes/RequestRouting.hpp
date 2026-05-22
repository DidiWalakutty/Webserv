#pragma once

#include "HTTPRequest.hpp"
#include "Config.hpp"
#include "HTTPCommon.hpp"
#include <string>
#include <sys/stat.h>
#include <unistd.h>

struct RouteResult
{
	const LocationParse* location;

	std::string		filePath;
	HTTPState 		state;
	HTTPMethod		method;
	bool			methodAllowed;

	bool 			isDirectory;
	bool 			isCGI;
	bool			autoIndex;
	
	bool			hasRedirect;
	std::string		redirectTarget;
	int				redirectCode;

	bool			exists;
	bool			readable;
	bool			writable;

	RouteResult();
};

class RequestRouting
{
	public:
		RequestRouting(const ServerParse& server);
		RouteResult route(const HTTPRequest& request);

	private:
		const ServerParse& _server;

		bool checkMethodAllowed(const LocationParse*, HTTPMethod method);
		bool resolvePath(const HTTPRequest& request, const LocationParse* location, std::string& outPath);
		HTTPState checkBasicValidity(const HTTPRequest& request, const std::string& path);
};