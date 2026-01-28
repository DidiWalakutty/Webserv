#include "HTTPResponse.hpp"

// Do we want 403 and 500?
// Treats files as binary, exactly as is on disk.
void HTTPResponse::handleGET(const HTTPRequest& request, const std::string& filePath)
{
	std::cout << "in GET" << std::endl;
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
}

void HTTPResponse::handlePOST(const HTTPRequest& request, const std::string& uploadDir)
{
	std::cout << "in POST" << std::endl;
	std::cout << "POST body size: " << request.body.size() << std::endl;
	std::cout << "uploaddir: " << uploadDir << std::endl;
	if (uploadDir.empty())
	{
		handleErrorPages(HTTPState::InternalServerError);
		return ;
	}

	// Check if body is empty
	if (request.body.empty())
	{
		std::cout << "body is empty" << std::endl;
		// a request server can't parse (invalid HTTP headers, corrupted request body)
		handleErrorPages(HTTPState::BadRequest);
		return ;
	}

	// --- Unique filename for the upload ---
	std::string fileName = "upload_" + std::to_string(std::time(nullptr));
	std::string filePath = uploadDir + fileName;

	std::cout << "Filename: " << fileName << std::endl;
	std::cout << "Uploading to: " << filePath << std::endl;

	// --- Write the body to the file ---
	std::ofstream outFile(filePath, std::ios::binary);

	// Checks if creation was successful
	if (!outFile.is_open())
	{
		std::cout << "couldnt create" << std::endl;
		handleErrorPages(HTTPState::InternalServerError);
		return ;
	}

	outFile << request.body;
	if (!outFile.good())
	{
		std::cout << "couldn't good it" << std::endl;
		outFile.close();
		handleErrorPages(HTTPState::InternalServerError);
		return ;
	}

	// --- Successfull creation
	outFile.close();

	body = "File uploaded as: " + fileName;
	updateForHTTPState(HTTPState::Created);
	headers["Content-Type"] = "text/plain";
}

void HTTPResponse::handleDELETE(const HTTPRequest& request, const std::string& filePath)
{
	// should check if the file/location isn't already empty.
	// check for rights???
	// delete
}

void HTTPResponse::handleHEAD(const HTTPRequest& request, const std::string& filePath)
{

}


// Looks for an HTML file based on the status code.
// If exists, reads it into body, otherwise uses a simple fallback message.
void HTTPResponse::handleErrorPages(HTTPState state)
{
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
