#include "HTTPResponse.hpp"
#include "RequestRouting.hpp"

HTTPResponse::HTTPResponse(const ServerParse& server)
	: serverParse(server)
{
}

void HTTPResponse::printResponse() const
{
	std::cout << "Protocol Version: " << protocolVersionToString(protocolVersion) << std::endl;
	std::cout << "Status Code: " << statusCode << std::endl;
	std::cout << "Reason Phrase: " << reasonPhrase << std::endl;
	std::cout << "Headers:" << std::endl;
	for (std::map<std::string, std::string>::const_iterator it = headers.begin(); it != headers.end(); ++it)
	{
		std::cout << "  " << it->first << ": " << it->second << std::endl;
	}
	if (!body.empty())
		std::cout << "==Body==\n"
				  << body << std::endl;
}

std::string HTTPResponse::parseContentType(const std::string filePath)
{
	size_t dot = filePath.find_last_of('.');
	if (dot == std::string::npos)
		return "text/plain";
	auto it = allowedExtensions.find(filePath.substr(dot));
	return (it != allowedExtensions.end()) ? it->second : "text/plain";
}

void HTTPResponse::setStandardHeaders()
{
	headers["SERVER"] = "Webserv_Didi_Goksu_and_Reinier";
	headers["CONNECTION"] = "keep-alive";
	headers["DATE"] = setDate();
}
std::string HTTPResponse::setDate()
{
	auto now = std::chrono::system_clock::now();
	time_t currentTime = std::chrono::system_clock::to_time_t(now);
	std::stringstream ss;
	ss << std::ctime(&currentTime);
	return ss.str().end()[-1] == '\n' ? ss.str().substr(0, ss.str().length() - 1) : ss.str();
}

/**
 * @brief Builds a complete HTTP error response for a given state.
 *
 * @details
 * Initializes the response, generates the appropriate error page (custom or default),
 * sets the required headers, and returns the final HTTP response string.
 *
 * @param request The original HTTP request (used for protocol/version info).
 * @param state The HTTP error state to generate a response for.
 * @return The fully formatted HTTP response string.
 */
std::string HTTPResponse::buildErrorResponse(const HTTPRequest& request, HTTPState state)
{
	protocolVersion = request.protocolVersion;
	headers.clear();
	body.clear();
	handleErrorPages(state);
	headers["CONTENT-LENGTH"] = std::to_string(body.size());

	setStandardHeaders();

	std::string response =
		protocolVersionToString(request.protocolVersion) + " " +
		statusCode + " " + reasonPhrase + "\r\n";

	for (const auto &h : headers)
		response += h.first + ": " + h.second + "\r\n";

	response += "\r\n" + body;

	return response;
}

/**
 * @brief Builds a complete HTTP response from a routed request.
 * 
 * This function initializes the response state and delegates method-specific behavior
 * to the appropriate handlers (GET, POST, DELETE, etc.).
 * 
 * RequestRouting::route() already validated the request (location matching, filesystem path,
 * redirect handling and method permissions).
 * 
 * It returns the final formatted HTTP response string to be sent back to the client.
 */
std::string HTTPResponse::buildResponse(const HTTPRequest& request, const RouteResult& route)
{
	std::cerr << "In buildResponse(), with request: " << methodToString(request.method) << " " << request.resourcePath << std::endl;
	
	// --- URI length check (RFC 7230: 414 URI Too Long) ---
	if (request.resourcePath.size() > MAX_HEADER_SIZE)
	{
		return buildErrorResponse(request, HTTPState::URITooLong);
	}
	
	// --- Initialize Response ---
	protocolVersion = request.protocolVersion;
	headers.clear();
	body.clear();

	// In case routing already determined an error state
	if (route.state != HTTPState::Ok)
	{
		handleErrorPages(route.state);
		return parseResponseStr(request, route);
	}
	
	return parseResponseStr(request, route);
}

/**
 * @brief Dispatches the request to the correct HTTP Method Handler based on the request method.
 *        Builds the final HTTP response string after handling the request.
 * 
 * Also applies common headers and content type logic after method-specific handling.
 * Assembles the final HTTP response string to include the status line, headers, and body (if applicable).
 * 
 * Will update Content-Length of the body after the method handler is called, since some handlers (e.g. POST) may modify the body content.
 * For HEAD requests, the body is cleared and Content-Length is set to the size of the body that would have been sent if it were a GET request.
 */
std::string HTTPResponse::parseResponseStr(const HTTPRequest& request, const RouteResult& route)
{
	switch (request.method)
	{
		case HTTPMethod::GET:
			handleGET(request, route);
			break;
		case HTTPMethod::POST:
			handlePOST(request, route);
			break;
		case HTTPMethod::DELETE:
			handleDELETE(request, route);
			break;
		case HTTPMethod::HEAD:
			handleHEAD(request, route);
			// handleHEAD already clears the body and sets correct CONTENT-LENGTH/CONTENT-TYPE
			break;
		case HTTPMethod::UNSUPPORTED:
			handleErrorPages(HTTPState::MethodNotAllowed);
			break;
		default:
			handleErrorPages(HTTPState::NotImplemented);
			break;
	}

	// --- Common Headers for ALL responses ---
	setStandardHeaders();

	// --- Set Content-Length ---
		if (request.method != HTTPMethod::HEAD)
		headers["CONTENT-LENGTH"] = std::to_string(body.size());

	// --- Set content type if handler didn't set it ---
	if (headers.find("CONTENT-TYPE") == headers.end() || headers["CONTENT-TYPE"].empty())
		headers["CONTENT-TYPE"] = parseContentType(route.filePath);

	// --- Build raw HTTP Response String ---
	std::string response =
		protocolVersionToString(request.protocolVersion) + " " +
		statusCode + " " + reasonPhrase + "\r\n";

	for (const auto &h : headers)
		response += h.first + ": " + h.second + "\r\n";

	if (request.method == HTTPMethod::HEAD)
		response += "\r\n";
	else
		response += "\r\n" + body;

	return response;
}

void HTTPResponse::clearBody()
{
	body.clear();
	headers["CONTENT-LENGTH"] = "0";
	headers["CONTENT-TYPE"] = "";
}

void HTTPResponse::updateForHTTPState(HTTPState state)
{
	HTTPMessage statusMessage = HTTPCommon::HTTPStatusMap.at(state);
	statusCode = statusMessage.code;
	reasonPhrase = statusMessage.message;

	if (state == HTTPState::NoContent)
	{
		clearBody();
	}
	else if (state == HTTPState::Ok)
	{
		// Body remains unchanged
	}
	else
	{
		body = statusCode + ": " + statusMessage.description;
		headers["CONTENT-LENGTH"] = std::to_string(body.length());
	}
}