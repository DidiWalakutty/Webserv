#include "HTTPResponse.hpp"
#include <chrono>
#include <ctime>
#include <sstream>
#include <fstream>

void HTTPResponse::printResponse() const {
	std::cout << "Protocol Version: " << protocolVersionToString(protocolVersion) << std::endl;
	std::cout << "Status Code: " << statusCode << std::endl;
	std::cout << "Reason Phrase: " << reasonPhrase << std::endl;
	std::cout << "Headers:" << std::endl;
	for (std::map<std::string, std::string>::const_iterator it = headers.begin(); it != headers.end(); ++it)
	{
		std::cout << "  " << it->first << ": " << it->second << std::endl;
	}
	if (!body.empty())
		std::cout << "==Body==\n" << body << std::endl;
}

HTTPResponse HTTPResponse::buildResponse(HTTPState status, HTTPRequest request)
{
	HTTPResponse response;
	HTTPMesage statusMessage = HTTPCommon::HTTPStatusMap.at(status);

	response.protocolVersion = stringToProtocolVersion(request.protocolVersion);
	response.statusCode = statusMessage.code;
	response.reasonPhrase = statusMessage.message;

	std::string filePath = request.resourcePath;
	if (filePath == "/") 
		filePath = "./html/home.html";
	std::cout << "Attempting to read file: " << filePath << std::endl;
	std::ifstream file(filePath);
	
	if (file.good()) {
		std::cout << "File found and opened successfully" << std::endl;
		std::stringstream buffer;
		buffer << file.rdbuf();
		response.body = buffer.str();
		file.close();
	} else {
		std::cout << "File not found, using status message" << std::endl;
		response.body = statusMessage.description;
	}

	response.headers["Content-Length"] = std::to_string(response.body.length());
	if (filePath.ends_with(".html")) 
        response.headers["Content-Type"] = "text/html";
    else if (filePath.ends_with(".css"))
        response.headers["Content-Type"] = "text/css";
    else if (filePath.ends_with(".js"))
        response.headers["Content-Type"] = "application/javascript";
	else if (filePath.ends_with(".png"))
		response.headers["Content-Type"] = "image/png";
	else if (filePath.ends_with(".jpg") || filePath.ends_with(".jpeg"))
		response.headers["Content-Type"] = "image/jpeg";
	else
		response.headers["Content-Type"] = "text/plain";

	response.headers["Connection"] = "keep-alive";
	response.headers["Server"] = "Webserv_Didi_Ferre_Goksu";
	
	auto now = std::chrono::system_clock::now();
	time_t currentTime = std::chrono::system_clock::to_time_t(now);
	std::stringstream ss;
	ss << std::ctime(&currentTime);
	response.headers["Date"] = ss.str().end()[-1] == '\n' ? ss.str().substr(0, ss.str().length() - 1) : ss.str();

	return response;
}

// to continue from patch response example
// c++ *.cpp && ./a.out ./http_messages/patch_response.txt 