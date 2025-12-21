#include "HTTPCommon.hpp"
#include "HTTPRequest.hpp"

#pragma once

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

	void printResponse() const;
	HTTPResponse buildResponse(HTTPState status, HTTPRequest request);

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