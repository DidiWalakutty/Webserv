#include "HTTPResponse.hpp"
#include "RequestRouting.hpp"
#include "utilities.hpp"

bool HTTPResponse::serveInjectedPage(const std::string& filePath, const std::string& placeholder, const std::string& inject)
{
	std::ifstream file(filePath.c_str());
	if (!file.is_open())
	{
		handleErrorPages(HTTPState::InternalServerError);
		return false;
	}

	// --- Read File Content into Body ---
	std::stringstream buffer;
	buffer << file.rdbuf();

	if (!validateSize(buffer.str(), filePath))
		return false;

	std::string html = buffer.str();
	file.close();

	// --- Inject content into placeholder ---
	size_t pos = html.find(placeholder);
	if (pos != std::string::npos)
		html.replace(pos, placeholder.size(), inject);

	body = html;
	headers["CONTENT-TYPE"] = "text/html";
	updateForHTTPState(HTTPState::Ok);

	return true;
}

void HTTPResponse::handleDirectoryRequest(const HTTPRequest& request, const RouteResult& route)
{
	const LocationParse* loc = route.location;

	// --- Build full index path---
	std::string indexPath;

	if (!route.indexFile.empty())
		indexPath = serverParse.joinPaths(route.filePath, route.indexFile);

	// --- Check if index file exists and is readable ---

	bool indexExists = !indexPath.empty() && pathExists(indexPath) && isReadable(indexPath);

	// --- Serve Index File ---
	if (indexExists)
	{
		// --- Special Case: /upload page ---
		if (loc->path == "/upload")
		{
			bool allowDelete = std::find(loc->allowedMethods.begin(), loc->allowedMethods.end(),
										HTTPMethod::DELETE) != loc->allowedMethods.end();
			serveInjectedPage(indexPath, "<div id=\"files\"></div>",
							 generateUploadList(loc->root, allowDelete));
			return;
		}

		// --- Special Case: /images gallery ---
		if (loc->path == "/images")
		{
			serveInjectedPage(indexPath, "<div class=\"gallery\" id=\"images\">",
							 generateImagesGallery(loc->root));
			return;
		}

		// --- Regular index file handling ---		
		std::ifstream file(indexPath.c_str(), std::ios::binary);
		if (!file.is_open())
		{
			handleErrorPages(HTTPState::InternalServerError);
			return;
		}

		std::stringstream buffer;
		buffer << file.rdbuf();

		if (!validateSize(buffer.str(), indexPath))
			return;
		
		body = buffer.str();
		file.close();

		headers["CONTENT-TYPE"] = parseContentType(indexPath);
		updateForHTTPState(HTTPState::Ok);

		return;
	}

	// --- No index file, check autoindex ---
	if (route.autoIndex)
	{
		body = generateAutoindex(route.filePath, request.resourcePath);
		headers["CONTENT-TYPE"] = "text/html";
		headers["CONTENT-LENGTH"] = std::to_string(body.size());
		updateForHTTPState(HTTPState::Ok);
		return;
	}

	// --- No index + no autoindex ---
	handleErrorPages(HTTPState::Forbidden);
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
void HTTPResponse::handleGET(const HTTPRequest& request, const RouteResult& route)
{
	std::cout << "Requested file path: " << route.filePath << std::endl;
	std::cout << "Resource path is: " << request.resourcePath << std::endl;

	// --- Redirect takes priority --- 
	if (route.hasRedirect)
	{
		std::cout << "Redirecting to: " << route.redirectTarget << " with statuscode: " << route.redirectCode << std::endl;
		body.clear();
		headers["LOCATION"] = route.redirectTarget;
		headers["CONTENT-LENGTH"] = "0";
		updateForHTTPState(getRedirectState(route.redirectCode));
		return;
	}
		
	// --- Prevent browsing CGI directories (/cgi-bin/) ---
	if (route.location->is_cgi && route.isDirectory)
	{
			handleErrorPages(HTTPState::Forbidden);
			return;
	}
	
	// --- Directory handling ---
	if (route.isDirectory)
	{
		handleDirectoryRequest(request, route);
		return;
	}

	if (!route.readable)
	{
		handleErrorPages(HTTPState::Forbidden);
		return;
	}

	// --- Regular file handling ---
	std::ifstream file(route.filePath.c_str(), std::ios::binary); // Treats the file as binary.
	if (!file.is_open())
	{
		handleErrorPages(HTTPState::InternalServerError);
		return;
	}

	// --- Read File Content into Body ---
	std::stringstream buffer;
	buffer << file.rdbuf();

	if (!validateSize(buffer.str(), route.filePath))	
		return;

	body = buffer.str();
	file.close();

	// --- Success Response ---
	headers["CONTENT-TYPE"] = parseContentType(route.filePath);
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
void HTTPResponse::handlePOST(const HTTPRequest& request, const RouteResult& route)
{
	// --- Body Validation ---
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
	
	if (!route.isDirectory || !route.writable)
	{
		handleErrorPages(HTTPState::Forbidden);
		return;
	}

	// --- Determine if upload is multipart/form-data (webform) ---
	bool isMultipart = false;
	if (request.headers.count("CONTENT-TYPE"))
	{
		std::string ct = request.headers.at("CONTENT-TYPE");
		if (ct.find("multipart/form-data") != std::string::npos)
			isMultipart = true;
	}

	// --- Extract File Data ---
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
		
		// !!!!test, remove later
		std::cout << "Extracted - multipart - filename: " << fileName << std::endl;
		std::cout << "Extracted - multipart - extension: " << ext << std::endl;
	}
	
	// --- Generate filename if not present ---
	if (fileName.empty())
		fileName = generateUploadFilename("upload_");
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

	// // --- Check if upload target directory exists and is writable ---
	// if (!checkPostAccess(filePath))
	// 	return ;

	// --- Ensure filePath ends with a slash for correct concatenation ---
	std::string fullPath = serverParse.joinPaths(route.filePath, fileName);
	
	std::cout << "Saving uploaded file to: " << fullPath << std::endl;
	std::cout << "FileName is: " << fileName << std::endl;

	// --- Save/write file ---
	std::ofstream outFile(fullPath, std::ios::binary);
	if (!outFile.is_open()) // couldn't create file
	{
		handleErrorPages(HTTPState::InternalServerError);
		return ;
	}
	
	outFile.write(fileData.c_str(), fileData.size());
	if (!outFile.good()) // couldn't write to file
	{
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
void HTTPResponse::handleDELETE(const HTTPRequest& request, const RouteResult& route)
{
	(void)request; // Unused parameter, but we may need it later for error handling or logging, so we keep it in the function

	if (!route.exists)
	{
		handleErrorPages(HTTPState::NotFound);
		return;
	}

	if (!route.writable || route.isDirectory) // DELETE requires write permissions and cannot delete directories
	{
		handleErrorPages(HTTPState::Forbidden);
		return;
	}

	// --- Check if target exists and can be deleted (404 must take priority over 405) ---
	if (!checkDeleteAccess(route.filePath))
	// 	return;

	// --- Prevent deletion of HTML files ---
	if (route.filePath.size() >= 5 && route.filePath.substr(route.filePath.size() - 5) == ".html")
	{
		handleErrorPages(HTTPState::Forbidden);
		return;
	}

	// --- Attempt to delete the file ---
	if (std::remove(route.filePath.c_str()) != 0)
	{
		handleErrorPages(HTTPState::Forbidden);
		return;
	}

	// --- Successfull deletion ---
	std::cout << "File deleted successfully: " << route.filePath << std::endl;
	body.clear();
	headers["CONTENT-TYPE"] = "text/plain";
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
void HTTPResponse::handleHEAD(const HTTPRequest& request, const RouteResult& route)
{
	// // --- Check if file exists and is accessible ---
	// if (!checkGetAccess(route.filePath))
	// 	return;

	// // --- Set headers if file exists ---
	// std::ifstream file(route.filePath, std::ios::binary);
	// if (!file.is_open())
	// {
	// 	handleErrorPages(HTTPState::InternalServerError); // Already checked in checkGetAccess, so should now be an unexpected error.
	// 	return;
	// }

	// std::cout << "File opened successfully for HEAD request" << std::endl;

	// // --- Get file size for Content-Length header ---
	// file.seekg(0, std::ios::end);			// Move the read pointer to end of the file
	// std::streampos fileSize = file.tellg();	// Get current pos -> this is the byte sized file
	// file.close();

	// body.clear(); // No body for HEAD response
	// headers["CONTENT-TYPE"] = parseContentType(route.filePath);
	// request.printRequest();
	// // std::cout << "HEAD request headers set with Content-Length: " << fileSize << std::endl;
	// updateForHTTPState(HTTPState::Ok);


	// OR
	// head should be identically to GET, but without body!
	handleGET(request, route);
	// remove body for head
	headers["CONTENT-LENGTH"] = std::to_string(body.size());
	body.clear();
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
	std::cout << "in HandleErrorPages with state: " << state << std::endl;
	updateForHTTPState(state);
	std::cout << "Error page is given: " << state << std::endl;
	HTTPMessage statusMessage = HTTPCommon::HTTPStatusMap.at(state);
	
	// Path to error HTML pages
	const std::string* customErrorPath = serverParse.get_error_page(state);
	std::string resolvedErrorPath;
	if (customErrorPath != nullptr)
	{
		resolvedErrorPath = *customErrorPath;
		std::cout << "Custom error page was provided in config file for status code: " << statusMessage.code << std::endl;
		std::cout << "Custom page path is: " << resolvedErrorPath << std::endl;
	}
	else
	{
		resolvedErrorPath = HTTPCommon::defaultErrorPagePath(statusMessage);
		std::cout << "Status code: " << statusMessage.code << " is not listed in config file error pages." << std::endl;
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

