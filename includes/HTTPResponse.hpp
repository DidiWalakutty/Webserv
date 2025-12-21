#include <string>
#include <map>
#include <sstream>
#include <algorithm>
#include <exception>
#include <iostream>
#include "HTTPCommon.hpp"
#include "HTTPRequest.hpp"
#include <chrono>

#pragma once

class HTTPResponse: public HTTPCommon{
public:
	// from status line
    HTTPProtocolVersion protocolVersion;
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

	void printResponse() const;
	HTTPResponse buildResponse(HTTPState status, HTTPRequest request);

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