#include "HTTPRequest.hpp"
#include "server.hpp"

HTTPRequest::HTTPRequest(Server *server) : server(server) {}

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
	// Rather than throwing, we send an error response for invalid method
	// if (!isValidMethod(methodStr))
	// 	throw HTTPRequestException("Invalid request format: does not start with valid HTTP method");

	// istringstream: treats a string like input we can read from line by line + token by token.
	std::istringstream stream(raw);
	if (stream.fail())
		throw HTTPRequestException("Failed to create stream from raw request");

	std::string requestLine;
	std::getline(stream, requestLine); // Reads until \n, stores it, moves forward.
	if (requestLine.empty() || isCRLF(requestLine))
		throw HTTPRequestException("Empty request line");

	// We determined method is valid, so we can safely convert it to enum for easier handling later.
	method = HTTPCommon::stringToMethod(methodStr);

	// --- Find Path ---
	size_t pathEnd = requestLine.find(' ', methodEnd + 1);
	if (pathEnd == std::string::npos)
		throw HTTPRequestException("Invalid request line: " + requestLine);
	resourcePath = requestLine.substr(methodEnd + 1, pathEnd - methodEnd - 1);
	if (!isValidResourcePath(resourcePath) || isCRLF(resourcePath))
		throw HTTPRequestException("Invalid resource path: " + resourcePath);

	// --- Split on query string at '?' if available ---
	// Query string is stored for CGI; ignored for non-CGI requests.
	size_t queryPos = resourcePath.find('?');
	if (queryPos != std::string::npos)
	{
		queryStringCGI = resourcePath.substr(queryPos + 1);
		resourcePath = resourcePath.substr(0, queryPos);
	}
	else
	{
		queryStringCGI.clear();;
	}
	if (!queryStringCGI.empty())
		std::cout << "Parsed query string: " << queryStringCGI << std::endl;
	std::cout << "Parsed resource path (without query): " << resourcePath << std::endl;

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
		if (isCRLF(line)) // detects header/body boundary
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

	bool hasContentLength = (headers.find("CONTENT-LENGTH") != headers.end());
	bool hasTransferEncoding = (headers.find("TRANSFER-ENCODING") != headers.end());

	if (hasContentLength)
	{
		if (headers["CONTENT-LENGTH"].empty() || !std::all_of(headers["CONTENT-LENGTH"].begin(), headers["CONTENT-LENGTH"].end(), ::isdigit))
			throw HTTPRequestException("Invalid Content-Length header value: " + headers["CONTENT-LENGTH"]);
		size_t contentLength = std::stoul(headers["CONTENT-LENGTH"]);
		if (contentLength > MAX_BODY_SIZE)
			throw HTTPRequestException("Content-Length exceeds maximum allowed size");
	}
	if (hasTransferEncoding)
	{
		if (headers["TRANSFER-ENCODING"] == "chunked")
		{
		}
		else if (headers["TRANSFER-ENCODING"] == "gzip")
		{
			throw HTTPRequestException("Gzip transfer encoding is not supported");
		}
		else
		{
			throw HTTPRequestException("Unsupported Transfer-Encoding: " + headers["TRANSFER-ENCODING"]);	
		}
	}

	// For methods that usually carry a payload, require explicit body framing.
	if ((method == HTTPMethod::POST || method == HTTPMethod::PUT || method == HTTPMethod::PATCH)
		&& !hasContentLength && !hasTransferEncoding)
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
	if (method == HTTPMethod::GET || method == HTTPMethod::HEAD || method == HTTPMethod::DELETE)
		std::cout << "The body is provided for method " << methodToString(method) << ", which typically does not have a body. This is allowed but unusual." << std::endl;
	if (headers.find("CONTENT-TYPE") == headers.end())
		std::cout << "No content-type header provided for body." << std::endl;
	else
	{
		auto contentType = headers.at("CONTENT-TYPE");
		std::cout << "Content-Type of body is: " << contentType << std::endl;
		std::string contentTypeLower = contentType;
		std::transform(contentTypeLower.begin(), contentTypeLower.end(), contentTypeLower.begin(), ::tolower);
		if (contentTypeLower.find("text") != std::string::npos || contentTypeLower.find("json") != std::string::npos || contentTypeLower.find("xml") != std::string::npos)
		{
			if (!std::all_of(body.begin(), body.end(), [](char c) { return std::isprint(static_cast<unsigned char>(c)) || std::isspace(static_cast<unsigned char>(c)); }))
			{
				std::cerr << "Warning: Body contains non-printable characters but Content-Type suggests text/json/html. This may indicate a mismatch." << std::endl;
				return false;
			}
		}
		else if (startsWith(contentTypeLower, "multipart/form-data"))
		{
			// Extract boundary from original (case-preserved) content type, since boundary values are case-sensitive
			size_t pos = contentType.find("boundary=");
			if (pos == std::string::npos)
				pos = contentTypeLower.find("boundary=");
    		if (pos == std::string::npos)
			{
				std::cerr << "Info: multipart/form-data content type specified but no boundary found." << std::endl;
				return true; // browsers and curl always include the boundary automatically
			}
        	std::string boundary = contentType.substr(pos + 9);

			// remove quotes if present
			if (!boundary.empty() && boundary[0] == '"')
			{
				size_t end = boundary.find('"', 1);
				if (end != std::string::npos)
					boundary = boundary.substr(1, end - 1);
			}
			if (boundary.empty())
			{
				std::cerr << "Warning: multipart/form-data content type specified but boundary is empty." << std::endl;
				return false;
			}

			std::string delim = "--" + boundary;
			std::string endDelim = delim + "--";

			// must contain at least one boundary
			if (body.find(delim) == std::string::npos)
			{
				std::cerr << "Warning: multipart/form-data body does not contain the required boundary." << std::endl;
				return false;
			}

			// must contain final boundary
			if (body.find(endDelim) == std::string::npos)
			{
				std::cerr << "Warning: multipart/form-data body does not contain the required final boundary." << std::endl;
				return false;
			}

			// basic structure: each part must have header/body separator
			pos = 0;
			while ((pos = body.find(delim, pos)) != std::string::npos)
			{
				size_t partStart = pos + delim.size();

				// final boundary → stop
				if (body.compare(partStart, 2, "--") == 0)
					break;

				// expect CRLF after boundary
				if (body.compare(partStart, 2, "\r\n") != 0)
				{
					std::cerr << "Warning: multipart/form-data part does not have expected CRLF after boundary." << std::endl;
					return false;
				}

				partStart += 2;

				size_t headerEnd = body.find("\r\n\r\n", partStart);
				if (headerEnd == std::string::npos)
				{
					std::cerr << "Warning: multipart/form-data part does not contain header/body separator." << std::endl;
					return false;
				}

				std::string headers = body.substr(partStart, headerEnd - partStart);

				// curl -X POST -F "file=@./test.txt" http://localhost:8080/upload
				// does not include Content-Disposition in the part headers, we will allow but log a warning

				// must have Content-Disposition
				if (headers.find("Content-Disposition:") == std::string::npos)
				{
					std::cerr << "Warning: multipart/form-data part does not contain required Content-Disposition header." << std::endl;
					return false;
				}

				pos = headerEnd + 4;
			}
		}
		else
		{
			std::cout << "Unrecognized Content-Type. No specific body validation applied." << std::endl;
		}
	}
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

void HTTPRequest::printRequest() const
{
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
