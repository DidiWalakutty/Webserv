#include "HTTPResponse.hpp"

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

HTTPResponse HTTPResponse::buildResponse(HTTPState status, std::string version)
{
	HTTPResponse response;
	HTTPMesage statusMessage = HTTPCommon::HTTPStatusMap.at(status);

	response.protocolVersion = stringToProtocolVersion(version);
	response.statusCode = statusMessage.code;
	response.reasonPhrase = statusMessage.message;
	response.body = statusMessage.description;

	// Set default headers
	response.headers["Content-Length"] = std::to_string(response.body.length());
	response.headers["Content-Type"] = "text/plain";
	response.headers["Connection"] = "close";

	return response;
}

// to continue from patch response example
// c++ *.cpp && ./a.out ./http_messages/patch_response.txt 