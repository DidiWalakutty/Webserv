#include "RequestRouting.hpp"

bool RequestRouting::checkMethodAllowed(const LocationParse* loc, HTTPMethod method)
{
	for (size_t i = 0; i < loc->allowedMethods.size(); ++i)
	{
		if (loc->allowedMethods[i] == method)
			return true;
	}
	return false;
}

RouteResult RequestRouting::route(const HTTPRequest& request)
{
	RouteResult result;

	result.method = request.method;

	// 1) Find matching location
	result.location = _server.get_best_location(request.resourcePath);
	if (!result.location)
	{
		result.state = HTTPState::NotFound;
		return result;
	}

	// 2) Check Redirect (if true, no need to check anything else))
	if (result.location->redirect.statusCode != 0)
	{
		result.hasRedirect = true;
		result.redirectTarget = result.location->redirect.targetURL;
		result.redirectCode = result.location->redirect.statusCode;
		result.state = HTTPCommon::getRedirectState(result.redirectCode);
		return result;
	}

	// 3) Check if method is allowed for this location
	result.allowed = checkMethodAllowed(result.location, request.method);

	if (!result.allowed)
	{
		result.state = HTTPState::MethodNotAllowed;
		return result;
	}

	// 4) Build filesystem path
	result.filePath = _server.build_filesystem_path(request.resourcePath);

	if (result.filePath.empty())
	{
		result.state = HTTPState::BadRequest;
		return result;
	}

	// 5) Determine if path is a directory
	result.isDirectory = _server.isDirectory(result.filePath);

	// 6) Check if CGI (only for GET and POST)
	result.isCGI = result.location->is_cgi && (request.method == HTTPMethod::GET || request.method == HTTPMethod::POST);

	result.state = HTTPState::Ok;
	return result;
}