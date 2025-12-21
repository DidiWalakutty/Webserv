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
	 * @param request The HTTP request object.
	 * @return The constructed HTTPResponse string.
	 */
	std::string buildResponse(HTTPRequest request);

	/**
	 * @brief Parses the requested resource path to determine the file path.
	 * @param request The HTTP request object.
	 * @return The file path corresponding to the requested resource.
	 */
	std::string parsePath(HTTPRequest request);

	/**
	 * @brief Determines the Content-Type based on the file extension.
	 * @param filePath The file path string.
	 * @return The corresponding Content-Type string.
	 */
	std::string parseContentType(const std::string filePath);

	/**
	 * @brief Sets the current date in HTTP date format.
	 * @return The formatted date string.
	 */
	std::string setDate();

	/**
	 * @brief Constructs the HTTP response string based on the request and status.
	 * @param request The HTTP request object.
	 * @param statusCode The HTTP status code.
	 * @param statusMessage The HTTP status message.
	 * @param body The response body content.
	 * @return The constructed HTTP response string.
	 */
	std::string parseResponseStr(const HTTPRequest request, std::string statusCode, HTTPMesage statusMessage, std::string body);


	/**
	 * @class HTTPResponseException
	 * @brief Exception class for HTTP response building and handling errors.
	 */
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