#include <string>
#include <map>
#include <sstream>
#include <algorithm>
#include <exception>
#include <iostream>
#include "HTTPCommon.hpp"

#pragma once

class HTTPResponse{
public:
	// from status line
    std::string protocolVersion;
	std::string statusCode;
	std::string reasonPhrase;

	// from headers
    std::map<std::string, std::string> headers;

	// from body
    std::string body;

	HTTPResponse() = default;
	HTTPResponse(const HTTPResponse& other) = default;
	HTTPResponse& operator=(const HTTPResponse& other) = default;
	~HTTPResponse() = default;

    bool parseResponse(const std::string& raw);
	void printResponse() const;
	bool isValidProtocolVersion(const std::string& protocolVersion) const;
	bool isValidStatusCode(const std::string& statusCode) const;
	bool isValidReasonPhrase(const std::string& reasonPhrase) const;
	bool isValidBody(const std::string& body) const;
	bool isCRLF(const std::string& line) const;
	const std::string cleanWhiteSpace(std::string line);

	class HTTPResponseException: public std::exception
	{
		private:
			std::string msg;
		public:
			HTTPResponseException(const std::string& message) : msg(message) {}
			virtual const char* what() const noexcept override
			{
				return msg.c_str();
			}
	};
};