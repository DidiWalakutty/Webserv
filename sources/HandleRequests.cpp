#include "HTTPResponse.hpp"

/**
 * @brief Handles an HTTP GET request.
 *
 * @details
 * - If HTTP request is for /images, generates a dynamic gallery page based on the contents of the images directory.
 * - Checks if the requested file exists on the server.
 * - Reads the file into memory (binary-safe, as is on disk) and sets it as the response body.
 * - Sets the Content-Type based on the file extension.
 * - If the file is missing, serves the appropriate error page (404).
 * - Only reads files; does not modify server state.
 */
void HTTPResponse::handleGET(const HTTPRequest& request, const std::string& filePath)
{
	// std::cout << "Recourse path is " << request.resourcePath << std::endl;
	std::cout << "Requested file path: " << filePath << std::endl;
	
	// --- Special case: redirect
	const LocationParse* loc = serverParse.get_best_location(request.resourcePath);
	std::cout << "Resource path is: " << request.resourcePath << std::endl;
	std::cout << "Best location path is: " << loc->path << std::endl;

	// when testing if website sees redirect.
	// std::cout << "loc redirect statuscode: " << loc->redirect.statusCode << std::endl;
	// std::cout << "loc redirect targeturl: " << loc->redirect.targetURL << std::endl;
	if (loc && loc->redirect.statusCode != 0 && !loc->redirect.targetURL.empty())
	{
		std::cout << "Redirecting to: " << loc->redirect.targetURL << " with statuscode: " << loc->redirect.statusCode << std::endl;
		body = "";
		headers["Location"] = loc->redirect.targetURL;
		headers["Content-Length"] = "0";

		if (loc->redirect.statusCode == 301)
			updateForHTTPState(HTTPState::MovedPermanently);
		else if (loc->redirect.statusCode == 302)
			updateForHTTPState(HTTPState::Found);
		
		// test
		std::cout << "!!!Location header is now: "
			<< headers["Location"] << std::endl;
		
		return;
	}

	// --- Special Case for /upload: generate autoindex if index file is requested --- 
	if (filePath == "www/upload/upload_index.html" || filePath == "www/upload/upload_index.html/")
	{
		std::ifstream file(filePath.c_str());
		if (!file.is_open())
		{
			handleErrorPages(HTTPState::NotFound);
			return;
		}

		std::stringstream buffer;
		buffer << file.rdbuf();
		std::string html = buffer.str();
		file.close();

		// Generate dynamic autoindex list
		std::string fileList = generateUploadAutoindex("www/upload");

		// Replace placeholder
		size_t pos = html.find("<div id=\"files\"></div>");
		if (pos != std::string::npos)
		{
			html.replace(pos, std::string("<div id=\"files\"></div>").length(), fileList);
		}

		body = html;
		headers["Content-Type"] = "text/html";
		headers["Content-Length"] = std::to_string(body.size());
		updateForHTTPState(HTTPState::Ok);
		return;
	}

	// --- Special case for /images: generates dynamic gallery based on contents of images directory ---
	if (filePath == "www/html/images/images_index.html" || filePath == "www/html/images/images_index.html/")
	{
		std::ifstream file(filePath);
		if (!file.is_open()) 
		{ 
			handleErrorPages(HTTPState::NotFound); 
			return; 
		}

		std::stringstream buffer;
		buffer << file.rdbuf();
		std::string html = buffer.str();
		file.close();

		// Inject gallery into placeholder
		std::string gallery = generateImagesGallery("www/html/images");

		size_t pos = html.find("<div class=\"gallery\" id=\"images\">");
		if (pos != std::string::npos)
		{
			// Insert gallery right after opening <div>
			html.replace(pos + std::string("<div class=\"gallery\" id=\"images\">").length(),
						0, gallery);
		}
		
		body = html;
		headers["Content-Type"] = "text/html";
		headers["Content-Length"] = std::to_string(body.size());
		updateForHTTPState(HTTPState::Ok);
		return;
	}

	// --- Normal file handling ---
	std::ifstream file(filePath, std::ios::binary); // Treats the file as binary.
	if (!file.is_open())
	{
		handleErrorPages(HTTPState::NotFound);
		return;
	}

	std::stringstream buffer;
	buffer << file.rdbuf();
	body = buffer.str();
	file.close();

	headers["Content-Type"] = parseContentType(filePath);
	headers["Content-Length"] = std::to_string(body.size());
	updateForHTTPState(HTTPState::Ok);
}

/**
 * @brief Handles HTTP POST request for file uploads.
 *
 * @details
 * - Validates POST is allowed for requested location.
 * - Verifies upload directory, body presence and max body size.
 * - Detects if upload is multipart/form-data (webform) or RAW/terminal.
 * - Normalizes file extension to lowercase to check against forbidden and allowed lists.
 * - Generates filename if not provided and sanatizes it to prevent directory traversal.
 * - Multipart upload:
 *   	- Extract filename, content and extension (filename may be missing)
 * - RAW upload:
 * 		- Filename is not provided (metadata isn't sent in RAW uploads)
 * 		- Detect extension via Content-Type header or magic bytes.
 * - Rejects forbidden extensions and defaults unknown/missing extensions to `.bin` (unexecutable generic binary extension for unknown files).
 * - Saves the uploaded file to the specified directory.
 * - Returns 201 Created and redirects to `/upload/` to update autoindex.
 */
