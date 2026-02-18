#include "HTTPResponse.hpp"
#include "dirent.h"

/**
 * @brief Generates a dynamic HTML gallery page for all image files in a directory.
 *
 * @param imagesDir The path to the directory containing image files.
 * @return std::string The full HTML page as a string.
 *
 * @details
 * - Scans the given directory for files with .png, .jpg, .jpeg, or .gif extensions.
 * - Ignores "." and ".." entries.
 * - For each image, adds a thumbnail wrapped in a clickable <a> link to the full image.
 * - Includes basic CSS styling for layout, thumbnails, hover effect, and filenames.
 * - Returns the complete HTML page as a string, ready to be sent as the HTTP response body.
 * 
 * @note This function automatically reflects any new images added to the directory without 
 *       needing to update the HTML manually.
 */
std::string generateImagesGallery(const std::string& imagesDir) 
{
    std::string html;
    html += "<!DOCTYPE html><html lang=\"en\"><head><meta charset=\"UTF-8\">"
            "<meta name=\"viewport\" content=\"width=device-width, initial-scale=1.0\">"
            "<title>Image Gallery</title><style>"
            "body{font-family:sans-serif;padding:30px;background:#f0f2f5;}"
            "h1{text-align:center;margin-bottom:30px;}"
            ".gallery{display:flex;flex-wrap:wrap;gap:20px;justify-content:center;}"
            ".gallery img{width:180px;height:180px;object-fit:cover;border-radius:12px;box-shadow:0 6px 12px rgba(0,0,0,0.08);transition:transform 0.2s;}"
            ".gallery img:hover{transform:scale(1.05);}"
            ".filename{text-align:center;font-size:14px;margin-top:6px;color:#555;}"
            "</style></head><body><h1>Image Gallery</h1><div class=\"gallery\">";

    DIR *dir = opendir(imagesDir.c_str());
    if (dir) {
        struct dirent *entry;
        while ((entry = readdir(dir)) != NULL) 
		{
            std::string name = entry->d_name;
            if (name == "." || name == "..") continue;
            // basic filter for images
            if (name.find(".png") != std::string::npos || name.find(".jpg") != std::string::npos ||
                name.find(".jpeg") != std::string::npos || name.find(".gif") != std::string::npos) 
				{
					html += "<div>";
					html += "<a href=\"/images/" + name + "\">";
					html += "<img src=\"/images/" + name + "\" alt=\"" + name + "\">";
					html += "</a>";
					html += "<div class=\"filename\">" + name + "</div>";
					html += "</div>";
		        }
        }
        closedir(dir);
    }

    html += "</div></body></html>";
    return html;
}

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
	std::cout << "in HandleGET" << std::endl;
	std::cout << "Requested file path: " << filePath << std::endl;
	
	// --- Special Case: /images --- 
	if (filePath == "www/html/images/images_index.html" || filePath == "www/html/images/images_index.html/")
	{
		std::string imagesDir = "www/html/images";
		body = generateImagesGallery(imagesDir);
		updateForHTTPState(HTTPState::Ok);
		headers["Content-Type"] = "text/html";
		headers["Content-Length"] = std::to_string(body.size());
		return;
	}

	// --- Normal file handling ---
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
	
	// --- Check if POST method is allowed for this location ---
	const LocationParse* location = serverParse.get_best_location(request.resourcePath);
	if (!location || std::find(location->allowedMethods.begin(), location->allowedMethods.end(), HTTPMethod::POST) == location->allowedMethods.end())
	{
		perror("POST method not allowed for this location");
		handleErrorPages(HTTPState::MethodNotAllowed);
		return;
	}
	
	if (uploadDir.empty() || request.body.empty() || request.body.size() > serverParse.maxBodySize)
	{
		handleErrorPages(request.body.empty() ? HTTPState::BadRequest : HTTPState::RequestTooLarge);
		return ;
	}
	
	// --- Detect file extension ---
	std::string ext = ".txt"; // default extension

	if (request.headers.count("Content-Type"))
	{
		std::string ct = request.headers.at("Content-Type");
		if (ct == "image/jpeg" || ct == "image/jpg")
			ext = ".jpg";
		else if (ct == "image/png")
			ext = ".png";
		else if (ct == "image/gif")
			ext = ".gif";
		else if (ct == "text/plain")
			ext = ".txt";
	}
	else if (request.body.size() >= 4)	// try magic bytes
	{
		unsigned char* data = reinterpret_cast<unsigned char*>(const_cast<char*>(request.body.data()));
		if (data[0] == 0xFF && data[1] == 0xD8 && data[2] == 0xFF)
			ext = ".jpg";
        else if (data[0] == 0x89 && data[1] == 0x50 && data[2] == 0x4E && data[3] == 0x47)
			ext = ".png";
        else if (data[0] == 0x47 && data[1] == 0x49 && data[2] == 0x46)   
			ext = ".gif";
	}
	
	// --- Unique filename for the upload: timestamp + static_counter ---
	std::string fileName = generateUploadFilename("upload_") + ext;
	std::string filePath = uploadDir + fileName;

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

	headers["Content-Type"] = parseContentType(fileName);
	body = "File uploaded as: " + fileName;
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
