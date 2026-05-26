*This project has been created as part of the 42 curriculum by diwalaku, akaya-oz, rbom.*

# 🌐 Webserv

> A custom HTTP/1.1 web server built in C++ using non-blocking sockets and epoll.

# 📚 Table of Contents

- [Description](#-description)
- [Requests](#-requests)
- [Features](#-features)
- [Instructions](#-instructions)
- [Testing](#-testing)
- [Technical Choices](#-technical-choices)
- [Resources](#-resources)
- [AI Usage](#-ai-usage)

## 📖 Description

Webserv is a project that's about creating your own custom HTTP web server written in C++, developed as part of the 42 curriculum.

The goal of this project is to understand how web servers work internally by recreating the core behavior of a real HTTP server from scratch. This includes handling client connections, parsing HTTP requests, serving static files, executing CGI scripts, managing multiple clients simultaneously, and generating valid HTTP responses.

HTTP (HyperText Transfer Protocol) is the protocol used to send and receive information over the internet.
When you click a link or submit a form, your browser sends an HTTP request, to which the server responds.

The web server can handle HTTP `GET`, `POST` and `DELETE` requests, and serve static files or dynamic content using CGI. 

The server is built using non-blocking sockets and an event-driven architecture based on `epoll`, allowing it to efficiently handle multiple connections at the same time.

# 📡 Requests

## GET
Used for read-only requests. It doesn't alter the server's state, but will return a representation of the resource.

## POST
Is mostly used for creating new resources.
Is able to alter the server's state.

## DELETE
Deletes a specified resource.
Is able to alter the server's state. 

## 🧩 CGI
CGI (Common Gateway Interface) is a standard for running external programs from a web server.
When the user sends a CGI request, the web server executes the program and returns the output to the user's web browser.
They use scripts that can be written in any programming language (Python, PHP, Perl, bash) and are used to process data submitted by a user through a web browser.

### Key Features

- HTTP request handling
- Non-blocking I/O with `epoll`
- Static file serving
- CGI execution
- File uploads
- Configurable virtual servers and routes
- Custom error pages
- Support for multiple HTTP methods
- Directory listing (autoindex)
- Request routing system
- Chunked and partial write handling
- Persistent connections

---

# ⚙️ Features

## Configuration File

The server uses a configration file similar to nginx.

Example:
``` conf
server {
	listen 8080;
	server_name localhost;

	location / {
		root www
		index index.html;
	}
} 
```

## HTTP

- GET, POST, and DELETE support
- HEAD request handling
- HTTP status code handling
- Header parsing
- Request body parsing

## Server Architecture

- Event-driven server loop
- Non-blocking sockets
- `epoll`-based connection management
- Partial read/write handling
- Client connection persistence

## Routing & Configuration

- Multiple virtual servers
- Route matching
- Method restrictions
- Redirect handling
- Upload directory support
- CGI route support
- Autoindex support

## CGI

- CGI script execution
- Environment variable setup
- POST body forwarding
- CGI output parsing

---

# 🛠️ Instructions

## Requirements
- Linux environment
- C++ compiler with C++20 support
- make

## Installation
Clone the repository
```bash
git clone https://github.com/DidiWalakutty/Webserv.git
cd webserv
```

## Compile the project
```bash
make
```

## Run the server with a configuration file
```bash
./webserv config/default.conf
```
If no configuration file is provided, the default configuration that was implemented will be used.

## Open in browser
```
http://localhost:8080
```
---

# 🧪 Testing

## Test with curl:
``` bash
curl http://localhost:8080
```

## Test with curl: image content (outputs raw data safely)
``` bash
curl http://localhost:8080/images/bird.png --output bird.png
```

## POST request
``` bash
curl -X POST -d "hello world" http://localhost:8080/upload
```

## 📤 POST file
``` bash
curl -i -X POST -F "file=@test.txt" http://localhost:8080/upload
```

## Delete request (existing file)
``` bash
curl -X DELETE http://localhost:8080/upload/<file.txt>
```

## CGI
``` bash
curl -i http://localhost:8080/cgi-bin/<cgifile.py>
```

## Redirect
``` bash
curl -i http://localhost:8080/redirect
```

## Second Server
``` bash
curl -i http://localhost:4040/
```

## Error Pages
``` bash
curl -i http://localhost:8080/non-existent
```

## Upload file that's too big
First, change the `max_body_size` in the configuration file to 1 (1 byte), and upload
a file that's bigger than 1 ASCII character (= 1 byte).
Make sure you restart the web server.
``` bash
curl -X POST --data "1234567890" http://127.0.0.1:8080/upload
```

---

# 🧠 Technical Choices

## Event-Driven Architecture

The server uses non-blocking sockets together with epoll to efficiently manage multiple simultaneous client connections without creating a thread per client.

## Request Routing

Requests are first parsed and then passed through a routing system which determines:

- matching server block
- matching location block
- allowed methods
- redirects
- CGI execution
- filesystem targets

## Response Handling

Responses are generated separately from routing logic to keep responsibilities isolated and maintainable.

Large responses and partial socket writes are handled through pending write buffers and its offsets.

## CGI Execution
CGI scripts are executed through `fork()` and `execve()` with pipes used for communication between the server and CGI process.

# 📚 Resources

## HTTP & Networking
- [Linux `epoll` documentation](https://man7.org/linux/man-pages/man7/epoll.7.html)
- [Nginx documentation](https://nginx.org/en/)
- [w3schools: HTTP Request Methods](https://www.w3schools.com/tags/ref_httpmethods.asp)
- [GeeksForGeeks: what is HTTP](https://www.geeksforgeeks.org/html/what-is-http/)
- [YouTube: Web Server Concepts and Examples](https://www.youtube.com/watch?v=9J1nJOivdyw&t=40s)
- [Mozilla: HTTP Response Status Codes](https://developer.mozilla.org/en-US/docs/Web/HTTP/Reference/Status)
- [GeeksForGeeks Web Programming in C++](https://www.geeksforgeeks.org/cpp/web-programming-in-c/)
- [GeeksForGeeks HTML Parser in C/C++](https://www.geeksforgeeks.org/html/html-parser-in-c-cpp/)
- [ScrapingAnt C++ HTML Parsing](https://scrapingant.com/blog/c-plus-plus-parse-html)

## Guides
- [Medium - webserv: Building a Non-Blocking Web Server in C++98](https://m4nnb3ll.medium.com/webserv-building-a-non-blocking-web-server-in-c-98-a-42-project-04c7365e4ec7)
- [alimnaqvi](https://www.alimnaqvi.com/blog/webserv)

## 🤖 AI Usage
AI tools were used as learning and debugging assistant during development.
Examples hereof include:
- Understanding HTTP protocol behavior
- Debugging of certain issues
- Discussing architecture and design choices
- Reviewing edge cases for request parsing
- Clarifying CGI behavior and its process
- Improving documentation and readability

# 👥 Team
- [Didi Walakutty](https://github.com/DidiWalakutty) 
- [Goksu Ozsan](https://github.com/goksuko)
- [Reinier Bom](https://github.com/ReinierBom)