void HTTPResponse::handlePOST(const HTTPRequest& request, const std::string& uploadDir)
{
	// --- Check if POST method is allowed for location ---
	const LocationParse* location = serverParse.get_best_location(request.resourcePath);
	if (!location || std::find(location->allowedMethods.begin(), location->allowedMethods.end(), HTTPMethod::POST) == location->allowedMethods.end())
	{
		perror("POST method not allowed for this location");
		handleErrorPages(HTTPState::MethodNotAllowed);
		return;
	}
	
	// --- Validate upload directory, body and size ---
	if (uploadDir.empty() || request.body.empty() || request.body.size() > serverParse.maxBodySize)
	{
		handleErrorPages(request.body.empty() ? HTTPState::BadRequest : HTTPState::RequestTooLarge);
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
		{	headers["Location"] = "/upload/"; 

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
		
		// test, remove later
		std::cout << "Extracted - multipart - filename: " << fileName << std::endl;
		std::cout << "Extracted - multipart - extension: " << ext << std::endl;
	}
	
	// --- Generate filename if not present ---
	if (fileName.empty())
	{
		fileName = generateUploadFilename("upload_");
	}
	else
	{
		// --- Sanitize: remove directory traversal ---
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
			ext = ".bin"; // generi
	}

	// --- Add extension to filename ---
	fileName += ext; // add extension to filename

	// --- File path to save the upload to ---
	std::string filePath = uploadDir + fileName;
	std::cout << "Saving uploaded file to: " << filePath << std::endl;
	std::cout << "FileName is: " << fileName << std::endl;

	// --- Save/write file ---
	std::ofstream outFile(filePath, std::ios::binary);
	if (!outFile.is_open())
	{
		std::cout << "Couldn't create post" << std::endl;
		handleErrorPages(HTTPState::InternalServerError);
		return ;
	}
	
	outFile.write(fileData.c_str(), fileData.size());
	if (!outFile.good())
	{
		std::cout << "couldn't write to file" << std::endl;
		outFile.close();
		handleErrorPages(HTTPState::InternalServerError);
		return ;
	}

	outFile.close();
	
	// --- Successfull: 201 created an dredirect to show updated autoindex ---
	headers["Location"] = "/upload/"; 
	updateForHTTPState(HTTPState::Created);
}

/**
 * @brief Handles an HTTP DELETE request.
 *
 * @details
 * - Deletes the specified file from the server if it exists.
 * - Returns 204 No Content on successful deletion.
 * - Returns 404 Not Found if the file does not exist.
 * - Returns 403 Forbidden or 500 Internal Server Error if deletion fails (e.g., permissions).
 * - Does not return a response body for successful deletions.
 */
void HTTPResponse::handleDELETE(const HTTPRequest& request, const std::string& filePath)
{
	// --- Check if DELETE method is allowed for this location ---
	const LocationParse* location = serverParse.get_best_location(request.resourcePath);
	if (std::find(location->allowedMethods.begin(), location->allowedMethods.end(), HTTPMethod::DELETE) == location->allowedMethods.end())
	{
		perror("DELETE method not allowed for this location");
		handleErrorPages(HTTPState::MethodNotAllowed);
		return;
	}

	// --- Prevent deletion of HTML files ---
	if (filePath.size() >= 5 && filePath.substr(filePath.size() - 5) == ".html")
	{
		perror("Attempted to delete an HTML file");
		handleErrorPages(HTTPState::Forbidden);
		return;
	}

	// --- Check if file exists ---
	if (!serverParse.file_exists(filePath) || filePath.empty())
	{
		handleErrorPages(HTTPState::NotFound);
		return;
	}

	// --- Check if file is a directory (forbidden to delete) ---
	if (serverParse.is_directory(filePath))
	{
		perror("Attempted to delete a directory");
		handleErrorPages(HTTPState::Forbidden);
		return;
	}

	// --- Attempt to delete the file ---
	if (std::remove(filePath.c_str()) != 0)		// deletion failed
	{
		// Could fail due to permissions or being a directory
		perror("Error deleting file");
		handleErrorPages(HTTPState::Forbidden);
		return;
	}

	// --- Successfull deletion ---
	std::cout << "File deleted successfully: " << filePath << std::endl;
	body.clear();
	headers["Content-Type"] = "text/plain";
	headers["Content-Length"] = std::to_string(body.size());
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
	// --- Check if file exists ---
	if (!serverParse.file_exists(filePath))
	{
		handleErrorPages(HTTPState::NotFound);
		return;
	}

	// --- Set headers if file exists ---
	std::ifstream file(filePath, std::ios::binary);
	if (!file.is_open())
	{
		handleErrorPages(HTTPState::Forbidden);
		return;
	}

	// --- Get file size for Content-Length header ---
	file.seekg(0, std::ios::end);			// Move the read pointer to end of the file
	std::streampos fileSize = file.tellg();	// Get current pos -> this is the byte sized file
	file.close();

	body.clear(); // No body for HEAD response
	headers["Content-Type"] = parseContentType(filePath);
	headers["Content-Length"] = std::to_string(fileSize);
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
	std::cout << "in HandleErrorPages with state: " << state << std::endl;
	updateForHTTPState(state);
	std::cout << "Error page is given: " << state << std::endl;
	
	// Path to error HTLM pages
	std::string errorPath = "www/errors/" + statusCode + ".html";
	std::ifstream file(errorPath, std::ios::binary);

	// --- If the error page exists, server it ---
	if (file.is_open())
	{
		std::stringstream buffer;
		buffer << file.rdbuf();
		body = buffer.str();
		file.close();

		// Set the content-type for HTML error pages
		headers["Content-Type"] = "text/html";
		headers["Content-Length"] = std::to_string(body.size());
	}
	else	// --- If the error page is missing, serve a simple plain-text message ---
	{
		body = statusCode + ": " + reasonPhrase;
		headers["Content-Type"] = "text/plain";
		headers["Content-Length"] = std::to_string(body.size());
	}
}
