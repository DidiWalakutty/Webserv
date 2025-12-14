#include <string>
#include <map>
#include <sstream>
#include <algorithm>
#include <exception>
#include <iostream>
#include "HTTPCommon.hpp"

#pragma once

class HTTPRequest{
public:
	// from request line
    std::string method;
    std::string resourcePath;
    std::string protocolVersion;

	// from headers
    std::map<std::string, std::string> headers;

	// from body
    std::string body;

	HTTPRequest() = default;
	HTTPRequest(const HTTPRequest& other) = default;
	HTTPRequest& operator=(const HTTPRequest& other) = default;
	~HTTPRequest() = default;

    bool parseRequest(const std::string& raw);
	void printRequest() const;
	bool isValidMethod(const std::string& method) const;
	bool isValidResourcePath(const std::string& resourcePath) const;
	bool isValidProtocolVersion(const std::string& protocolVersion) const;
	bool isValidBody(const std::string& body) const;
	bool isCRLF(const std::string& line) const;
	const std::string cleanWhiteSpace(std::string line);

	class HTTPRequestException: public std::exception
	{
		private:
			std::string msg;
		public:
			HTTPRequestException(const std::string& message) : msg(message) {}
			virtual const char* what() const noexcept override
			{
				return msg.c_str();
			}
	};
};