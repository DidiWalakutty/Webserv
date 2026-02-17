        Webserv
** Updates, To Do and Questions **

Updates:
0.  Changed the makefile to C++20, because computers currently   can't work with highter. Editor must see which c++ we use. Update the c_cpp_properties.json
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
8.	Made the constuctor for HTTPResponse pass the server to respect
	location, roots etc.

To Do:
1.	Start():
	Select the appropriate server based on host header -> needs extra testing, so connect to server we want to use and check.
2.	parseResponseStr():
	We might need to check (somewhere) that in case of POST or PUT, the file was actually
	Shouldnt update headers if already done in functions.
3.	isValidBody():
	Always returns true, so needs checks.
4.  Server():
	POST uploads work correctly, but the server still logs an error after finishing the request. This happens because after responding, the client closes the connection and the server tries to parse an empty read as a new HTTP request. That triggers "Empty (raw) request string" even though nothing failed.
	" Removed FD: 6
	  Failed to parse HTTP request: Empty (raw) request string" even if it does everything correctly (uploading a .txt file).
	So the server logs a failure after it was successful.

Questions:
1.  Which HTTP Methods do we accept for HTTP Requests?
	The subjects says: "You need at least the GET, POST, and DELETE methods."
	Currently, request accepts as well: PUT and HEAD too
	How will we handle those?
2.	What headers are required for HTTP Requests?
	- Host
	- Content Lenght
	Do we accept any others? If so, which and how to handle those?
3.	In Start(), I added a RemoveClient() when we   	couldn't parse the HTTP Request. Good, because it's an issue when we even get in that statement?
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


