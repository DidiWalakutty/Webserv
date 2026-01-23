        Webserv
** Updates, To Do and Questions **

Updates:
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
	Updated the filepath to better location matching + validating if that filepath exists
	If directory, we need to serve the file in the index, so also gets the right location block.

To Do:
1.	Start():
	Select the appropriate server based on host header -> NEEDS TESTING, so connect to server we want to use and check.
2.	parseResponseStr():
	We might need to check (somewhere) that in case of POST or PUT, the file was actually
	created/updated and set state to created -> 201?
3.	isValidBody():
	Always returns true, so needs checks.

Questions:
1.  Which HTTP Methods do we accept for HTTP Requests?
	The subjects says: "You need at least the GET, POST, and DELETE methods."
	Currently, request accepts as well: PUT and HEAD too
	How will we handle those?
2.	What headers are required for HTTP Requests?
	- Host
	- Content Lenght
	Do we accept any others? If so, which and how to handle those?
3.	In Start(), I added a RemoveClient() when we couldn't parse the HTTP Request. Good?