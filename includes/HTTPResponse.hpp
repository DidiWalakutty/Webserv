#include "HTTPCommon.hpp"
#include "HTTPRequest.hpp"
#include "Config.hpp"
#include <algorithm>
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
	".exe", ".php", ".sh", ".bat", ".cmd", ".py"
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

	/**
	 * @brief Builds an HTTP response based on the status and request.
	 * @param request The HTTP request object.
	 * @return The constructed HTTPResponse string.
	 */
	std::string buildResponse(HTTPRequest request);

private:

	// Handler functions
	ServerParse serverParse;	// field in class
	void handleGET(const HTTPRequest& request, const std::string& filePath);
	void handleHEAD(const HTTPRequest& request, const std::string& filePath);
	void handlePOST(const HTTPRequest& request, const std::string& filePath);
	void handleDELETE(const HTTPRequest& request, const std::string& filePath);
	void handleErrorPages(HTTPState state);
	void RunCGI(const HTTPRequest& request, const std::string& filePath, const LocationParse& location);

	// Helper functions
	std::string generateImagesGallery(const std::string& imagesDir);
	std::string generateUploadAutoindex(const std::string& uploadDir);
	std::string generateUploadFilename(const std::string& prefix);
	bool extractMultipartFile(const HTTPRequest& request, std::string& fileName, std::string& fileData, std::string& ext);
	std::string findExtension(const HTTPRequest& request, const std::string& fileData);

	/**
	 * @brief Validates that the buffer is recieved totally.
	 * @return True if the size mathces the expected size, false otherwise.
	 */
	bool validateSize(const std::string buffer, const std::string filePath);
	
	/**
	 * @brief Prints the HTTP response details to the standard output.
	 */
	void printResponse() const;

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
	 * @brief Parses the response string based on the request, status message, and file path.
	 * @param request The HTTP request object.
	 * @param statusMessage The HTTP message corresponding to the status.
	 * @param filePath The file path string.
	 * @return The constructed HTTP response string.
	 */
	// std::string parseResponseStr(const HTTPRequest request, HTTPMessage statusMessage, std::string filePath);
	// checking if update works better, because statusmessage isn't updated correctly
	std::string parseResponseStr(const HTTPRequest request, const std::string filePath);

	/**
	 * @brief Clears the response body and resets related headers.
	 */
	void clearBody();

	/**
	 * @brief Updates the response based on the given HTTP state.
	 * @param state The HTTP state to update the response for.
	 */
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