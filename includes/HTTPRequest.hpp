#include "HTTPCommon.hpp"

#pragma once

/**
 * @class HTTPRequest
 * @brief Represents an HTTP request and provides parsing and validation methods.
 */
class HTTPRequest : public HTTPCommon
{
public:
	// from request line
	HTTPMethod method;
	std::string resourcePath;
	HTTPProtocolVersion protocolVersion;

	// from headers
	std::map<std::string, std::string> headers;

	// from body
	std::string body;

	HTTPRequest() = default;
	HTTPRequest(const HTTPRequest &other) = default;
	HTTPRequest &operator=(const HTTPRequest &other) = default;
	~HTTPRequest() = default;

	/**
	 * @brief Parses a raw HTTP request string and populates the request fields.
	 * @param raw The raw HTTP request string.
	 * @return True if parsing is successful, otherwise throws exception or returns false.
	 */
	bool parseRequest(const std::string raw);

	/**
	 * @brief Prints the HTTP request details to the standard output.
	 */
	void printRequest() const;

	/**
	 * @brief Checks if the HTTP method is valid.
	 * @param method The HTTP method as a string.
	 * @return True if valid, false otherwise.
	 */
	bool isValidMethod(const std::string method) const;

	/**
	 * @brief Checks if the resource path is valid.
	 * @param resourcePath The resource path string.
	 * @return True if valid, false otherwise.
	 */
	bool isValidResourcePath(const std::string resourcePath) const;

	/**
	 * @brief Checks if the protocol version is valid.
	 * @param protocolVersion The protocol version string.
	 * @return True if valid, false otherwise.
	 */
	bool isValidProtocolVersion(const std::string protocolVersion) const;

	/**
	 * @brief Checks if the body content is valid.
	 * @param body The body string.
	 * @return True if valid, false otherwise.
	 */
	bool isValidBody(const std::string body) const;

	/**
	 * @brief Checks if a line is considered a CRLF (empty or whitespace line).
	 * @param line The line string.
	 * @return True if CRLF, false otherwise.
	 */
	bool isCRLF(const std::string line) const;

	/**
	 * @class HTTPRequestException
	 * @brief Exception class for HTTP request parsing and validation errors.
	 */
	class HTTPRequestException : public std::exception
	{
	private:
		std::string msg;

	public:
		HTTPRequestException(const std::string message) : msg(message) {}
		virtual const char *what() const noexcept override
		{
			return msg.c_str();
		}
	};
};