#include <unordered_map>
#include <string>
#include <vector>
#include <string_view>
#include <iostream>
#include <chrono>
#include <ctime>
#include <sstream>
#include <fstream>
#include <map>
#include <algorithm>
#include <exception>

#pragma once

#define BOLDRED "\033[1;31m"
#define BOLDGREEN "\033[1;32m"
#define BOLDYELLOW "\033[1;33m"
#define BOLDBLUE "\033[1;34m"
#define RESET "\033[0m"

#define MAX_HEADER_SIZE 8192	// 8 KB 
#define MAX_BODY_SIZE 10485760	// 10 MB

/**
 * @enum HTTPState
 * @brief Represents HTTP status codes and their meanings.
 */
enum HTTPState
{
	Continue = 100,
	SwitchingProtocols = 101,
	EarlyHints = 103,
	Ok = 200,
	Created = 201,
	Accepted = 202,
	NonAuthoritativeInformation = 203,
	NoContent = 204,
	ResetContent = 205,
	PartialContent = 206,
	MultipleChoices = 300,
	MovedPermanently = 301,
	Found = 302,
	SeeOther = 303,
	NotModified = 304,
	TemporaryRedirect = 307,
	PermanentRedirect = 308,
	BadRequest = 400,
	Unauthorized = 401,
	PaymentRequired = 402,
	Forbidden = 403,
	NotFound = 404,
	MethodNotAllowed = 405,
	NotAcceptable = 406,
	ProxyAuthenticationRequired = 407,
	RequestTimeout = 408,
	Conflict = 409,
	Gone = 410,
	LengthRequired = 411,
	PreconditionFailed = 412,
	RequestTooLarge = 413,
	URITooLong = 414,
	UnsupportedMediaType = 415,
	RangeNotSatisfiable = 416,
	ExpectationFailed = 417,
	InternalServerError = 500,
	NotImplemented = 501,
	BadGateway = 502,
	ServiceUnavailable = 503,
	GatewayTimeout = 504,
	HTTPVersionNotSupported = 505,
	NetworkAuthenticationRequired = 511
};

/**
 * @brief Struct to hold HTTP message details.
 *
 */
struct HTTPMessage
{
	std::string code;
	std::string message;
	std::string description;

	std::string toString() const
	{
		return code + " " + message + "\r\n";
	}
};

/**
 * @brief Enum for HTTP methods.
 *
 */
enum class HTTPMethod
{
	GET,		/** <@brief  Retrieves data from the server (e.g., loading a webpage). */
	POST,		/** <@brief  Sends data to the server. */
	PUT,		/** <@brief  Updates existing data on the server. */
	DELETE,		/** <@brief  Removes specified data from the server. */
	HEAD,		/** <@brief  Retrieves only headers of a resource, without the actual content. */
	UNSUPPORTED /** <@brief  For methods that are not recognized or supported. */
};

/**
 * @brief Enum for HTTP protocol versions.
 *
 */
enum class HTTPProtocolVersion
{
	HTTP_0_9,
	HTTP_1_0,
	HTTP_1_1,
	HTTP_2_0,
	HTTP_3_0,
	UNSUPPORTED
};

/**
 * @brief Common HTTP utilities and mappings.
 *
 */
class HTTPCommon
{
public:
	static const std::unordered_map<HTTPState, HTTPMessage> HTTPStatusMap;

	static HTTPMethod stringToMethod(const std::string method);
	static HTTPProtocolVersion stringToProtocolVersion(const std::string version);
	static std::string methodToString(HTTPMethod method);
	static std::string protocolVersionToString(HTTPProtocolVersion version);
};

/**
 * @brief Cleans leading and trailing whitespace from a string.
 *
 * @param str
 * @return const std::string
 */
std::string cleanWhiteSpace(std::string str);