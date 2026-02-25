#include "HTTPRequest.hpp"
#include "server.hpp"

HTTPRequest::HTTPRequest(Server* server) : server(server) {}

bool HTTPRequest::parseRequest(const std::string raw) 
{
	if (raw.empty())
		throw HTTPRequestException("Empty (raw) request string");
	
	// --- Find Method ---
	// Validate that request starts with a valid HTTP method (prevent binary garbage parsing)
	size_t methodEnd = raw.find(' ');
	if (methodEnd == std::string::npos)
		throw HTTPRequestException("Invalid request format: does not start with valid HTTP method");
	
	std::string methodStr = raw.substr(0, methodEnd);
	if (!isValidMethod(methodStr))
		throw HTTPRequestException("Invalid request format: does not start with valid HTTP method");
	
	// istringstream: treats a string like input we can read from line by line + token by token.
	std::istringstream stream(raw);
	if (stream.fail())
		throw HTTPRequestException("Failed to create stream from raw request");
	
	std::string requestLine;
	std::getline(stream, requestLine);	// Reads until \n, stores it, moves forward.
	if (requestLine.empty() || isCRLF(requestLine))
		throw HTTPRequestException("Empty request line");

	method = HTTPCommon::stringToMethod(methodStr);

	// --- Find Path ---
	size_t pathEnd = requestLine.find(' ', methodEnd + 1);
	if (pathEnd == std::string::npos)
		throw HTTPRequestException("Invalid request line: " + requestLine);
	resourcePath = requestLine.substr(methodEnd + 1, pathEnd - methodEnd - 1);
	if (!isValidResourcePath(resourcePath) || isCRLF(resourcePath))
		throw HTTPRequestException("Invalid resource path: " + resourcePath);

	// --- Find HTTP Protocol Version ---
	std::string strVersion = requestLine.substr(pathEnd + 1);
	if (!isValidProtocolVersion(strVersion) || isCRLF(strVersion))
		throw HTTPRequestException("Unsupported protocol version: " + strVersion);
	protocolVersion = HTTPCommon::stringToProtocolVersion(strVersion);

	// --- Validate Headers ---
	std::string line;
	// Read and validate HTTP headers line-by-line until the empty line that separates the body.
	while (std::getline(stream, line))
	{
		if (isCRLF(line))	// detects header/body boundary
			break;
		size_t colon = line.find(':');
		if (colon != std::string::npos)
		{
			std::string key = cleanWhiteSpace(line.substr(0, colon));
			std::string value = cleanWhiteSpace(line.substr(colon + 1));
			// Convert key to uppercase for case-insensitive comparison (HTTP headers are case-insensitive)
			std::transform(key.begin(), key.end(), key.begin(), ::toupper);
			// Checks if key already exists in map of Headers
			if (headers.find(key) != headers.end())
				throw HTTPRequestException("Duplicate header: " + key);
			headers[key] = value;
		}
		else if (!line.empty())
		{
			// Skip malformed header lines silently (lenient parsing for browser compatibility)
			// Don't throw - modern browsers may send headers in unexpected formats
			continue;
		}
	}
	if (headers.find("HOST") == headers.end())
		throw HTTPRequestException("Missing required Host header");
	
	if (headers.find("CONTENT-LENGTH") != headers.end())
	{
		if (headers["CONTENT-LENGTH"].empty() || !std::all_of(headers["CONTENT-LENGTH"].begin(), headers["CONTENT-LENGTH"].end(), ::isdigit))
			throw HTTPRequestException("Invalid Content-Length header value: " + headers["CONTENT-LENGTH"]);
		size_t contentLength = std::stoul(headers["CONTENT-LENGTH"]);
		if (contentLength > MAX_BODY_SIZE)
			throw HTTPRequestException("Content-Length exceeds maximum allowed size");
	}
	// --- !!! --- Patch or Delete needed??
	else if (methodStr == "POST" || methodStr == "PUT" || methodStr == "PATCH")
	{
		throw HTTPRequestException("Missing required Content-Length header for method: " + methodStr);
	}
	// If request has no body, we end here.
	if (stream.eof())
		return true;
	
	// --- Validate body ---
	std::cout << "Reading body..." << std::endl;
	std::string bodyRaw;
	if (headers.find("CONTENT-LENGTH") != headers.end())
	{
		size_t contentLength = std::stoul(headers["CONTENT-LENGTH"]);
		std::cout << "Content-Length: " << contentLength << std::endl;
		if (contentLength > MAX_BODY_SIZE)
			throw HTTPRequestException("Content-Length exceeds maximum allowed size");

		bodyRaw.resize(contentLength);
		stream.read(&bodyRaw[0], static_cast<std::streamsize>(contentLength));
		std::streamsize readCount = stream.gcount();
		std::cout << "Read " << readCount << " bytes of body." << std::endl;
		if (static_cast<size_t>(readCount) != contentLength)
		{
			if (stream.eof())
			{
				bodyRaw.resize(static_cast<size_t>(readCount));
				if (readCount > 0)
				{
					std::cerr << "Warning: Content-Length larger than available data. Provided: " << headers["CONTENT-LENGTH"]
							  << ", Actual: " << readCount << " — accepting shorter body." << std::endl;
				}
			}
			else
			{
				throw HTTPRequestException("Content-Length does not match actual body size. Provided: " + headers["CONTENT-LENGTH"] + ", Actual: " + std::to_string(readCount));
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
	// --- !!! --- Read Body currently empty, always returns true
	if (!isValidBody(body))
		throw HTTPRequestException("Invalid body content: " + body);
	return true;
}

bool HTTPRequest::isValidMethod(const std::string strMethod) const
{
	// Check if this is a valid HTTP method syntax (not whether it's allowed by server config)
	// Method validation against server/location config happens during request handling
	HTTPMethod method = HTTPCommon::stringToMethod(strMethod);
	return method != HTTPMethod::UNSUPPORTED;
}

bool HTTPRequest::isValidResourcePath(const std::string resourcePath) const
{
	return !resourcePath.empty() && resourcePath[0] == '/';
}

bool HTTPRequest::isValidProtocolVersion(const std::string protocolVersion) const
{
	return HTTPCommon::stringToProtocolVersion(protocolVersion) != HTTPProtocolVersion::UNSUPPORTED;
}

bool HTTPRequest::isValidBody(const std::string body) const
{
	// Current placeholder, later needs to check:
	// - content length
	// - chunked encoding
	// - max body size
	// - allowed for method
	return true;
}

// In the interest of robustness, servers SHOULD ignore any empty line(s) 
// received where a Request-Line is expected. In other words, if the server is 
// reading the protocol stream at the beginning of a message and receives a CRLF first, 
// it should ignore the CRLF.
bool HTTPRequest::isCRLF(const std::string line) const
{
	return line == "\r" || line == "" || line == "\n" || line == "\r\n" || line == "\n\r" || line == "\t";
}

void HTTPRequest::printRequest() const {
	std::cout << "Method: " << methodToString(method) << std::endl;
	std::cout << "Resource Path: " << resourcePath << std::endl;
	// std::cout << "Protocol Version: " << protocolVersionToString(protocolVersion) << std::endl;
	// std::cout << "Headers:" << std::endl;
	// for (std::map<std::string, std::string>::const_iterator it = headers.begin(); it != headers.end(); ++it)
	// {
	// 	std::cout << "  " << it->first << ": " << it->second << std::endl;
	// }
	// if (!body.empty())
	// 	std::cout << "==Body==\n" << body << std::endl;
}
