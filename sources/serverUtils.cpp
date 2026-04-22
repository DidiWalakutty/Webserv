/* ************************************************************************** */
/*                                                                            */
/*                                                        ::::::::            */
/*   serverUtils.cpp                                    :+:    :+:            */
/*                                                     +:+                    */
/*   By: diwalaku <diwalaku@codam.student.nl>         +#+                     */
/*                                                   +#+                      */
/*   Created: 2026/04/15 21:27:32 by diwalaku      #+#    #+#                 */
/*   Updated: 2026/04/22 18:21:39 by rbom          ########   odam.nl         */
/*                                                                            */
/* ************************************************************************** */

#include "server.hpp"

HTTPState Server::checkCGIAccess(const std::string& filePath)
{
	struct stat st;

	// --- 1. Check if file exists ---
	if (access(filePath.c_str(), F_OK) != 0)
		return HTTPState::NotFound;

	// --- 2. Get file info ---
	if (stat(filePath.c_str(), &st) != 0)
		return HTTPState::InternalServerError;

	// --- 3. Reject directories ---
	if (S_ISDIR(st.st_mode))
		return HTTPState::Forbidden;

	// --- 4. Check read permission ---
	if (access(filePath.c_str(), R_OK) != 0)
		return HTTPState::Forbidden;

	// --- 5. Check execute permission ---
	if (access(filePath.c_str(), X_OK) != 0)
		return HTTPState::Forbidden;

	// Needed??? --- 6. Prevents checking sockets, pipes etc
	if (!S_ISREG(st.st_mode))
		return HTTPState::Forbidden;

	return HTTPState::Ok;
}

bool Server::IsCGIRequest(const HTTPRequest& request, const ServerParse& server,
                          std::string& filePath, const LocationParse*& location)
{
	// --- 1. Find matching location ---
	location = server.get_best_location(request.resourcePath);
	if (!location)
		return false;

	// --- 2. Must be marked as CGI ---
	if (!location->is_cgi)
		return false;

	// --- 3. Build filesystem path ---
	filePath = server.build_filesystem_path(request.resourcePath);
	if (filePath.empty())
		return false;

	// --- 4. Check extension ---
	size_t dot = filePath.find_last_of('.');
	if (dot == std::string::npos)
		return false;

	std::string ext = filePath.substr(dot);
	if (ext != location->cgi_extension)
		return false;

	return true;
}

void Server::QueueResponse(int clientFD, const HTTPRequest& request, const std::string& responseStr)
{
	auto connIt = request.headers.find("CONNECTION");
	bool clientWantsClose = (connIt != request.headers.end() &&
	                         connIt->second.find("close") != std::string::npos);
	bool http10 = (request.protocolVersion == HTTPProtocolVersion::HTTP_1_0);
	closeAfterWrite[clientFD] = (clientWantsClose || http10);

	pendingWrites[clientFD] = responseStr;
	writeOffsets[clientFD] = 0;

	epoll_event writeEv{};
	writeEv.events = EPOLLOUT;
	writeEv.data.fd = clientFD;
	if (epoll_ctl(epollFD, EPOLL_CTL_MOD, clientFD, &writeEv) < 0)
	{
		std::cerr << "Failed to register EPOLLOUT for client: " << clientFD << std::endl;
		RemoveClient(clientFD);
	}
}