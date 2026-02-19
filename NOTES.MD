        Webserv
** Updates, To Do and Questions **

Updates:
0.  Changed the makefile to C++20, because computers currently can't work with highter. Editor must see which c++ we use, so I updated the c_cpp_properties.json
1.	Server::Server(): 
	Webserv constructor now receives all parsed + validated .conf info in ServerParse and LocationParse blocks.
	In the constructor, you can now also see which server is running on which sockets.
2.	CreateSockets():
	Now uses the parsed .conf information via ServerParse.
	Each server block has one port and host.
	Now support multipe servers via _servers vector.
	Able to print to see which IP is bound to which socket.
3.	AddClient():
	Able to print and see which client FD was accepted from which IP + Port
4.	ReadClient():
	Resized buffer at end to size to actual received data (not too much if only a little bit)
5.	Start():
	Select the appropriate server based on host header -> NEEDS TESTING, so connect to server we want to use and check
6.	buildResponse():
	- Reset state first: clear headers and body to prevent leftovers from previous responses.
	- Set default HTTPState to Ok.
	- Updated the filepath to better location matching + validating if that filepath exists.
	- If directory, and it's GET or HEAD, we need to serve the file in the index, so also gets the right location block.
	- If any other HTTPMethod, we keep the filePath as is.
	- Now does early return on errors, otherwise we'd overwrite a state.
	- Previously we passed a statusMessage into parseResponseStr, but that value always stayed 404.
	- We did update the statusCode, but didn't send that to parseResponseStr.
	- Directory handling doesn’t early-return on errors, which made headers/body get overwritten consistently.
7.	parseResponseStr():
	- Now uses statusCode of class and is a formatter only.
	- Created functions to Handle the given HTTP Request
8.	Made the constuctor for HTTPResponse pass the server to respect
	location, roots etc.
9.	HandleGET():
	Handles the GET requests. When GET is for /images, it'll generate a dynamic gallery based on the contents of the image directory. Will also update when evaluator adds a new picture to those static files. Just refresh the page.
	Does the same for /upload, incl upload form, where it'll show an autoindex + deletion button (not for html files).
10.	HandlePOST():
	Checks if we may upload, if it's not too big etc.
	Will check the extension and create a unique filename + correct extension.
11.	HandleDELETE():
	Deletes the specified file from the server if it exists.
	Can't delete directories.
12.	HandleErrorPages():
	Finds path to correct error html page, if not existing, serve a simple plain-text message
13.	HandleHEAD():
	Returns header identical to GET, but doesn't include the file body.
14.	Created index.HTML:
	for homepage (index.html), and locations: images/ and upload/

Summarized To Do List: -> hopefully a little clearer
1. Request reading is incomplete (EPOLL/read()).
	- Only reads 512 bytes max, doesn't read the full request.
	- Treads each read() as a full request
	- Parses incomplete requests because of it
	This makes the POST body truncated, multipart uploading (website/form) is not possible, gives false parse errors and uploads fail.
	When using curl, it only does up to a certain extent, but through multipart not at all.
2.	Heading parsing too strict
	- Rejects modern browser headers (Sec-Fetch-* etc)
	- Also unable to delete uploads through website, because it doesn't recognize the header.
	- Throws on unknown headers
	- Causes ERR_EMPTY_RESPONSE
	Needs to accept any "Header-Name: Value", shouldn't throw.
3. Multipart no implemented
	- Only supports raw binary POST, up to a certain size
	- Can't parse multipart boundaries
	- Fails for HTML form upload
	- Needs to extract boundary from Content-Type
	- Needs to split body by boundary
	- Needs to extract: filename, file content
	- Save file content only
	This is separate from request reading.
4.	Empty request logged after succes
	- Gives false error after successful upload
	- Also when: client closes connection, server reads empty 
	- parseRequest(raw.empty()) throws
5.	isValidBody() -> always returns true
6.	Protect index.html from DELETE -> Didi's part
7.	Create HTML pages for error pages -> Didi's part
8.	Adjust HTML pages for upload and images -> Didi's part
9.	Add no one can delete .html pages when in upload/ -> Didi's part
10.	Add CGI extension -> Didi's part
11.	Test if right server is chosen based on Host + Port, location block -> Didi's part
12. Test longest prefix matching -> Didi's part
13. Check allowed methods -> Didi's part
14.	What's stress-testing exactly?
15. Max number of client (sockets)?
16. If != isServerSocket, we still need to addClient + close immediately


