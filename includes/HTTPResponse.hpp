#include "HTTPCommon.hpp"
#include "HTTPRequest.hpp"
#include "ServerParse.hpp"
#include <algorithm>
#include <unistd.h>
#include <sys/stat.h>
#include <ctime>
#include <sstream>
#include <set>

#pragma once

// Unordered map for fast lookup of allowed extensions and their corresponding content-type
static const std::unordered_map<std::string, std::string> allowedExtensions = {
	{".txt", "text/plain"},
	{".html", "text/html"},
	{".htm", "text/html"},
	{".css", "text/css"},
	{".js", "application/javascript"},
	{".json", "application/json"},
	{".png", "image/png"},
	{".jpg", "image/jpeg"},
	{".jpeg", "image/jpeg"},
	{".gif", "image/gif"},
	{".pdf", "application/pdf"},
	{".zip", "application/zip"},
	{".svg", "image/svg+xml"}
};

// Set for quick lookup of forbidden extensions (potentially dangerous files)
static const std::set<std::string> forbiddenExtensions = {
	".exe", ".bat", ".cmd"
};

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
	HTTPResponse(const ServerParse &server);
	HTTPResponse(const HTTPResponse &other) = default;
	HTTPResponse &operator=(const HTTPResponse &other) = default;
	~HTTPResponse() = default;


	std::string buildResponse(HTTPRequest request);
	std::string buildErrorResponse(const HTTPRequest& request, HTTPState state);

private:

	// Handler functions
	ServerParse serverParse;	// field in class
	void handleGET(const HTTPRequest& request, const std::string& filePath);
	void handleHEAD(const HTTPRequest& request, const std::string& filePath);
	void handlePOST(const HTTPRequest& request, const std::string& filePath);
	void handleDELETE(const HTTPRequest& request, const std::string& filePath);
	void handleDirectoryRequest(const HTTPRequest& request, const LocationParse* loc, const std::string& filePath);
	void handleErrorPages(HTTPState state);

	// Helper functions
	void setStandardHeaders();
	bool serveInjectedPage(const std::string& filePath, const std::string& placeholder, const std::string& inject);
	std::string generateImagesGallery(const std::string& imagesDir);
	std::string generateAutoindex(const std::string& dirPath, const std::string& urlPath);
	std::string generateUploadList(const std::string& uploadDir, bool allowDelete);
	std::string generateUploadFilename(const std::string& prefix);
	bool checkGetAccess(const std::string& filePath);
	bool checkPostAccess(const std::string& filePath);
	bool checkCGIAccess(const std::string& filePath);
	bool checkDeleteAccess(const std::string& filePath);
	bool extractMultipartFile(const HTTPRequest& request, std::string& fileName, std::string& fileData, std::string& ext);
	std::string findExtension(const HTTPRequest& request, const std::string& fileData);
	HTTPState getRedirectState(int code) const;

	bool validateSize(const std::string& buffer, const std::string& filePath);
	void printResponse() const;
	std::string parseContentType(const std::string filePath);
	std::string setDate();

	std::string parseResponseStr(const HTTPRequest request, const std::string filePath);
	void clearBody();
	void updateForHTTPState(HTTPState state);

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