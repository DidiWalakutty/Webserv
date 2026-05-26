#include "HTTPResponse.hpp"

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
 * @brief Builds a complete HTTP error response for the given HTTP state.
 *
 * @details
 * - Initializes response context from the request
 * - Generates appropriate error page (custom or default)
 * - Applies standard headers (Date, Server, Connection)
 * - Formats final HTTP response string with status line, headers, and body
 */
std::string HTTPResponse::buildErrorResponse(const HTTPRequest& request, HTTPState state)
{
	protocolVersion = request.protocolVersion;
	headers.clear();
	body.clear();
	handleErrorPages(state);

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
 * @brief Builds a full HTTP response for a client request.
 *
 * @details
 * - Validates request constraints (URI length)
 * - Resolves requested resource using server configuration
 * - Handles directory logic (index files, permissions, autoindex)
 * - Verifies file existence and access rights
 * - Reads file content for GET/HEAD requests
 * - Delegates method-specific handling to parseResponseStr()
 *
 * @param request Incoming HTTP request
 * @return Fully constructed HTTP response string
 */
std::string HTTPResponse::buildResponse(HTTPRequest request)
{
	std::cerr << "In buildResponse(), with request: " << methodToString(request.method) << " " << request.resourcePath << std::endl;
	
	// --- URI length check (RFC 7230: 414 URI Too Long) ---
	if (request.resourcePath.size() > 8192)
	{
		protocolVersion = request.protocolVersion;
		headers.clear();
		body.clear();
		handleErrorPages(HTTPState::URITooLong);
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
	
	protocolVersion = request.protocolVersion;
	headers.clear();
	body.clear();
	updateForHTTPState(HTTPState::Ok);
	
	// --- Get Location info for index ---
	const LocationParse* location = serverParse.get_best_location(request.resourcePath);
	
	if (!location)
	{
		updateForHTTPState(HTTPState::NotFound);
		return parseResponseStr(request, "");
	}

	// --- Build initial path ---
	std::string filePath = serverParse.build_filesystem_path(request.resourcePath);
	// std::cerr << "Initial file path is: " << filePath << std::endl; 
	
	if (filePath.empty())
	{
		updateForHTTPState(HTTPState::NotFound);
		return parseResponseStr(request, "");
	}

	// --- Directory handling (method aware) ---
	bool isDir = serverParse.is_directory(filePath);

	if (isDir)	
	{
		// Only GET / HEAD use index
		if (request.method == HTTPMethod::GET || request.method == HTTPMethod::HEAD)
		{
			if (!location->index.empty())
			{
				filePath = serverParse.joinPaths(filePath, location->index);
				// std::cerr << "Index file found, filepath is: " << filePath << std::endl;
			}
			else
			{
				// No index -> forbidden
				updateForHTTPState(HTTPState::Forbidden);
				return parseResponseStr(request, filePath);
			}
		}
		// If any other method, we keep the original filePath we created with build_filesystem_path().
	}

	// --- Existence Check ---
	// For GET/HEAD the index file must exist. For POST/DELETE the path itself is
	// the target; if it's still a directory at this point (no index was appended),
	// skip the file_exists() test (which returns false for directories).
	bool skipExistenceCheck = isDir &&
		(request.method != HTTPMethod::GET && request.method != HTTPMethod::HEAD);
	if (!skipExistenceCheck && !serverParse.file_exists(filePath))
	{
		updateForHTTPState(HTTPState::NotFound);
		return parseResponseStr(request, filePath);
	}

	// --- Only read file for GET / HEAD ---
	if (request.method == HTTPMethod::GET || request.method == HTTPMethod::HEAD)
	{
		std::ifstream file(filePath.c_str(), std::ios::binary);
		if (!file.is_open())
		{
			updateForHTTPState(HTTPState::Forbidden);
			return parseResponseStr(request, filePath);
		}
		
		std::stringstream buffer;
		buffer << file.rdbuf();
		body = buffer.str();
		file.close();
		std::cout << "File found and opened successfully" << std::endl;
	}
	// For POST / PUT, body handling happens in their handleX functions.

	return parseResponseStr(request, filePath);
}

/**
 * @brief Dispatches request handling based on HTTP method and builds response.
 *
 * @details
 * - Routes request to method-specific handlers (GET, POST, DELETE, HEAD)
 * - Applies default handling for unsupported methods
 * - Sets response headers (Content-Type, Content-Length)
 * - Builds final HTTP response string
 *
 * @param request HTTP request
 * @param filePath Resolved filesystem path for the resource
 * @return Complete HTTP response string
 */
std::string HTTPResponse::parseResponseStr(const HTTPRequest request, const std::string filePath)
{
	switch (request.method)
	{
		case HTTPMethod::GET:
			handleGET(request, filePath);
			break;
		case HTTPMethod::POST:
			handlePOST(request, filePath);
			break;
		case HTTPMethod::PUT:
			break;
		case HTTPMethod::DELETE:
			handleDELETE(request, filePath);
			break;
		case HTTPMethod::HEAD:
			handleHEAD(request, filePath);
			// handleHEAD already clears the body and sets correct CONTENT-LENGTH/CONTENT-TYPE
			break;
		case HTTPMethod::UNSUPPORTED:
			handleErrorPages(HTTPState::MethodNotAllowed);
			break;
		default:
			handleErrorPages(HTTPState::NotImplemented);
			break;
	}

	// --- Common Headers ---
	// For HEAD: handleHEAD already set correct CONTENT-LENGTH (file size) and CONTENT-TYPE
	if (request.method != HTTPMethod::HEAD)
		headers["CONTENT-LENGTH"] = std::to_string(body.size());
	setStandardHeaders();

	// --- Set content type if not already set or empty ---
	if (headers.find("CONTENT-TYPE") == headers.end() || headers["CONTENT-TYPE"].empty())
		headers["CONTENT-TYPE"] = parseContentType(filePath);

	// --- Build HTTP Response String ---
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

/**
 * @brief Clears the response body and resets content headers.
 *
 * @details
 * Used mainly for HEAD responses and empty error states.
 */
void HTTPResponse::clearBody()
{
	body.clear();
	headers["CONTENT-LENGTH"] = "0";
	headers["CONTENT-TYPE"] = "";
}

/**
 * @brief Updates response state based on HTTP status.
 *
 * @details
 * - Maps HTTPState to status code and reason phrase
 * - Handles special cases like NoContent (clears body)
 * - Generates default error body for error states
 * - Updates Content-Length accordingly
 *
 * @param state HTTP status to apply
 */
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