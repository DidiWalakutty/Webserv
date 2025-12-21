#include "HTTPCommon.hpp"

const std::unordered_map<HTTPState, HTTPMesage> HTTPCommon::HTTPStatusMap = {
	{HTTPState::Continue, {"100", "Continue", "The server has received the request headers, and the client should proceed to send the request body"}},
	{HTTPState::SwitchingProtocols, {"101", "Switching Protocols", "The requester has asked the server to switch protocols"}},
	{HTTPState::EarlyHints, {"103", "Early Hints", "Used with the Link header to allow the browser to start preloading resources while the server prepares a response"}},	
	{HTTPState::Ok, {"200", "OK", "The request is OK (this is the standard response for successful HTTP requests)"}},
	{HTTPState::Created, {"201", "Created", "The request has been fulfilled, and a new resource is created"}},
	{HTTPState::Accepted, {"202", "Accepted", "The request has been accepted for processing, but the processing has not been completed"}},
	{HTTPState::NonAuthoritativeInformation, {"203", "Non-Authoritative Information", "The request has been successfully processed, but is returning information that may be from another source"}},	
	{HTTPState::NoContent, {"204", "No Content", "The request has been successfully processed, but is not returning any content"}},
	{HTTPState::ResetContent, {"205", "Reset Content", "The request has been successfully processed, but is not returning any content, and requires that the requester reset the document view"}},
	{HTTPState::PartialContent, {"206", "Partial Content", "The server is delivering only part of the resource due to a range header sent by the client"}},	
	{HTTPState::MultipleChoices, {"300", "Multiple Choices", "A link list. The user can select a link and go to that location. Maximum five addresses"}},
	{HTTPState::MovedPermanently, {"301", "Moved Permanently", "The requested page has moved to a new URL"}},
	{HTTPState::Found, {"302", "Found", "The requested page has moved temporarily to a new URL"}},
	{HTTPState::SeeOther, {"303", "See Other", "The requested page can be found under a different URL"}},
	{HTTPState::NotModified, {"304", "Not Modified", "Indicates the requested page has not been modified since last requested"}},
	{HTTPState::TemporaryRedirect, {"307", "Temporary Redirect", "The requested page has moved temporarily to a new URL"}},
	{HTTPState::PermanentRedirect, {"308", "Permanent Redirect", "The requested page has moved permanently to a new URL"}},
	{HTTPState::BadRequest, {"400", "Bad Request", "The server could not understand the request due to invalid syntax"}},
	{HTTPState::Unauthorized, {"401", "Unauthorized", "The client must authenticate itself to get the requested response"}},
	{HTTPState::PaymentRequired, {"402", "Payment Required", "This response code is reserved for future use"}},
	{HTTPState::Forbidden, {"403", "Forbidden", "The client does not have access rights to the content"}},
	{HTTPState::NotFound, {"404", "Not Found", "The server can not find the requested resource"}},
	{HTTPState::MethodNotAllowed, {"405", "Method Not Allowed", "The request method is known by the server but has been disabled and cannot be used"}},
	{HTTPState::NotAcceptable, {"406", "Not Acceptable", "The server cannot produce a response matching the list of acceptable values defined in the request's proactive content negotiation headers"}},
	{HTTPState::ProxyAuthenticationRequired, {"407", "Proxy Authentication Required", "The client must first authenticate itself with the proxy"}},
	{HTTPState::RequestTimeout, {"408", "Request Timeout", "The server would like to shut down this unused connection"}},
	{HTTPState::Conflict, {"409", "Conflict", "This response is sent when a request conflicts with the current state of the server"}},
	{HTTPState::Gone, {"410", "Gone", "The content has been permanently deleted from server, with no forwarding address"}},
	{HTTPState::LengthRequired, {"411", "Length Required", "The server rejected the request because the Content-Length header field is not defined and the server requires it"}},
	{HTTPState::PreconditionFailed, {"412", "Precondition Failed", "The client has indicated preconditions in its headers which the server does not meet"}},
	{HTTPState::RequestTooLarge, {"413", "Request Entity Too Large", "The request entity is larger than limits defined by server"}},
	{HTTPState::URITooLong, {"414", "Request-URI Too Long", "The URI requested by the client is longer than the server is willing to interpret"}},
	{HTTPState::UnsupportedMediaType, {"415", "Unsupported Media Type", "The media format of the requested data is not supported by the server"}},
	{HTTPState::RangeNotSatisfiable, {"416", "Range Not Satisfiable", "The range specified by the Range header field in the request cannot be fulfilled"}},
	{HTTPState::ExpectationFailed, {"417", "Expectation Failed", "The expectation indicated by the Expect request header field cannot be met by the server"}},
	{HTTPState::InternalServerError, {"500", "Internal Server Error", "The server has encountered a situation it doesn't know how to handle"}},
	{HTTPState::NotImplemented, {"501", "Not Implemented", "The request method is not supported by the server and cannot be handled"}},
	{HTTPState::BadGateway, {"502", "Bad Gateway", "The server, while acting as a gateway or proxy, received an invalid response from the upstream server"}},
	{HTTPState::ServiceUnavailable, {"503", "Service Unavailable", "The server is not ready to handle the request"}},
	{HTTPState::GatewayTimeout, {"504", "Gateway Timeout", "The server, while acting as a gateway or proxy, did not receive a timely response from the upstream server"}},
	{HTTPState::HTTPVersionNotSupported, {"505", "HTTP Version Not Supported", "The HTTP version used in the request is not supported by the server"}},
	{HTTPState::NetworkAuthenticationRequired, {"511", "Network Authentication Required", "The client needs to authenticate to gain network access"}}
};

