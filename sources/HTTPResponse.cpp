#include "HTTPResponse.hpp"
#include "dirent.h"

HTTPResponse::HTTPResponse(const ServerParse& server)
	: serverParse(server)
{
}

void HTTPResponse::printResponse() const
{
	std::cout << "Protocol Version: " << protocolVersionToString(protocolVersion) << std::endl;
	std::cout << "Status Code: " << statusCode << std::endl;
	std::cout << "Reason Phrase: " << reasonPhrase << std::endl;
	std::cout << "Headers:" << std::endl;
	for (std::map<std::string, std::string>::const_iterator it = headers.begin(); it != headers.end(); ++it)
	{
		std::cout << "  " << it->first << ": " << it->second << std::endl;
	}
	if (!body.empty())
		std::cout << "==Body==\n"
				  << body << std::endl;
}

std::string HTTPResponse::parseContentType(const std::string filePath)
{
	size_t dot = filePath.find_last_of('.');
	if (dot == std::string::npos)
		return "text/plain";
	auto it = allowedExtensions.find(filePath.substr(dot));
	return (it != allowedExtensions.end()) ? it->second : "text/plain";
}

void HTTPResponse::setStandardHeaders()
{
	headers["SERVER"] = "Webserv_Didi_Goksu_and_Reinier";
	headers["CONNECTION"] = "keep-alive";
	headers["DATE"] = setDate();
}
std::string HTTPResponse::setDate()
{
	auto now = std::chrono::system_clock::now();
	time_t currentTime = std::chrono::system_clock::to_time_t(now);
	std::stringstream ss;
	ss << std::ctime(&currentTime);
	return ss.str().end()[-1] == '\n' ? ss.str().substr(0, ss.str().length() - 1) : ss.str();
}

/**
 * @brief Builds a complete HTTP error response for the given HTTP state.
 *
 * @details
 * - Initializes response context from the request
 * - Generates appropriate error page (custom or default)
 * - Applies standard headers (Date, Server, Connection)
 * - Formats final HTTP response string with status line, headers, and body
 */
std::string HTTPResponse::buildErrorResponse(const HTTPRequest& request, HTTPState state)
{
	protocolVersion = request.protocolVersion;
	headers.clear();
	body.clear();
	handleErrorPages(state);

	setStandardHeaders();

	std::string response =
		protocolVersionToString(request.protocolVersion) + " " +
		statusCode + " " + reasonPhrase + "\r\n";

	for (const auto &h : headers)
		response += h.first + ": " + h.second + "\r\n";

	response += "\r\n" + body;

	return response;
}

/**
 * @brief Builds a full HTTP response for a client request.
 *
 * @details
 * - Validates request constraints (URI length)
 * - Resolves requested resource using server configuration
 * - Handles directory logic (index files, permissions, autoindex)
 * - Verifies file existence and access rights
 * - Reads file content for GET/HEAD requests
 * - Delegates method-specific handling to parseResponseStr()
 *
 * @param request Incoming HTTP request
 * @return Fully constructed HTTP response string
 */
std::string HTTPResponse::buildResponse(HTTPRequest request)
{
	std::cerr << "In buildResponse(), with request: " << methodToString(request.method) << " " << request.resourcePath << std::endl;
	
	// --- URI length check (RFC 7230: 414 URI Too Long) ---
	if (request.resourcePath.size() > 8192)
	{
		protocolVersion = request.protocolVersion;
		headers.clear();
		body.clear();
		handleErrorPages(HTTPState::URITooLong);
		headers["CONTENT-LENGTH"] = std::to_string(body.size());
		setStandardHeaders();
		std::string response =
			protocolVersionToString(request.protocolVersion) + " " +
			statusCode + " " + reasonPhrase + "\r\n";
		for (const auto &h : headers)
			response += h.first + ": " + h.second + "\r\n";
		response += "\r\n" + body;
		return response;
	}
	
	protocolVersion = request.protocolVersion;
	headers.clear();
	body.clear();
	updateForHTTPState(HTTPState::Ok);
	
	// --- Get Location info for index ---
	const LocationParse* location = serverParse.get_best_location(request.resourcePath);
	
	if (!location)
	{
		updateForHTTPState(HTTPState::NotFound);
		return parseResponseStr(request, "");
	}

	// --- Build initial path ---
	std::string filePath = serverParse.build_filesystem_path(request.resourcePath);
	// std::cerr << "Initial file path is: " << filePath << std::endl; 
	
	if (filePath.empty())
	{
		updateForHTTPState(HTTPState::NotFound);
		return parseResponseStr(request, "");
	}

	// --- Directory handling (method aware) ---
	bool isDir = serverParse.is_directory(filePath);

	if (isDir)	
	{
		// Only GET / HEAD use index
		if (request.method == HTTPMethod::GET || request.method == HTTPMethod::HEAD)
		{
			if (!location->index.empty())
			{
				filePath = serverParse.joinPaths(filePath, location->index);
				// std::cerr << "Index file found, filepath is: " << filePath << std::endl;
			}
			else
			{
				// No index -> forbidden
				updateForHTTPState(HTTPState::Forbidden);
				return parseResponseStr(request, filePath);
			}
		}
		// If any other method, we keep the original filePath we created with build_filesystem_path().
	}

	// --- Existence Check ---
	// For GET/HEAD the index file must exist. For POST/DELETE the path itself is
	// the target; if it's still a directory at this point (no index was appended),
	// skip the file_exists() test (which returns false for directories).
	bool skipExistenceCheck = isDir &&
		(request.method != HTTPMethod::GET && request.method != HTTPMethod::HEAD);
	if (!skipExistenceCheck && !serverParse.file_exists(filePath))
	{
		updateForHTTPState(HTTPState::NotFound);
		return parseResponseStr(request, filePath);
	}

	// --- Only read file for GET / HEAD ---
	if (request.method == HTTPMethod::GET || request.method == HTTPMethod::HEAD)
	{
		std::ifstream file(filePath.c_str(), std::ios::binary);
		if (!file.is_open())
		{
			updateForHTTPState(HTTPState::Forbidden);
			return parseResponseStr(request, filePath);
		}
		
		std::stringstream buffer;
		buffer << file.rdbuf();
		body = buffer.str();
		file.close();
		std::cout << "File found and opened successfully" << std::endl;
	}
	// For POST / PUT, body handling happens in their handleX functions.

	return parseResponseStr(request, filePath);
}

