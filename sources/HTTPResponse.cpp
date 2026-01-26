#include "HTTPResponse.hpp"

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
	if (filePath.ends_with(".html"))
		return "text/html";
	else if (filePath.ends_with(".css"))
		return "text/css";
	else if (filePath.ends_with(".js"))
		return "application/javascript";
	else if (filePath.ends_with(".png"))
		return "image/png";
	else if (filePath.ends_with(".jpg") || filePath.ends_with(".jpeg"))
		return "image/jpeg";
	else
		return "text/plain";
}

std::string HTTPResponse::setDate()
{
	auto now = std::chrono::system_clock::now();
	time_t currentTime = std::chrono::system_clock::to_time_t(now);
	std::stringstream ss;
	ss << std::ctime(&currentTime);
	return ss.str().end()[-1] == '\n' ? ss.str().substr(0, ss.str().length() - 1) : ss.str();
}

// Updated the buildresponse to create the correct path and checking if it exists.
// 
std::string HTTPResponse::buildResponse(HTTPRequest request, const ServerParse& server)
{
	protocolVersion = request.protocolVersion;
	
	headers.clear();
	body.clear();
	updateForHTTPState(HTTPState::Ok);
	
	// --- Get Location info for index ---
	const LocationParse* location = server.get_best_location(request.resourcePath);
	std::string filePath;
	
	if (!location)
	{
		updateForHTTPState(HTTPState::NotFound);
		return parseResponseStr(request, "");
	}

	// --- Build initial path ---
	filePath = server.build_filesystem_path(request.resourcePath);
	if (filePath.empty())
	{
		updateForHTTPState(HTTPState::NotFound);
		return parseResponseStr(request, "");
	}

	if (server.is_directory(filePath))
	{
		if (!location->index.empty())
		{
			filePath = server.joinPaths(filePath, location->index);
		}
		else
		{
			// No index -> forbidden
			updateForHTTPState(HTTPState::Forbidden);
			return parseResponseStr(request, filePath);
		}
	}

	if (!server.file_exists(filePath))
	{
		updateForHTTPState(HTTPState::NotFound);
		return parseResponseStr(request, filePath);
	}

	// Else, filePath exists
	std::ifstream file(filePath);
	std::stringstream buffer;
	buffer << file.rdbuf();
	body = buffer.str();
	file.close();
	std::cout << "File found and opened successfully" << std::endl;

	return parseResponseStr(request, filePath);
}

// Perhaps need to check if a file was actually created/updated abd set to state created(201)?
std::string HTTPResponse::parseResponseStr(const HTTPRequest request, std::string filePath)
{
	switch (request.method)
	{
		case HTTPMethod::GET:
			break;
		case HTTPMethod::POST:
			break;
		case HTTPMethod::PUT:
			break;
		case HTTPMethod::DELETE:
			updateForHTTPState(HTTPState::NoContent);
			break;
		case HTTPMethod::HEAD:
			clearBody();
			break;
		case HTTPMethod::UNSUPPORTED:
			updateForHTTPState(HTTPState::MethodNotAllowed);
			break;
		default:
			if (statusCode == "200")
				updateForHTTPState(HTTPState::NotImplemented);
			break;
	}

	headers["Content-Length"] = std::to_string(body.size());
	headers["Server"] = "Webserv_Didi_and_Goksu";
	headers["Connection"] = "keep-alive";
	headers["Date"] = setDate();


	std::string response =
		protocolVersionToString(request.protocolVersion) + " " +
		statusCode + " " + reasonPhrase + "\r\n";

	for (const auto &h : headers)
		response += h.first + ": " + h.second + "\r\n";

	response += "\r\n" + body;

	return response;
}

void HTTPResponse::clearBody()
{
	body.clear();
	headers["Content-Length"] = "0";
	headers["Content-Type"] = "";
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
		headers["Content-Length"] = std::to_string(body.length());
	}
}