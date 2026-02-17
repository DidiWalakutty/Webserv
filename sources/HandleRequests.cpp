#include "HTTPResponse.hpp"

/**
 * @brief Handles an HTTP GET request.
 *
 * @details
 * - Checks if the requested file exists on the server.
 * - Reads the file into memory (binary-safe, as is on disk) and sets it as the response body.
 * - Sets the Content-Type based on the file extension.
 * - If the file is missing, serves the appropriate error page (404).
 * - Only reads files; does not modify server state.
 */
void HTTPResponse::handleGET(const HTTPRequest& request, const std::string& filePath)
{
	std::cout << "in HandleGET" << std::endl;
	std::ifstream file(filePath, std::ios::binary); // Treats the file as binary.
	
	// File not found, serve 404 page
	if (!file.is_open())
	{
		handleErrorPages(HTTPState::NotFound);
		return;
	}

	std::stringstream buffer;
	buffer << file.rdbuf();
	body = buffer.str();
	file.close();

	updateForHTTPState(HTTPState::Ok);
	headers["Content-Type"] = parseContentType(filePath);
	headers["Content-Length"] = std::to_string(body.size());
}

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
	   << localTime->tm_min
	   << localTime->tm_sec;

	// --- Add static counter for uniqueness ---
	static int counter = 0;
	ss << "_" << counter++;

	return ss.str();
}


/**
 * @brief Handles an HTTP POST request (typically file uploads).
 *
 * @details
 * - Receives client data in the request body.
 * - Saves the data to the specified upload directory with a unique filename.
 * - Returns 201 Created on success, and includes the filename in the response body.
 * - Returns 400 Bad Request if the request body is empty.
 * - Returns 500 Internal Server Error if file creation or writing fails.
 * - Does not serve existing files; only stores new data.
 */
void HTTPResponse::handlePOST(const HTTPRequest& request, const std::string& uploadDir)
{
	std::cout << "in HandlePOST" << std::endl;
	// std::cout << "POST body size: " << request.body.size() << std::endl;
	// std::cout << "uploaddir: " << uploadDir << std::endl;
	
	// --- Check if POST method is allowed for this location ---
	const LocationParse* location = serverParse.get_best_location(request.resourcePath);
	if (std::find(location->allowedMethods.begin(), location->allowedMethods.end(), HTTPMethod::POST) == location->allowedMethods.end())
	{
		perror("POST method not allowed for this location");
		handleErrorPages(HTTPState::MethodNotAllowed);
		return;
	}
	
	if (uploadDir.empty())
	{
		handleErrorPages(HTTPState::InternalServerError);
		return ;
	}
	
	if (request.body.empty())
	{
		// a request server can't parse (invalid HTTP headers, corrupted request body)
		std::cout << "body is empty" << std::endl;
		handleErrorPages(HTTPState::BadRequest);
		return ;
	}

	if (request.body.size() > serverParse.maxBodySize)
	{
		std::cout << "Body size exceeds maximum allowed limit" << std::endl;
		handleErrorPages(HTTPState::RequestTooLarge); // 413
		return;
	}
	
	// --- Unique filename for the upload: timestamp + static_counter ---
	std::string fileName = generateUploadFilename("upload_");
	std::string filePath = uploadDir + fileName;

	// std::cout << "Filename: " << fileName << std::endl;
	// std::cout << "Uploading to: " << filePath << std::endl;

	// --- Write the body to the file ---
	std::ofstream outFile(filePath, std::ios::binary);

	// Checks if creation was successful
	if (!outFile.is_open())
	{
		std::cout << "Couldn't create post" << std::endl;
		handleErrorPages(HTTPState::InternalServerError);
		return ;
	}

	outFile << request.body;
	if (!outFile.good())
	{
		std::cout << "couldn't write to file" << std::endl;
		outFile.close();
		handleErrorPages(HTTPState::InternalServerError);
		return ;
	}

	// --- Successfull creation
	outFile.close();

	body = "File uploaded as: " + fileName;
	headers["Content-Type"] = "text/plain";
	headers["Content-Length"] = std::to_string(body.size());
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
	std::cout << "in handleDELETE" << std::endl;

	// --- Check if DELETE method is allowed for this location ---
	const LocationParse* location = serverParse.get_best_location(request.resourcePath);
	if (std::find(location->allowedMethods.begin(), location->allowedMethods.end(), HTTPMethod::DELETE) == location->allowedMethods.end())
	{
		perror("DELETE method not allowed for this location");
		handleErrorPages(HTTPState::MethodNotAllowed);
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
	if (std::remove(filePath.c_str()) != 0)	// deletes the file at filePath
	{
		// Could fail due to permissions or being a directory
		perror("Error deleting file");
		handleErrorPages(HTTPState::Forbidden);
		return;
	}

	// --- Successfull deletion ---
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
	std::cout << "in handleHEAD" << std::endl;

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

	// Fallback if error page is missing
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
	else
	{
		// Fallback message if HTML file isn't found
		body = statusCode + ": " + reasonPhrase;
		headers["Content-Type"] = "text/plain";
		headers["Content-Length"] = std::to_string(body.size());
	}
}