/**
 * @brief Dispatches request handling based on HTTP method and builds response.
 *
 * @details
 * - Routes request to method-specific handlers (GET, POST, DELETE, HEAD)
 * - Applies default handling for unsupported methods
 * - Sets response headers (Content-Type, Content-Length)
 * - Builds final HTTP response string
 *
 * @param request HTTP request
 * @param filePath Resolved filesystem path for the resource
 * @return Complete HTTP response string
 */
std::string HTTPResponse::parseResponseStr(const HTTPRequest request, const std::string filePath)
{
	switch (request.method)
	{
		case HTTPMethod::GET:
			handleGET(request, filePath);
			break;
		case HTTPMethod::POST:
			handlePOST(request, filePath);
			break;
		case HTTPMethod::PUT:
			break;
		case HTTPMethod::DELETE:
			handleDELETE(request, filePath);
			break;
		case HTTPMethod::HEAD:
			handleHEAD(request, filePath);
			// handleHEAD already clears the body and sets correct CONTENT-LENGTH/CONTENT-TYPE
			break;
		case HTTPMethod::UNSUPPORTED:
			handleErrorPages(HTTPState::MethodNotAllowed);
			break;
		default:
			handleErrorPages(HTTPState::NotImplemented);
			break;
	}

	// --- Common Headers ---
	// For HEAD: handleHEAD already set correct CONTENT-LENGTH (file size) and CONTENT-TYPE
	if (request.method != HTTPMethod::HEAD)
		headers["CONTENT-LENGTH"] = std::to_string(body.size());
	setStandardHeaders();

	// --- Set content type if not already set or empty ---
	if (headers.find("CONTENT-TYPE") == headers.end() || headers["CONTENT-TYPE"].empty())
		headers["CONTENT-TYPE"] = parseContentType(filePath);

	// --- Build HTTP Response String ---
	std::string response =
		protocolVersionToString(request.protocolVersion) + " " +
		statusCode + " " + reasonPhrase + "\r\n";

	for (const auto &h : headers)
		response += h.first + ": " + h.second + "\r\n";

	if (request.method == HTTPMethod::HEAD)
		response += "\r\n";
	else
		response += "\r\n" + body;

	return response;
}

/**
 * @brief Clears the response body and resets content headers.
 *
 * @details
 * Used mainly for HEAD responses and empty error states.
 */
void HTTPResponse::clearBody()
{
	body.clear();
	headers["CONTENT-LENGTH"] = "0";
	headers["CONTENT-TYPE"] = "";
}

/**
 * @brief Updates response state based on HTTP status.
 *
 * @details
 * - Maps HTTPState to status code and reason phrase
 * - Handles special cases like NoContent (clears body)
 * - Generates default error body for error states
 * - Updates Content-Length accordingly
 *
 * @param state HTTP status to apply
 */
void HTTPResponse::updateForHTTPState(HTTPState state)
{
	HTTPMessage statusMessage = HTTPCommon::HTTPStatusMap.at(state);
	statusCode = statusMessage.code;
	reasonPhrase = statusMessage.message;

	if (state == HTTPState::NoContent)
	{
		clearBody();
	}
	else if (state == HTTPState::Ok)
	{
		// Body remains unchanged
	}
	else
	{
		body = statusCode + ": " + statusMessage.description;
		headers["CONTENT-LENGTH"] = std::to_string(body.length());
	}
}

/**
 * @brief Serves an HTML file with simple placeholder injection.
 *
 * @details
 * - Loads an HTML file from disk
 * - Validates file size to prevent oversized responses
 * - Replaces a single placeholder with dynamic content
 * - Builds HTTP response headers and body
 *
 * @param filePath Path to the HTML file
 * @param placeholder String inside the file to replace
 * @param inject Content to insert into the HTML
 * @return true if the page was successfully served, false otherwise
 */
bool HTTPResponse::serveInjectedPage(const std::string& filePath, const std::string& placeholder, const std::string& inject)
{
	std::ifstream file(filePath.c_str());
	if (!file.is_open())
	{
		handleErrorPages(HTTPState::InternalServerError);
		return false;
	}
	std::stringstream buffer;
	buffer << file.rdbuf();
	if (!validateSize(buffer.str(), filePath))
		return false;
	std::string html = buffer.str();
	file.close();

	size_t pos = html.find(placeholder);
	if (pos != std::string::npos)
		html.replace(pos, placeholder.size(), inject);

	body = html;
	headers["CONTENT-TYPE"] = "text/html";
	headers["CONTENT-LENGTH"] = std::to_string(body.size());
	updateForHTTPState(HTTPState::Ok);
	return true;
}

