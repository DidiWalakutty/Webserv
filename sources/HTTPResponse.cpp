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

std::string HTTPResponse::parsePath(HTTPRequest request)
{
	std::string filePath = request.resourcePath;
	if (filePath.front() == '/')
	{
		filePath = "." + filePath;
	}
	if (filePath == "./" || filePath.empty())
	{
		filePath = "./html/home.html";
	}
	return filePath;
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

std::string HTTPResponse::buildResponse(HTTPRequest request)
{
	protocolVersion = request.protocolVersion;

	HTTPMesage statusMessage = HTTPCommon::HTTPStatusMap.at(HTTPState::NotFound);

	std::string filePath = parsePath(request);

	std::cout << "Attempting to read file: " << filePath << std::endl;
	std::ifstream file(filePath);

	if (file.good())
	{
		std::cout << "File found and opened successfully" << std::endl;
		std::stringstream buffer;
		buffer << file.rdbuf();
		body = buffer.str();
		file.close();
		statusMessage = HTTPCommon::HTTPStatusMap.at(HTTPState::Ok);
		statusCode = statusMessage.code;
		reasonPhrase = statusMessage.message;
	}
	else
	{
		std::cout << "File not found, using status message" << std::endl;
		statusMessage = HTTPCommon::HTTPStatusMap.at(HTTPState::NotFound);
		statusCode = statusMessage.code;
		reasonPhrase = statusMessage.message;
		body = statusCode + ": " + statusMessage.description;
	}

	headers["Content-Length"] = std::to_string(body.length());
	headers["Connection"] = "keep-alive";
	headers["Server"] = "Webserv_Didi_Ferre_Goksu";
	headers["Content-Type"] = parseContentType(filePath);
	headers["Date"] = setDate();

	std::string responseStr = parseResponseStr(request, statusCode, statusMessage, body);

	// This built response is going to be changed according to the method, searching for best practices
	std::cout << BOLDBLUE << "Built Response String for request: " << methodToString(request.method) << std::endl;
	std::cout << responseStr << RESET << std::endl;
	return responseStr;
}

std::string HTTPResponse::parseResponseStr(const HTTPRequest request, std::string statusCode, HTTPMesage statusMessage, std::string body)
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
		body.clear();
		headers["Content-Length"] = "0";
		headers["Content-Type"] = "";
		statusCode = "204";
		statusMessage = HTTPCommon::HTTPStatusMap.at(HTTPState::NoContent);
		body = statusCode + ": " + statusMessage.description;
		break;
	case HTTPMethod::HEAD:
		body.clear();
		headers["Content-Length"] = "0";
		headers["Content-Type"] = "";
		break;
	case HTTPMethod::UNSUPPORTED:
		statusCode = "405";
		statusMessage = HTTPCommon::HTTPStatusMap.at(HTTPState::MethodNotAllowed);
		body = statusCode + ": " + statusMessage.description;
		break;
	default:
		statusCode = "501";
		statusMessage = HTTPCommon::HTTPStatusMap.at(HTTPState::NotImplemented);
		body = statusCode + ": " + statusMessage.description;
		break;
	}

	headers["Content-Length"] = std::to_string(body.size());
	headers["Server"] = "Webserv_Didi_Ferre_Goksu";
	headers["Connection"] = "keep-alive";

	std::time_t now = std::time(NULL);
	char dateBuffer[100];
	std::strftime(dateBuffer, sizeof(dateBuffer),
				  "%a, %d %b %Y %H:%M:%S GMT", std::gmtime(&now));
	headers["Date"] = dateBuffer;

	std::string response =
		protocolVersionToString(request.protocolVersion) + " " +
		statusCode + " " + statusMessage.message + "\r\n";

	for (const auto &h : headers)
		response += h.first + ": " + h.second + "\r\n";

	response += "\r\n" + body;

	std::cout << BOLDBLUE
			  << "Built Response String for request: "
			  << methodToString(request.method)
			  << std::endl
			  << response
			  << RESET << std::endl;

	return response;
}
