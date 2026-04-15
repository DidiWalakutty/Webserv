#include "HTTPResponse.hpp"
#include "dirent.h"

 /**
  * @brief Generates an HTML gallery page for all images in the /images directory.
  * 
  * @param imagesDir = the path to the images directory ("www/html/images")
  * @return std::string = the full HTML page as a string
  * 
  * @details
  * - Scans the specified directory for image files with the following extensions: .png, .jp(e)g, .gif.
  * - Ignores "." and ".." entries, to avoid listing current and parent directory.
  * - For each image, adds a thumbgnail wrapped in a clickable <a> link to the full image.
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
        if (name == "." || name == "..") continue;

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
 * @brief Generates an HTML autoindex page for all uploaded files in the /upload directory.
 *
 * @details
 * - Opens the specified upload directory.
 * - Iterates over all entries except "." and "..", to avoid listing current + parent directory.
 * - Ignores any .html files to prevent accidental deletion of the autoindex page itself.
 * - Generates an HTML page containing clickable links <a> for each file.
 * - Adds a delete button next to each file, which triggers a DELETE request to remove the file from the server.
 * - Used to provide directory listing for GET /upload.
 * - Does not perform permission or method checks.
 */
std::string HTTPResponse::generateUploadAutoindex(const std::string& uploadDir)
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
           << "<a href=\"/upload/" << name << "\" target=\"_blank\">" << name << "</a>"
           << "<button class=\"delete-btn\" "
           << "onclick=\"deleteFile('" << name << "')\">Delete</button>"
           << "</div>";
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
        else if (ct == "application/x-sh") 
			ext = ".sh";
        else if (ct == "application/x-php") 
			ext = ".php";
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
	// Check if file path is empty
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
	// Check if file path is empty
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
	// Check if file path is empty
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

	// Check if file is readable and executable
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
	// Check if path is empty
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
		handleErrorPages(HTTPState::Forbidden);
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