/**
 * @brief Handles requests targeting directory paths.
 *
 * @details
 * - Determines if an index file exists and serves it if available
 * - Supports special dynamic pages (e.g. upload and images)
 * - Applies access control checks for allowed methods
 * - Falls back to autoindex generation if enabled
 * - Returns appropriate HTTP errors if no valid response is possible
 */
void HTTPResponse::handleDirectoryRequest(const HTTPRequest& request, const LocationParse* loc, const std::string& filePath)
{
	// --- Check cgi directory
	if (loc->is_cgi)
	{
		handleErrorPages(HTTPState::Forbidden);
		return;
	}

	// --- Check if index file exists ---
	bool indexExists = !filePath.empty() 
						&& access(filePath.c_str(), F_OK) == 0;

	if (indexExists)
	{
		// --- Special Case: /upload page ---
		if (loc->path == "/upload")
		{
			bool allowDelete = std::find(loc->allowedMethods.begin(), loc->allowedMethods.end(),
										HTTPMethod::DELETE) != loc->allowedMethods.end();
			serveInjectedPage(filePath, "<div id=\"files\"></div>",
							 generateUploadList(loc->root, allowDelete));
			return;
		}

		// --- Special Case: /images page ---
		if (loc->path == "/images")
		{
			serveInjectedPage(filePath, "<div class=\"gallery\" id=\"images\">",
							 generateImagesGallery(loc->root));
			return;
		}

		// --- Normal Index Serving
		if (!checkGetAccess(filePath))
			return;
		
		std::ifstream file(filePath.c_str(), std::ios::binary);
		if (!file.is_open())
		{
			handleErrorPages(HTTPState::InternalServerError);
			return;
		}

		std::stringstream buffer;
		buffer << file.rdbuf();
		if (!validateSize(buffer.str(), filePath))
			return;
		body = buffer.str();
		file.close();

		headers["CONTENT-TYPE"] = parseContentType(filePath);
		headers["CONTENT-LENGTH"] = std::to_string(body.size());
		updateForHTTPState(HTTPState::Ok);
		return;
	}

	// --- No index file, check autoindex ---
	if (loc && loc->autoIndex)
	{
		body = generateAutoindex(loc->root, request.resourcePath);
		headers["CONTENT-TYPE"] = "text/html";
		headers["CONTENT-LENGTH"] = std::to_string(body.size());
		updateForHTTPState(HTTPState::Ok);
		return;
	}

	// --- Directory exists, but no index and autoindex is off ---
	handleErrorPages(HTTPState::NotFound);
}


/**
 * @brief Handles an HTTP GET request.
 *
 * @details
 * - Finds the matching location and checks if GET is allowed.
 * - Checks if the requested file exists on the server.
 * - Uses stat() to determine if the requested path is a file or directory.
 *   - If directory → delegates to handleDirectoryRequest().
 *   - If file → reads and returns the file content.
 * - Sets appropriate headers (Content-Type, Content-Length).
 */
void HTTPResponse::handleGET(const HTTPRequest& request, const std::string& filePath)
{
	std::cout << "Requested file path: " << filePath << std::endl;
	std::cout << "Resource path is: " << request.resourcePath << std::endl;
	
	// --- Find matching location for request ---
	const LocationParse* loc = serverParse.get_best_location(request.resourcePath);
		
	// --- Redirect takes priority over checking method allowance ---
	if (loc && loc->redirect.statusCode != 0 && !loc->redirect.targetURL.empty())
	{
		std::cout << "Redirecting to: " << loc->redirect.targetURL << " with statuscode: " << loc->redirect.statusCode << std::endl;
		body = "";
		headers["LOCATION"] = loc->redirect.targetURL;
		headers["CONTENT-LENGTH"] = "0";
		updateForHTTPState(getRedirectState(loc->redirect.statusCode));
		return;
	}
		
	// --- Check if GET method is allowed ---
	if (!loc || std::find(loc->allowedMethods.begin(), loc->allowedMethods.end(), 
				HTTPMethod::GET) == loc->allowedMethods.end())
	{
		std::cerr << "GET method not allowed for this location" << std::endl;
		handleErrorPages(HTTPState::MethodNotAllowed);
		return;
	}

	// --- Prevent direct browsing of CGI location itself ---
	if (loc && loc->is_cgi)
	{
		if (request.resourcePath == loc->path || request.resourcePath == loc->path + "/")
		{
			handleErrorPages(HTTPState::Forbidden);
			return;
		}
	}
	
	// --- Regular file request ---
	if (!checkGetAccess(filePath))
	return;

	// --- Handle directory requests separately ---
	if (request.resourcePath == loc->path || request.resourcePath == loc->path + "/")
	{
		handleDirectoryRequest(request, loc, filePath);
		return;
	}
	
	std::ifstream file(filePath, std::ios::binary); // Treats the file as binary.
	if (!file.is_open())
	{
		handleErrorPages(HTTPState::NotFound);
		return;
	}

	std::stringstream buffer;
	buffer << file.rdbuf();
	if (!validateSize(buffer.str(), filePath))	
		return;
	body = buffer.str();
	file.close();

	headers["CONTENT-TYPE"] = parseContentType(filePath);
	headers["CONTENT-LENGTH"] = std::to_string(body.size());
	updateForHTTPState(HTTPState::Ok);
}



