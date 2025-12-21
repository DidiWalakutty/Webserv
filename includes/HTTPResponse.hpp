#include "HTTPCommon.hpp"
#include "HTTPRequest.hpp"

#pragma once

/**
 * @class HTTPResponse
 * @brief Represents an HTTP response and provides methods to build and print responses.
 */
class HTTPResponse : public HTTPCommon
{
public:
	HTTPProtocolVersion protocolVersion;
	std::string statusCode;
	std::string reasonPhrase;

	std::map<std::string, std::string> headers;

	std::string body;

	HTTPResponse() = default;
	HTTPResponse(const HTTPResponse &other) = default;
	HTTPResponse &operator=(const HTTPResponse &other) = default;
	~HTTPResponse() = default;

	/**
	 * @brief Prints the HTTP response details to the standard output.
	 */
	void printResponse() const;

	/**
	 * @brief Builds an HTTP response based on the status and request.
	 * @param status The HTTP status code (enum).
	 * @param request The HTTP request object.
	 * @return The constructed HTTPResponse object.
	 */
	HTTPResponse buildResponse(HTTPState status, HTTPRequest request);
	
	/**
	 * @class HTTPResponseException
	 * @brief Exception class for HTTP response building and handling errors.
	 */
	class HTTPResponseException : public std::exception

								  class HTTPResponseException : public std::exception
	{
	private:
		std::string msg;

	public:
		HTTPResponseException(const std::string &message) : msg(message) {}
		virtual const char *what() const noexcept override
		{
			return msg.c_str();
		}
	};
};