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
    DIR *dir = opendir(imagesDir.c_str());
    if (!dir) return html;

    struct dirent *entry;
    while ((entry = readdir(dir)) != NULL) 
    {
        std::string name = entry->d_name;
        if (name == "." || name == "..") continue;

        if (name.find(".png") != std::string::npos || 
            name.find(".jpg") != std::string::npos ||
            name.find(".jpeg") != std::string::npos ||
            name.find(".gif") != std::string::npos) 
        {
            html += "<div class=\"image-item\">";
            html += "<img src=\"/images/" + name + "\" alt=\"" + name + "\">";
            html += "<div class=\"filename\">" + name + "</div>";
            html += "</div>";
        }
    }
    closedir(dir);
    return html;
}


/**
 * @brief Generates a simple HTML autoindex page for uploaded files.
 *
 * @details
 * - Opens the specified upload directory.
 * - Iterates over all entries except "." and "..".
 * - Generates an HTML page containing clickable links (<a>) for each file.
 * - Used to provide directory listing for GET /upload.
 * - Does not perform permission or method checks.
 */
std::string generateUploadAutoindex(const std::string& uploadDir)
{
   	DIR* dir = opendir(uploadDir.c_str());
	if (!dir)
        return "";

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
           << "<span>" << name << "</span>"
           << "<button class=\"delete-btn\" "
           << "onclick=\"deleteFile('" << name << "')\">Delete</button>"
           << "</div>";
    }

    closedir(dir);
    return ss.str();
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
		headers["LOCATION"] = loc->redirect.targetURL;
		headers["CONTENT-LENGTH"] = "0";

		if (loc->redirect.statusCode == 301)
			updateForHTTPState(HTTPState::MovedPermanently);
		else if (loc->redirect.statusCode == 302)
			updateForHTTPState(HTTPState::Found);
		
		// test
		std::cout << "!!!Location header is now: "
			<< headers["LOCATION"] << std::endl;
		
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
		headers["CONTENT-TYPE"] = "text/html";
		headers["CONTENT-LENGTH"] = std::to_string(body.size());
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
		headers["CONTENT-TYPE"] = "text/html";
		headers["CONTENT-LENGTH"] = std::to_string(body.size());
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

	headers["CONTENT-TYPE"] = parseContentType(filePath);
	headers["CONTENT-LENGTH"] = std::to_string(body.size());
	updateForHTTPState(HTTPState::Ok);
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
 * @brief Handles an HTTP POST request for file uploads.
 *
 * @details
 * - Validates that POST is allowed for the matched location.
 * - Verifies request body presence and maximum body size.
 * - Determines file extension using Content-Type header or magic bytes.
 * - Generates a unique filename and writes the raw body to disk.
 * - Returns 201 Created on success.
 * - Redirects back to /upload to show updated autoindex with new file.
 */
void HTTPResponse::handlePOST(const HTTPRequest& request, const std::string& uploadDir)
{
	// --- Check if POST method is allowed for this location ---
	const LocationParse* location = serverParse.get_best_location(request.resourcePath);
	if (!location || std::find(location->allowedMethods.begin(), location->allowedMethods.end(), HTTPMethod::POST) == location->allowedMethods.end())
	{
		perror("POST method not allowed for this location");
		handleErrorPages(HTTPState::MethodNotAllowed);
		return;
	}
	
	// --- Check if request is valid/possible for upload ---
	if (uploadDir.empty() || request.body.empty() || request.body.size() > serverParse.maxBodySize)
	{
		handleErrorPages(request.body.empty() ? HTTPState::BadRequest : HTTPState::RequestTooLarge);
		return ;
	}

	// --- Detect file extension ---
	std::string ext = ".txt"; // default extension

	if (request.headers.count("CONTENT-TYPE"))
	{
		std::string ct = request.headers.at("CONTENT-TYPE");
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
	
	// --- Unique filename for the upload: timestamp + extension ---
	std::string fileName = generateUploadFilename("upload_") + ext;
	std::string filePath = uploadDir + fileName;

	// --- Save file ---
	std::ofstream outFile(filePath, std::ios::binary);
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

	// --- Respond 201 and redirect back to GET /upload to show updated autoindex with new file ---
	headers["LOCATION"] = "/upload/"; 
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
	headers["CONTENT-TYPE"] = parseContentType(filePath);
	headers["CONTENT-LENGTH"] = std::to_string(fileSize);
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
	HTTPMessage statusMessage = HTTPCommon::HTTPStatusMap.at(state);
	
	// Path to error HTLM pages
	std::string errorPath = "www/html/errors/" + statusCode + ".html";
	std::ifstream file(errorPath, std::ios::binary);

	auto replaceAll = [](std::string& inout, const std::string& from, const std::string& to)
	{
		if (from.empty())
			return;
		size_t pos = 0;
		while ((pos = inout.find(from, pos)) != std::string::npos)
		{
			inout.replace(pos, from.size(), to);
			pos += to.size();
		}
	};

	// --- If the error page exists, server it ---
	if (file.is_open())
	{
		std::stringstream buffer;
		buffer << file.rdbuf();
		body = buffer.str();
		file.close();

		// Fill optional template placeholders (only if they exist in the HTML)
		replaceAll(body, "{{STATUS_CODE}}", statusMessage.code);
		replaceAll(body, "{{REASON_PHRASE}}", statusMessage.message);
		replaceAll(body, "{{DESCRIPTION}}", statusMessage.description);

		// Set the content-type for HTML error pages
		headers["CONTENT-TYPE"] = "text/html";
		headers["CONTENT-LENGTH"] = std::to_string(body.size());
	}
	else	// --- If the error page is missing, serve a simple plain-text message ---
	{
		body = statusCode + ": " + reasonPhrase;
		headers["CONTENT-TYPE"] = "text/plain";
		headers["CONTENT-LENGTH"] = std::to_string(body.size());
	}
}