/**
 * @brief Handles HTTP POST request for file uploads.
 *
 * @details
 * - Verifies POST is allowed for requested location.
 * - Validates request body and size limits.
 * - Supports both:
 *   - multipart/form-data (web uploads)
 *   - raw uploads (e.g. via curl) -> metadata isn't sent, so misses filename
 * - Extracts or generates a filename and sanitizes it. 
 * - Normalizes file extension to lowercase to check againgst
 *   forbidden and allowed lists.
 * - Saves the uploaded file to the target directory.
 */
void HTTPResponse::handlePOST(const HTTPRequest& request, const std::string& filePath)
{
	// --- Find matching location for request ---
	const LocationParse* location = serverParse.get_best_location(request.resourcePath);

	// --- Check if POST is allowed for this location ---
	if (!location || std::find(location->allowedMethods.begin(), location->allowedMethods.end(), 
							   HTTPMethod::POST) == location->allowedMethods.end())
	{
		std::cerr << "POST method not allowed for this location" << std::endl;
		handleErrorPages(HTTPState::MethodNotAllowed);
		return;
	}

	// --- Validate upload directory, body and size ---
	if (filePath.empty())
	{
		handleErrorPages(HTTPState::NotFound);
		return;
	}
	if (request.body.empty())
	{
		handleErrorPages(HTTPState::BadRequest);
		return;
	}
	size_t effectiveMaxBody = (location && location->maxBodySize > 0)
		? location->maxBodySize
		: serverParse.maxBodySize;
	if (request.body.size() > effectiveMaxBody)
	{
		handleErrorPages(HTTPState::RequestTooLarge);
		return ;
	}

	// --- Determine if upload is multipart/form-data (webform) ---
	bool isMultipart = false;
	if (request.headers.count("CONTENT-TYPE"))
	{
		std::string ct = request.headers.at("CONTENT-TYPE");
		if (ct.find("multipart/form-data") != std::string::npos)
			isMultipart = true;
	}

	// --- Extract filename + file content + extension ---
	std::string fileName = "";
	std::string fileData = request.body;
	std::string ext = "";

	// --- If multipart/form-data/webform: extract filename, content and extension ---
	if (isMultipart)
	{
		if (!extractMultipartFile(request, fileName, fileData, ext))
		{	
			handleErrorPages(HTTPState::BadRequest);
			return ;
		}

		// normalize extension to lowercase for checks
    	std::transform(ext.begin(), ext.end(), ext.begin(), ::tolower);
		
		if (forbiddenExtensions.count(ext))
		{
			std::cout << "Upload rejected due to forbidden extension: " << ext << std::endl;
			handleErrorPages(HTTPState::Forbidden);
			return;
		}
		
		if (!allowedExtensions.count(ext))
			ext = ".bin";
	}
	
	// --- Generate filename if not present ---
	if (fileName.empty())
	{
		fileName = generateUploadFilename("upload_");
	}
	else
	{
		// --- Sanitize filename: remove any directory components ---
		size_t lastSlash = fileName.find_last_of("/\\");
		if (lastSlash != std::string::npos)
			fileName = fileName.substr(lastSlash + 1); // removes any path components: "subdir/../file.txt" -> "file.txt"
	}

	// --- If RAW upload ---
	if (!isMultipart)
	{
		ext = findExtension(request, fileData);
    	std::transform(ext.begin(), ext.end(), ext.begin(), ::tolower);

		if (forbiddenExtensions.count(ext))
		{
			std::cout << "Upload rejected due to forbidden extension: " << ext << std::endl;
			handleErrorPages(HTTPState::Forbidden);
			return ;
		}

		if (!allowedExtensions.count(ext) || ext.empty())
			ext = ".bin";
	}
	fileName += ext;

	// --- Check if upload target directory exists and is writable ---
	if (!checkPostAccess(filePath))
		return ;

	// --- Ensure filePath ends with a slash for correct concatenation ---
	std::string fullPath = filePath;
	if (!fullPath.empty() && fullPath.back() != '/')
		fullPath += "/";
	fullPath += fileName;

	std::cout << "Saving uploaded file to: " << fullPath << std::endl;
	std::cout << "FileName is: " << fileName << std::endl;

	// --- Save/write file ---
	std::ofstream outFile(fullPath, std::ios::binary);
	if (!outFile.is_open())
	{
		std::cout << "Couldn't create post" << std::endl;
		handleErrorPages(HTTPState::InternalServerError);
		return ;
	}
	
	outFile << fileData;
	if (!outFile.good())
	{
		std::cout << "couldn't write to file" << std::endl;
		outFile.close();
		handleErrorPages(HTTPState::InternalServerError);
		return ;
	}

	outFile.close();
	
	// --- Successfull upload ---
	handleErrorPages(HTTPState::Created);
}

/**
 * @brief Handles an HTTP DELETE request.
 *
 * @details
 * - Verifies that the target file exists and is deletable.
 * - Checks if DELETE is allowed for the requested location.
 * - Prevents deletion of protected files (e.g. .html).
 * - Removes the file from the filesystem.
 */