Summary:
The main issue currently is that we only read 512 bytes per request and parse immediately, instead of buffering until the full HTTP request (headers + body) is received. This breaks multipart/form-data uploads and larger POST bodies.
Header parsing is also too strict and rejects modern browser headers.

To Do: -> written when found, so a bit messy but may have more details
1.	Start():
	Select the appropriate server based on host header -> needs extra testing, so connect to server we want to use and check.
2.	parseResponseStr():
	We might need to check (somewhere) that in case of POST or PUT, the file was actually
	Shouldnt update headers if already done in functions.
3.	isValidBody():
	Always returns true, so needs checks.
4.  Server()/parseRequest():
	If (raw.empty()) logs false empty requests, even if POST was handled correctly.
	POST uploads work correctly through curl, but the server still logs an error after finishing the request. This happens because after responding, the client closes the connection and the server tries to parse an empty read as a new HTTP request. That triggers "Empty (raw) request string" even though nothing failed.
	" Removed FD: 6
	  Failed to parse HTTP request: Empty (raw) request string" even if it does everything correctly (uploading a .txt file).
	So the server logs a failure after it was successful.
5.  Issue with parsing a HTTP request in Start() at EPOLLIN. 
	Currently not reading the full request, just a max of 512 bytes we just read.
	It acts as if it's the complete HTTP request, parses it, and we don't receive the full body. parseRequest() sees incomplete body, so handles POST as empty body.
	Currently treating each read() as one request, instead of handling the full request until the end. So we need to keep reading until the end of the request/body.
	So it does work for small GET requests, but breaks for POST uploads. So we need to do the complete request and parse only once. Perhaps a per-client buffer that reads untill full request is received?
6.  Make sure upload_index.html can't be deleted -> handleDELETE
7.  In parseRequest(): Our validate header part is too strict for the website. It currently throws an error for any header that doesn’t exactly match "Name: Value". Modern browsers send extra headers like Sec-Fetch-Site or Sec-Fetch-Mode, which are valid but trigger Invalid header line errors. This causes ERR_EMPTY_RESPONSE on /upload and similar endpoints on the website. The parsing should accept any Header-Name: Value, trim whitespace, and stop only at the empty line that ends the header section. Unknown headers should not throw — they can be stored in the map or ignored. Parser needs to accept modern headers.
8.	In parseRequest(): Currently only parses plain binary POSTS, but not multipart/form-data as body type (HTML form).
	Doesn't work with multipart/form-data (how browsers send files): the body is split into parts with headers and file bytes. Current parser reads headers line by line and expects the regular headers, making mutlipart headers break it.
	Does work for plain binary POSTs (curl --data-, sends a raw HTTP body), and will fail when > 512 bytes. Summary: needs proper multipart parsing to extract files and handle boundaries.

Questions:
1.  Which HTTP Methods do we accept for HTTP Requests?
	The subjects says: "You need at least the GET, POST, and DELETE methods."
	Currently, request accepts as well: PUT and HEAD too
2.	What headers are required for HTTP Requests?
	- Host
	- Content Lenght
3.	In Start(), I added a RemoveClient() when we couldn't parse the HTTP Request. Good? Because it's an issue when we even get in that statement?
4. Is there a way to add a '\n' for the client side when it says the request has been fulfilled?	


Testing:
1. Upload works correctly, but always gives error.
	Tested file uploading with curl: 'curl -X POST http://localhost:8080/upload --data-binary @test.txt'
	where test.txt contains "Hello from POST upload".
	Result on client side is good -> 201: The request has been fulfilled, and a new resource is created
	
	But the result on server side not good ->
	" Added FD: 6

	Read FD: 6
	Reading body...
	Method: POST
	Resource Path: /upload

	POST body size: 23
	uploaddir: www/upload/index.html
	Filename: upload_1770652928
	Uploading to: www/upload/index.htmlupload_1770652928

	-!!!- Removed FD: 6
	-!!!- Failed to parse HTTP request: Empty (raw) request string "
	----> Even though the upload succeeds, the server still prints: Failed to parse....
	Which happens after the response is already built + file is written/uploaded.
	The server loop tries to parse again after the client already closed the connection.
	That produces an empty read, which then throws the error, but nothing has actually failed.
2.	Test website for /upload and /redirect. Won't be able to until parser + reading full request is    updated.

