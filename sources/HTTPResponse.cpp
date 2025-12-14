#include "HTTPResponse.hpp"

bool HTTPResponse::parseResponse(const std::string& raw) 
{
	if (raw.empty())
		throw HTTPResponseException("Empty response string");
	
	std::istringstream stream(raw);
	if (stream.fail())
		throw HTTPResponseException("Failed to create stream from raw response");
	
	std::string statusLine;
	std::getline(stream, statusLine);
	if (statusLine.empty() || isCRLF(statusLine))
		throw HTTPResponseException("Empty status line");
	size_t protocolEnd = statusLine.find(' ');
	if (protocolEnd == std::string::npos)
		throw HTTPResponseException("Invalid status line: " + statusLine);
	protocolVersion = statusLine.substr(0, protocolEnd);
	if (!isValidProtocolVersion(protocolVersion) || isCRLF(protocolVersion))
		throw HTTPResponseException("Unsupported protocol version: " + protocolVersion);

	size_t codeEnd = statusLine.find(' ', protocolEnd + 1);
	if (codeEnd == std::string::npos)
		throw HTTPResponseException("Invalid status line: " + statusLine);
	statusCode = statusLine.substr(protocolEnd + 1, codeEnd - protocolEnd - 1);
	if (!isValidStatusCode(statusCode) || isCRLF(statusCode))
		throw HTTPResponseException("Invalid status code: " + statusCode);

	reasonPhrase = statusLine.substr(codeEnd + 1);
	if (reasonPhrase.empty() && ((protocolVersion == "HTTP/0.9") || (protocolVersion == "HTTP/1.0")))
		throw HTTPResponseException("Empty reason phrase in status line");
	if ((!isValidReasonPhrase(reasonPhrase) || isCRLF(reasonPhrase))  && ((protocolVersion == "HTTP/0.9") || (protocolVersion == "HTTP/1.0")))
		throw HTTPResponseException("Invalid reason phrase: " + reasonPhrase);
	
	std::string line;
	// Read header lines until an empty line (CRLF) that separates headers and body.
	while (std::getline(stream, line))
	{
		if (isCRLF(line))
			break;
		size_t colon = line.find(':');
		if (colon != std::string::npos)
		{
			std::string key = cleanWhiteSpace(line.substr(0, colon));
			std::string value = cleanWhiteSpace(line.substr(colon + 1));
			if (headers.find(key) != headers.end())
				throw HTTPResponseException("Duplicate header: " + key);
			headers[key] = value;
		}
		else
			throw HTTPResponseException("Invalid header line: " + line);
	}
	
	if (headers.find("Content-Length") != headers.end())
	{
		if (headers["Content-Length"].empty() || !std::all_of(headers["Content-Length"].begin(), headers["Content-Length"].end(), ::isdigit))
			throw HTTPResponseException("Invalid Content-Length header value: " + headers["Content-Length"]);
		size_t contentLength = std::stoul(headers["Content-Length"]);
		if (contentLength > MAX_BODY_SIZE)
			throw HTTPResponseException("Content-Length exceeds maximum allowed size");
	}
	if (stream.eof())
		return true;
	
	std::cout << "Reading body..." << std::endl;
	std::string bodyRaw;
	if (headers.find("Content-Length") != headers.end())
	{
		size_t contentLength = std::stoul(headers["Content-Length"]);
		if (contentLength > MAX_BODY_SIZE)
			throw HTTPResponseException("Content-Length exceeds maximum allowed size");

		bodyRaw.resize(contentLength);
		stream.read(&bodyRaw[0], static_cast<std::streamsize>(contentLength));
		std::streamsize readCount = stream.gcount();
		if (static_cast<size_t>(readCount) != contentLength)
		{
			// If stream reached EOF and fewer bytes are available, accept the shorter body
			if (stream.eof())
			{
				bodyRaw.resize(static_cast<size_t>(readCount));
				std::cerr << "Warning: Content-Length larger than available data. Provided: " << headers["Content-Length"]
						  << ", Actual: " << readCount << " — accepting shorter body." << std::endl;
			}
			else
			{
				throw HTTPResponseException("Content-Length does not match actual body size. Provided: " + headers["Content-Length"] + ", Actual: " + std::to_string(readCount));
			}
		}
	}
	else
	{
		std::ostringstream ss;
		ss << stream.rdbuf();
		bodyRaw = ss.str();
		if (bodyRaw.size() > MAX_BODY_SIZE)
			throw HTTPResponseException("Body size exceeds maximum limit");
	}
	body = bodyRaw;
	if (!isValidBody(body))
		throw HTTPResponseException("Invalid body content: " + body);
	return true;
}

bool HTTPResponse::isValidStatusCode(const std::string& statusCode) const
{
	if (statusCode.length() != 3 || !std::all_of(statusCode.begin(), statusCode.end(), ::isdigit))
		return false;
	int code = std::stoi(statusCode);
	return HTTPCommon::HTTPStatusMap.find(static_cast<HTTPState>(code)) != HTTPCommon::HTTPStatusMap.end();
}

bool HTTPResponse::isValidReasonPhrase(const std::string& reasonPhrase) const
{
	for (std::unordered_map<HTTPState, HTTPMesage>::const_iterator it = HTTPCommon::HTTPStatusMap.begin(); it != HTTPCommon::HTTPStatusMap.end(); ++it)
	{
		if (it->second.message == reasonPhrase)
			return true;
	}
	return false;
}

bool HTTPResponse::isValidProtocolVersion(const std::string& protocolVersion) const
{
	return HTTPCommon::stringToProtocolVersion(protocolVersion) != HTTPProtocolVersion::UNSUPPORTED;
}

bool HTTPResponse::isValidBody(const std::string& body) const
{
	return true;
}

// In the interest of robustness, servers SHOULD ignore any empty line(s) 
// received where a response-Line is expected. In other words, if the server is 
// reading the protocol stream at the beginning of a message and receives a CRLF first, 
// it should ignore the CRLF.
bool HTTPResponse::isCRLF(const std::string& line) const
{
	return line == "\r" || line == "" || line == "\n" || line == "\r\n" || line == "\n\r" || line == "\t";
}

const std::string HTTPResponse::cleanWhiteSpace(std::string line)
{
	line.erase(0, line.find_first_not_of(" \t\r\n"));
	line.erase(line.find_last_not_of(" \t\r\n") + 1);
	return line;
}

void HTTPResponse::printResponse() const {
	std::cout << "Protocol Version: " << protocolVersion << std::endl;
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

// to continue from patch response example
// c++ *.cpp && ./a.out ./http_messages/patch_response.txt 