void HTTPResponse::handleDELETE(const HTTPRequest& request, const std::string& filePath)
{
	// --- Check if target exists and can be deleted (404 must take priority over 405) ---
	if (!checkDeleteAccess(filePath))
		return;

	// --- Find the matching location for this request ---
	const LocationParse* location = serverParse.get_best_location(request.resourcePath);

	// --- Check if DELETE is allowed for this location ---
	if (!location ||
		std::find(location->allowedMethods.begin(),
	          	  location->allowedMethods.end(),
	          	  HTTPMethod::DELETE) == location->allowedMethods.end())
	{
		std::cerr << "DELETE method not allowed for this location" << std::endl;
		handleErrorPages(HTTPState::MethodNotAllowed);
		return;
	}

	// --- Prevent deletion of HTML files ---
	if (filePath.size() >= 5 && filePath.substr(filePath.size() - 5) == ".html")
	{
		std::cerr << "Attempted to delete an HTML file, which is forbidden: " << filePath << std::endl;
		handleErrorPages(HTTPState::Forbidden);
		return;
	}

	// --- Attempt to delete the file ---
	if (std::remove(filePath.c_str()) != 0)
	{
		std::cerr << "Error deleting file: " << filePath << std::endl;
		handleErrorPages(HTTPState::Forbidden);
		return;
	}

	// --- Successfull deletion ---
	std::cout << "File deleted successfully: " << filePath << std::endl;
	body.clear();
	headers["CONTENT-TYPE"] = "text/plain";
	headers["CONTENT-LENGTH"] = std::to_string(body.size());
	updateForHTTPState(HTTPState::NoContent);
}

/**
 * @brief Handles an HTTP HEAD request.
 *
 * @details
 * - Returns headers identical to GET, but without including the file body.
 * - Used to allow clients to check metadata (Content-Length, Content-Type) without downloading the content.
 * - If the file does not exist, serves the appropriate error page headers.
 */
void HTTPResponse::handleHEAD(const HTTPRequest& request, const std::string& filePath)
{
	// --- Check if file exists and is accessible ---
	if (!checkGetAccess(filePath))
		return;

	// --- Set headers if file exists ---
	std::ifstream file(filePath, std::ios::binary);
	if (!file.is_open())
	{
		handleErrorPages(HTTPState::InternalServerError); // Already checked in checkGetAccess, so should now be an unexpected error.
		return;
	}

	std::cout << "File opened successfully for HEAD request" << std::endl;

	// --- Get file size for Content-Length header ---
	file.seekg(0, std::ios::end);			// Move the read pointer to end of the file
	std::streampos fileSize = file.tellg();	// Get current pos -> this is the byte sized file
	file.close();

	body.clear(); // No body for HEAD response
	headers["CONTENT-TYPE"] = parseContentType(filePath);
	headers["CONTENT-LENGTH"] = std::to_string(fileSize);
	request.printRequest();
	std::cout << "HEAD request headers set with Content-Length: " << fileSize << std::endl;
	updateForHTTPState(HTTPState::Ok);
}

/**
 * @brief Serves error pages for HTTP responses.
 *
 * @details
 * - Attempts to serve a pre-made HTML error page based on the HTTPState (e.g., 404.html).
 * - If the HTML error page is missing, falls back to a plain-text message.
 * - Sets the Content-Type and Content-Length headers accordingly.
 * - Can be called from any HTTP method handler to standardize error responses.
 *
 * @param state The HTTP state corresponding to the error (e.g., NotFound, BadRequest).
 */
void HTTPResponse::handleErrorPages(HTTPState state)
{
	updateForHTTPState(state);
	bool isError = (state >= HTTPState::BadRequest);
	std::cout << "[HTTP] Generating "
			  << (isError ? "error" : "success") 
			  << " page for status code: " << HTTPCommon::HTTPStatusMap.at(state).code << std::endl;
	
	HTTPMessage statusMessage = HTTPCommon::HTTPStatusMap.at(state);
	
	// Path to error HTML pages
	const std::string* customErrorPath = serverParse.get_error_page(state);
	std::string resolvedErrorPath;
	if (customErrorPath != nullptr)
	{
		resolvedErrorPath = *customErrorPath;
		std::cout << "Custom error page was provided in config file for status code: " << statusMessage.code << std::endl;
		// std::cout << "Custom page path is: " << resolvedErrorPath << std::endl;
	}
	else
	{
		resolvedErrorPath = HTTPCommon::defaultErrorPagePath(statusMessage);
		// std::cout << "Status code: " << statusMessage.code << " is not listed in config file error pages." << std::endl;
	}
	std::ifstream file(resolvedErrorPath, std::ios::binary);

	// --- If the error page exists, serve it ---
	if (file.is_open())
	{
		std::stringstream buffer;
		buffer << file.rdbuf();
		if (!validateSize(buffer.str(), resolvedErrorPath))	
			return;
		body = buffer.str();
		file.close();

		// Fill optional template placeholders (only if they exist in the HTML)
		HTTPCommon::fillErrorPageTemplate(body, statusMessage);

		// Set the content-type for HTML error pages
		headers["CONTENT-TYPE"] = "text/html";
		headers["CONTENT-LENGTH"] = std::to_string(body.size());
	}
	else	// --- If the error page is missing, serve a simple plain-text message ---
	{
		body = statusMessage.code + ": " + statusMessage.message;
		headers["CONTENT-TYPE"] = "text/plain";
		headers["CONTENT-LENGTH"] = std::to_string(body.size());
	}
}

 /**
  * @brief Generates an HTML gallery page for all images in the /images directory.
  * 
  * @param imagesDir = the path to the images directory ("www/html/images")
  * @return std::string = the full HTML page as a string
  * 
  * @details
  * - Scans the specified directory for image files with the following extensions: .png, .jp(e)g, .gif.
  * - Ignores "." and ".." entries, to avoid listing current and parent directory.
  * - For each image, adds a thumbnail wrapped in a clickable <a> link to the full image.
  * - Returns the complete HTML page as a string, ready to be sent as the HTTP response body
  * - Automatically updates the gallery to the newly added images (no need to manually update the HTML).
  */