HTTPMethod HTTPCommon::stringToMethod(const std::string method)
{
	if (method == "GET") return HTTPMethod::GET;
	else if (method == "POST") return HTTPMethod::POST;
	else if (method == "PUT") return HTTPMethod::PUT;
	else if (method == "DELETE") return HTTPMethod::DELETE;
	else if (method == "HEAD") return HTTPMethod::HEAD;
	else if (method == "PATCH") return HTTPMethod::PATCH;
	else if (method == "OPTIONS") return HTTPMethod::OPTIONS;
	else return HTTPMethod::UNSUPPORTED;
};

std::string HTTPCommon::methodToString(HTTPMethod method)
{
	switch (method)
	{
		case HTTPMethod::GET: return "GET";
		case HTTPMethod::POST: return "POST";
		case HTTPMethod::PUT: return "PUT";
		case HTTPMethod::DELETE: return "DELETE";
		case HTTPMethod::HEAD: return "HEAD";
		case HTTPMethod::PATCH: return "PATCH";
		case HTTPMethod::OPTIONS: return "OPTIONS";
		default: return "UNSUPPORTED";
	}
};

HTTPProtocolVersion HTTPCommon::stringToProtocolVersion(const std::string version)
{
	std::string v = cleanWhiteSpace(version);	

	if (v == "HTTP/0.9") return HTTPProtocolVersion::HTTP_0_9;
	else if (v == "HTTP/1.0") return HTTPProtocolVersion::HTTP_1_0;
	else if (v == "HTTP/1.1") return HTTPProtocolVersion::HTTP_1_1;
	else if (v == "HTTP/2.0") return HTTPProtocolVersion::HTTP_2_0;
	else if (v == "HTTP/3.0") return HTTPProtocolVersion::HTTP_3_0;
	else return HTTPProtocolVersion::UNSUPPORTED;
};

std::string HTTPCommon::protocolVersionToString(HTTPProtocolVersion version)
{
	switch (version)
	{
		case HTTPProtocolVersion::HTTP_0_9: return "HTTP/0.9";
		case HTTPProtocolVersion::HTTP_1_0: return "HTTP/1.0";
		case HTTPProtocolVersion::HTTP_1_1: return "HTTP/1.1";
		case HTTPProtocolVersion::HTTP_2_0: return "HTTP/2.0";
		case HTTPProtocolVersion::HTTP_3_0: return "HTTP/3.0";
		default: return "UNSUPPORTED";
	}
};

const std::string cleanWhiteSpace(std::string str)
{
	str.erase(0, str.find_first_not_of(" \t\r\n"));
	str.erase(str.find_last_not_of(" \t\r\n") + 1);
	return str;
}