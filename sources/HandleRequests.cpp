#include "HTTPResponse.hpp"

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
						&& access(filePath.c_str(), F_OK) == 0
						&& access(filePath.c_str(), R_OK) == 0;

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

	// --- Handle directory requests separately ---
	if (request.resourcePath == loc->path || request.resourcePath == loc->path + "/")
	{
		handleDirectoryRequest(request, loc, filePath);
		return;
	}

	// --- Regular file request ---
	if (!checkGetAccess(filePath))
	return;
	
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
	if (request.body.size() > serverParse.maxBodySize)
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