std::string HTTPResponse::generateImagesGallery(const std::string& imagesDir) 
{
    std::string html;
    DIR *dir = opendir(imagesDir.c_str());
    if (!dir) return html;

    struct dirent *entry;
    while ((entry = readdir(dir)) != NULL) 
    {
        std::string name = entry->d_name;
		// Skip current directory
        if (name == "." || name == "..") 
			continue;

        if (name.find(".png") != std::string::npos || 
            name.find(".jpg") != std::string::npos ||
            name.find(".jpeg") != std::string::npos ||
            name.find(".gif") != std::string::npos) 
        {
            html += "<div class=\"image-item\">";
			html += "<a href=\"/images/" + name + "\" target=\"_blank\">";
			html += "<img src=\"/images/" + name + "\" alt=\"" + name + "\">";
			html += "</a>";
			html += "<div class=\"filename\">" + name + "</div>";
			html += "</div>";
        }
    }
    closedir(dir);
    return html;
}

/**
 * @brief Generates a simple HTML directory listing (autoindex).
 *
 * @param dirPath Filesystem path to read directory contents from.
 * @param urlPath URL path to use for the links in the listing (e.g., "/files/").
 *
 * @details
 * - Lists all files in the directory as links.
 * - Used when no index file is present and autoindex is enabled.
 * - Returns a minimal HTML page showing the directory contents.
 */
std::string HTTPResponse::generateAutoindex(const std::string& dirPath, const std::string& urlPath)
{
	DIR *dir = opendir(dirPath.c_str());
	if (!dir) 
		return "<html><body><h1>Unable to open directory</h1></body></html>";
	
	struct dirent *entry;
	std::stringstream html;

	html << "<html><head><title>Index of " << urlPath << "</title></head>";
	html << "<body>";
	html << "<h1>Index of " << urlPath << "</h1>";
	html << "<ul>";

	while ((entry = readdir(dir)) != NULL) 
	{
		std::string name = entry->d_name;
		
		// Skip current directory
		if (name == "." || name == "..") 
			continue;

		std::string link = urlPath;
		if (!link.empty() && link.back() != '/')
			link += "/";
		link += name;

		html << "<li><a href=\"" << link << "\">" << name << "</a></li>";
	}
	html << "</ul>";
	html << "</body></html>";

	closedir(dir);
	return html.str();
}

/**
 * @brief Generates an HTML autoindex page for all uploaded files in the /upload directory.
 *
 * @details
 * - Reads all files in the directory (skips "." and ".." and .html files).
 * - Uses a stringstream to build HTML dynamically.
 * - Each file is wrapped in a <div class="file-item">:
 *     - <a> tag → opens the file in the browser
 *     - optional <button> → triggers delete via JavaScript
 */
std::string HTTPResponse::generateUploadList(const std::string& uploadDir, bool allowDelete)
{
   	DIR* dir = opendir(uploadDir.c_str());
	if (!dir)
	{
        return "";
	}

    struct dirent* entry;
    std::stringstream ss;

    while ((entry = readdir(dir)) != NULL)
    {
        std::string name = entry->d_name;
        if (name == "." || name == "..")
            continue;

        if (name.find(".html") != std::string::npos)
            continue;

        ss << "<div class=\"file-item\">"
		   << "<a href=\"/upload/" << name << "\" target=\"_blank\">" << name << "</a>";
		if (allowDelete)
		{
			ss << "<button class=\"delete-btn\" "
			   << "onclick=\"deleteFile('" << name << "')\">Delete</button>";
		}
		ss << "</div>";
    }

    closedir(dir);
    return ss.str();
}

/**
 * @brief Generates a unique filename for uploaded files.
 *
 * @details
 * - Uses the current local date and time as part of the filename.
 * - Format: prefix + YYYYMMDD_HHMMSS_counter
 * - Adds a static incrementing counter to avoid collisions
 *   when multiple uploads happen within the same second.
 * - Returns only the filename (no directory or extension).
 */
std::string HTTPResponse::generateUploadFilename(const std::string& prefix)
{
	// --- Get current time ---
	std::time_t now = std::time(nullptr);
	std::tm* localTime = std::localtime(&now);

	// --- Format time as YYYY/MM/DD_HH/MM/SS ---
	std::stringstream ss;
	ss << prefix
	   << localTime->tm_year + 1900
	   << (localTime->tm_mon + 1)
	   << localTime->tm_mday
	   << "_"
	   << localTime->tm_hour
	   << localTime->tm_min;

	// --- Add static counter for uniqueness ---
	static int counter = 0;
	ss << "_" << counter++;

	return ss.str();
}

