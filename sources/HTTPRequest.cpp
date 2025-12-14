#include "HTTPRequest.hpp"

// https://help.fortinet.com/fweb/600/Content/FortiWeb/fortiweb-admin/http_protocol_restraints.htm

// # print body (everything after the first empty line) and show byte count
// awk 'p{print} /^$/{p=1}' ./http_messages/put_request.txt | wc -c
// # print the Content-Length header
// grep -i 'Content-Length' ./http_messages/put_request.txt
// # replace Content-Length with the correct value
// sed -i 's/Content-Length: .*/Content-Length: 74/' ./http_messages/put_request.txt

bool HTTPRequest::parseRequest(const std::string& raw) 
{
	if (raw.empty())
		throw HTTPRequestException("Empty request string");
	
	std::istringstream stream(raw);
	if (stream.fail())
		throw HTTPRequestException("Failed to create stream from raw request");
	
	std::string requestLine;
	std::getline(stream, requestLine);
	if (requestLine.empty() || isCRLF(requestLine))
		throw HTTPRequestException("Empty request line");
	size_t methodEnd = requestLine.find(' ');
	if (methodEnd == std::string::npos)
		throw HTTPRequestException("Invalid request line: " + requestLine);
	method = requestLine.substr(0, methodEnd);
	if (!isValidMethod(method) || isCRLF(method))
		throw HTTPRequestException("Unsupported method: " + method);

	size_t pathEnd = requestLine.find(' ', methodEnd + 1);
	if (pathEnd == std::string::npos)
		throw HTTPRequestException("Invalid request line: " + requestLine);
	resourcePath = requestLine.substr(methodEnd + 1, pathEnd - methodEnd - 1);
	if (!isValidResourcePath(resourcePath) || isCRLF(resourcePath))
		throw HTTPRequestException("Invalid resource path: " + resourcePath);

	protocolVersion = requestLine.substr(pathEnd + 1);
	if (!isValidProtocolVersion(protocolVersion) || isCRLF(protocolVersion))
		throw HTTPRequestException("Unsupported protocol version: " + protocolVersion);
	
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
				throw HTTPRequestException("Duplicate header: " + key);
			headers[key] = value;
		}
		else
			throw HTTPRequestException("Invalid header line: " + line);
	}
	if (headers.find("Host") == headers.end())
		throw HTTPRequestException("Missing required Host header");
	if (headers.find("Content-Length") != headers.end())
	{
		if (headers["Content-Length"].empty() || !std::all_of(headers["Content-Length"].begin(), headers["Content-Length"].end(), ::isdigit))
			throw HTTPRequestException("Invalid Content-Length header value: " + headers["Content-Length"]);
		size_t contentLength = std::stoul(headers["Content-Length"]);
		if (contentLength > MAX_BODY_SIZE)
			throw HTTPRequestException("Content-Length exceeds maximum allowed size");
	}
	else if (method == "POST" || method == "PUT" || method == "PATCH")
	{
		throw HTTPRequestException("Missing required Content-Length header for method: " + method);
	}
	if (stream.eof())
		return true;
	
	std::cout << "Reading body..." << std::endl;
	std::string bodyRaw;
	if (headers.find("Content-Length") != headers.end())
	{
		size_t contentLength = std::stoul(headers["Content-Length"]);
		if (contentLength > MAX_BODY_SIZE)
			throw HTTPRequestException("Content-Length exceeds maximum allowed size");

		bodyRaw.resize(contentLength);
		stream.read(&bodyRaw[0], static_cast<std::streamsize>(contentLength));
		std::streamsize readCount = stream.gcount();
		if (static_cast<size_t>(readCount) != contentLength)
		{
			if (stream.eof())
			{
				bodyRaw.resize(static_cast<size_t>(readCount));
				std::cerr << "Warning: Content-Length larger than available data. Provided: " << headers["Content-Length"]
						  << ", Actual: " << readCount << " — accepting shorter body." << std::endl;
			}
			else
			{
				throw HTTPRequestException("Content-Length does not match actual body size. Provided: " + headers["Content-Length"] + ", Actual: " + std::to_string(readCount));
			}
		}
	}
	else
	{
		std::ostringstream ss;
		ss << stream.rdbuf();
		bodyRaw = ss.str();
		if (bodyRaw.size() > MAX_BODY_SIZE)
			throw HTTPRequestException("Body size exceeds maximum limit");
	}
	body = bodyRaw;
	if (!isValidBody(body))
		throw HTTPRequestException("Invalid body content: " + body);
	return true;
}

bool HTTPRequest::isValidMethod(const std::string& method) const
{
	return HTTPCommon::stringToMethod(method) != HTTPMethod::UNSUPPORTED;
}

bool HTTPRequest::isValidResourcePath(const std::string& resourcePath) const
{
	return !resourcePath.empty() && resourcePath[0] == '/';
}

bool HTTPRequest::isValidProtocolVersion(const std::string& protocolVersion) const
{
	return HTTPCommon::stringToProtocolVersion(protocolVersion) != HTTPProtocolVersion::UNSUPPORTED;
}

bool HTTPRequest::isValidBody(const std::string& body) const
{
	return true;
}

// In the interest of robustness, servers SHOULD ignore any empty line(s) 
// received where a Request-Line is expected. In other words, if the server is 
// reading the protocol stream at the beginning of a message and receives a CRLF first, 
// it should ignore the CRLF.
bool HTTPRequest::isCRLF(const std::string& line) const
{
	return line == "\r" || line == "" || line == "\n" || line == "\r\n" || line == "\n\r" || line == "\t";
}

const std::string HTTPRequest::cleanWhiteSpace(std::string line)
{
	line.erase(0, line.find_first_not_of(" \t\r\n"));
	line.erase(line.find_last_not_of(" \t\r\n") + 1);
	return line;
}

void HTTPRequest::printRequest() const {
	std::cout << "Method: " << method << std::endl;
	std::cout << "Resource Path: " << resourcePath << std::endl;
	std::cout << "Protocol Version: " << protocolVersion << std::endl;
	std::cout << "Headers:" << std::endl;
	for (std::map<std::string, std::string>::const_iterator it = headers.begin(); it != headers.end(); ++it)
	{
		std::cout << "  " << it->first << ": " << it->second << std::endl;
	}
	if (!body.empty())
		std::cout << "==Body==\n" << body << std::endl;
}

// to continue from patch request example
// c++ *.cpp && ./a.out ./http_messages/patch_request.txt 