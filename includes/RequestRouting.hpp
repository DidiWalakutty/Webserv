#pragma once

#include "HTTPRequest.hpp"
#include "Config.hpp"
#include "HTTPCommon.hpp"
#include <string>

struct RouteResult
{
	const LocationParse* location;

	std::string		filePath;
	HTTPMethod		method;
	HTTPState 		state;

	bool 			isDirectory;
	bool			allowed;
	
	bool 			isCGI;
	bool			hasRedirect;
	std::string		redirectTarget;
	int				redirectCode;


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