/** 
 * @brief Extracts the filename, file data and extension from a multipart/form-data HTTP request body (web)
 * 
 * @details
 * - Checks if there's a Content-Type header that includes a boundary (required for multipart).
 * - Uses the boundary to locate the start of the multipart section in the body.
 * - Save the headers part in a string to extract the filename and extension if present.
 * - Extracts the actual file data/contant between the headers and the next boundary.
 * - Also removes any trailing CRLF before the next boundary, as uploading files through web forms 
 *   often ends the file content with a CRLF before the boundary (which is not part of the actual file data).
 */
bool HTTPResponse::extractMultipartFile(const HTTPRequest& request, std::string& fileName, std::string& fileData, std::string& ext)
{
	std::string body = request.body;

	// --- Get boundary from Content-Type header ---
	std::string boundary;
	if (request.headers.count("CONTENT-TYPE"))
	{
		std::string ct = request.headers.at("CONTENT-TYPE");
		size_t pos = ct.find("boundary=");
		if (pos == std::string::npos)
			return false; // No boundary found, invalid
		boundary = "--" + ct.substr(pos + 9);
	}
	else
		return false;

	// --- Find first boundary to locate start of the multipart section ---
	size_t boundaryStart = body.find(boundary);
	if (boundaryStart == std::string::npos)
		return false;
	boundaryStart += boundary.length() + 2; // move past CRLF after boundary (boundary\r\n)

	// --- Find end of headers (marked by double CRLF) to seperate headers from file content ---
	size_t headerEnd = body.find("\r\n\r\n", boundaryStart);
	if (headerEnd == std::string::npos)
		return false;

	// --- Save headers part to extract info ---
	std::string headersPart = body.substr(boundaryStart, headerEnd - boundaryStart);

	// --- Extract filename and extension if present in header ---
	size_t filenamePos = headersPart.find("filename=");
	if (filenamePos != std::string::npos)
	{
		size_t startQuote = headersPart.find('"', filenamePos);
		size_t endQuote = headersPart.find('"', startQuote + 1);
		// Extract OG filename from multipart headers
		if (startQuote != std::string::npos && endQuote != std::string::npos)
		{
			fileName = headersPart.substr(startQuote + 1, endQuote - startQuote -1);

			size_t lastDot = fileName.find_last_of('.');
			if (lastDot != std::string::npos)
			{
				ext = fileName.substr(lastDot); 		// includes the dot and extension
				fileName = fileName.substr(0, lastDot);	// filename without extension
			}
		}
	}

	// --- Extract file content/data (between headers and next boundary) ---
	size_t contentStart = headerEnd + 4; // Skip double CRLF
	size_t contentEnd = body.find(boundary, contentStart);
	if (contentEnd == std::string::npos)
		return false; // No second boundary found
	
	// --- Remove trailing CRLF before boundary if present ---
	// 	   Can happen because multipart format often ends file content with CRLF before the boundary, but is not part of the actual file data.
	if (contentEnd >= 2 && body[contentEnd - 2] == '\r' && body[contentEnd - 1] == '\n')
		contentEnd -= 2;

	//  --- Extract actual file data/content ---
	fileData = body.substr(contentStart, contentEnd - contentStart);
	return true;
}

std::string HTTPResponse::findExtension(const HTTPRequest& request, const std::string& fileData)
{
    std::string ext = "";

    // --- Check Content-Type header ---
    if (request.headers.count("CONTENT-TYPE"))
    {
        std::string ct = request.headers.at("CONTENT-TYPE");

        // Map common content-types to extensions
        if (ct == "image/jpeg" || ct == "image/jpg") 
			ext = ".jpg";
        else if (ct == "image/png") 
			ext = ".png";
        else if (ct == "image/gif") 
			ext = ".gif";
        else if (ct == "text/plain") 
			ext = ".txt";
        else if (ct == "application/pdf") 
			ext = ".pdf";
        else if (ct == "application/zip") 
			ext = ".zip";
        else if (ct == "image/svg+xml") 
			ext = ".svg";
        // Forbidden types
        else if (ct == "application/x-msdownload") 
			ext = ".exe";
        else if (ct == "application/x-bat") 
			ext = ".bat";
        else if (ct == "application/x-cmd") 
			ext = ".cmd";
    }

    // --- Check magic bytes if no extension was found ---
    if (ext.empty() && fileData.size() >= 4)
    {
		// unsigned char to avoid sign issues with char data when checking magic bytes
        const unsigned char* data = reinterpret_cast<const unsigned char*>(fileData.data());

        if (data[0] == 0xFF && data[1] == 0xD8 && data[2] == 0xFF) 
			ext = ".jpg";
        else if (data[0] == 0x89 && data[1] == 0x50 && data[2] == 0x4E && data[3] == 0x47) 
			ext = ".png";
        else if (data[0] == 0x47 && data[1] == 0x49 && data[2] == 0x46) 
			ext = ".gif";
		else if (data[0] == 0x25 && data[1] == 0x50 && data[2] == 0x44 && data[3] == 0x46)
            return ".pdf";
        // ZIP / DOCX (both start with 50 4B 03 04)
        else if (data[0] == 0x50 && data[1] == 0x4B && data[2] == 0x03 && data[3] == 0x04)
        {
            // Check if it's likely a DOCX (look for [Content_Types].xml in first 1 KB)
            std::string start(fileData.begin(), fileData.begin() + std::min<size_t>(1024, fileData.size()));
            if (start.find("[CONTENT-TYPE].xml") != std::string::npos)
                return ".docx";
            else
                return ".zip";
		}
   	}

    return ext; // Can be empty if unknown
}

