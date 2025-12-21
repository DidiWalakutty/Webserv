#include "HTTPCommon.hpp"

#pragma once

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

	bool parseRequest(const std::string raw);
	void printRequest() const;
	bool isValidMethod(const std::string method) const;
	bool isValidResourcePath(const std::string resourcePath) const;
	bool isValidProtocolVersion(const std::string protocolVersion) const;
	bool isValidBody(const std::string body) const;
	bool isCRLF(const std::string line) const;

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