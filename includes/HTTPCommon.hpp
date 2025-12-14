#include <unordered_map>
#include <string>
#include <vector>
#include <string_view>

#pragma once

#define BOLDRED    "\033[1;31m"
#define BOLDGREEN  "\033[1;32m"
#define BOLDYELLOW "\033[1;33m"
#define RESET      "\033[0m"

#define MAX_HEADER_SIZE 8192
#define MAX_BODY_SIZE 10485760 // 10 MB

// https://www.w3schools.com/tags/ref_httpmessages.asp

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

enum class MessageType
{
	unknown,
	HTTPRequest,
	HTTPResponse
};

struct HTTPMesage
{
	std::string code;
	std::string message;
	std::string description;

	std::string toString() const
	{
		return code + " " + message + "\r\n";
	}
};

enum class HTTPMethod
{
	GET, // Retrieves data from the server (e.g., loading a webpage).
	POST, // Sends data to the server.
	PUT, // Updates existing data on the server.
	DELETE, // Removes specified data from the server.
	HEAD, // Retrieves only headers of a resource, without the actual content.
	PATCH, // Applies partial modifications to a resource.
	OPTIONS, // Describes communication options available for a resource.
	UNSUPPORTED // For methods that are not recognized or supported.
};

enum class HTTPProtocolVersion
{
	HTTP_0_9,
	HTTP_1_0,
	HTTP_1_1,
	HTTP_2_0,
	HTTP_3_0,
	UNSUPPORTED
};

class HTTPCommon
{
	public:
		static const std::unordered_map<HTTPState, HTTPMesage> HTTPStatusMap;

		static HTTPMethod stringToMethod(const std::string& method);
		static HTTPProtocolVersion stringToProtocolVersion(const std::string& version);
		static std::string methodToString(HTTPMethod method);
		static std::string protocolVersionToString(HTTPProtocolVersion version);
};

MessageType getMessageType(std::vector<char> data);