bool HTTPResponse::checkGetAccess(const std::string& filePath)
{
	if (filePath.empty())
	{
		handleErrorPages(HTTPState::NotFound);
		return false;
	}

	// Check if file exists
	if (access(filePath.c_str(), F_OK) != 0)
	{
		handleErrorPages(HTTPState::NotFound);
		return false;
	}

	// Check if file is readable/openable
	if (access(filePath.c_str(), R_OK) != 0)
	{
		handleErrorPages(HTTPState::Forbidden);
		return false;
	}

	return true;
}

/**
 * @brief Checks if a directory exists and is writable for POST uploads.
 *
 * @details
 * - Uses access() to verify existence (F_OK) and write permission (W_OK).
 * - Uses stat() to retrieve file metadata and confirm the path is a directory.
 * - struct stat stores information about the file (type, permissions, etc.).
 * - S_ISDIR checks if the path refers to a directory.
 */
bool HTTPResponse::checkPostAccess(const std::string& filePath)
{
	if (filePath.empty())
	{
		handleErrorPages(HTTPState::NotFound);
		return false;
	}

	// Check if path exists
	if (access(filePath.c_str(), F_OK) != 0)
	{
		handleErrorPages(HTTPState::NotFound);
		return false;
	}

	// Check if path is a directory (uploads must target a directory)
	struct stat pathStat;
	if (stat(filePath.c_str(), &pathStat) != 0 || !S_ISDIR(pathStat.st_mode))
	{
		handleErrorPages(HTTPState::Forbidden);
		return false;
	}
	
	// Check if directory is writable
	if (access(filePath.c_str(), W_OK) != 0)
	{
		handleErrorPages(HTTPState::Forbidden);
		return false;
	}

	return true;
}

/**
 * @brief Checks if the file exists, is executable and readable for CGI execution.
 * 	F_OK: Tests for existence of the file.
 * 	R_OK: Tests for read permission.
 * 	X_OK: Tests for execute permission.
 */
bool HTTPResponse::checkCGIAccess(const std::string& filePath)
{
	if (filePath.empty())
	{
		handleErrorPages(HTTPState::NotFound);
		return false;
	}

	if (access(filePath.c_str(), F_OK) != 0)
	{
		handleErrorPages(HTTPState::NotFound);
		return false;
	}

	if (access(filePath.c_str(), R_OK | X_OK) != 0)
	{
		handleErrorPages(HTTPState::Forbidden);
		return false;
	}

	return true;
}

/**
 * @brief Checks if a file exists and can be deleted.
 *
 * @details
 * - Uses access() to verify existence (F_OK) and write permission (W_OK).
 * - Uses stat() to retrieve file metadata.
 * - struct stat stores information about the file (type, permissions, etc.).
 * - S_ISDIR is used to prevent deletion of directories.
 */
bool HTTPResponse::checkDeleteAccess(const std::string& filePath)
{
	if (filePath.empty())
	{
		handleErrorPages(HTTPState::NotFound);
		return false;
	}

	// Check if path exists
	if (access(filePath.c_str(), F_OK) != 0)
	{
		handleErrorPages(HTTPState::NotFound);
		return false;
	}

	// Check if target is a directory (forbidden to delete)
	struct stat pathStat;
	if (stat(filePath.c_str(), &pathStat) != 0 || S_ISDIR(pathStat.st_mode))
	{
		handleErrorPages(HTTPState::MethodNotAllowed);
		return false;
	}
	
	// Check if file is writable before attempting deletion
	if (access(filePath.c_str(), W_OK) != 0)
	{
		handleErrorPages(HTTPState::Forbidden);
		return false;
	}

	return true;
}

HTTPState HTTPResponse::getRedirectState(int code) const
{
	switch (code)
	{
		case 301: return HTTPState::MovedPermanently;
		case 302: return HTTPState::Found;
		case 307: return HTTPState::TemporaryRedirect;
		case 308: return HTTPState::PermanentRedirect;
		default:  return HTTPState::InternalServerError; // should never happen if validated
	}
}

// Validates if the buffer size matches the expected file size on disk.
// This ensures we received the entire file before attempting to save it, preventing incomplete uploads from being saved.
bool HTTPResponse::validateSize(const std::string& buffer, const std::string& filePath)
{
	std::ifstream file(filePath.c_str(), std::ios::binary | std::ios::ate);
	if (!file.is_open())
	{
		std::cerr << "Could not reopen file to validate size: " << filePath << std::endl;
		handleErrorPages(HTTPState::InternalServerError);
		return false;
	}

	std::ifstream::pos_type expected = file.tellg();
	file.close();
	if (expected < 0 || buffer.size() != static_cast<size_t>(expected))
	{
		std::cerr << "Buffer size mismatch: buffer is " << buffer.size() << " bytes, expected was " << expected << " bytes" << std::endl;
		handleErrorPages(HTTPState::InternalServerError);
		return false;
	}
	return true;
}