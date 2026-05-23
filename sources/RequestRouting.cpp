#include "RequestRouting.hpp"
#include "utilities.hpp"

RouteResult::RouteResult() 
	:	location(NULL),
		state(HTTPState::Ok),
		method(HTTPMethod::GET),
		methodAllowed(false),
		isDirectory(false),
		possibleCGI(false),
		autoIndex(false),
		hasRedirect(false),
		redirectCode(0),
		exists(false),
		isReadable(false),
		isWritable(false),
		canDelete(false)
{
}

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

	// 2) Copy IndexFile and autoIndex settings
	if (result.location->index.empty())
		result.indexFile = _server.index;
	else
		result.indexFile = result.location->index;

	result.autoIndex = result.location->autoIndex;

	// 3) Check Redirect (if true, no need to check anything else))
	if (result.location->redirect.statusCode != 0)
	{
		result.hasRedirect = true;
		result.redirectTarget = result.location->redirect.targetURL;
		result.redirectCode = result.location->redirect.statusCode;
		result.state = HTTPCommon::getRedirectState(result.redirectCode);
		return result;
	}

	// 4) Method validation for location
	result.methodAllowed = checkMethodAllowed(result.location, request.method);

	if (!result.methodAllowed)
	{
		result.state = HTTPState::MethodNotAllowed;
		return result;
	}

	// 5) Build filesystem path
	result.filePath = _server.build_filesystem_path(request.resourcePath);

	if (result.filePath.empty())
	{
		result.state = HTTPState::BadRequest;
		return result;
	}

	// 6) Check if path exists and permissions
	result.exists = pathExists(result.filePath);
	if (result.exists)
	{
		result.readable = isReadable(result.filePath);
		result.writable = isWritable(result.filePath);
		result.isDirectory = _server.isDirectory(result.filePath);
	}

	// 7) Request-specific checks 
	// --- GET / HEAD ---
	if (request.method == HTTPMethod::GET || request.method == HTTPMethod::HEAD)
	{
		if (!result.exists)
		{
			result.state = HTTPState::NotFound;
			return result;
		}
		if (!result.readable)
		{
			result.state = HTTPState::Forbidden;
			return result;
		}
	}

	// --- DELETE ---
	if (request.method == HTTPMethod::DELETE)
	{
		if (!result.exists)
		{
			result.state = HTTPState::NotFound;
			return result;
		}
		if (result.writable)
		{
			result.state = HTTPState::Forbidden;
			return result;
		}
	}


	// --- CGI Candidate --- 
	result.possibleCGI = result.location->is_cgi &&
						 result.exists &&
						 !result.isDirectory &&
						 (request.method == HTTPMethod::GET || request.method == HTTPMethod::POST);

	result.state = HTTPState::Ok;

	return result;
}