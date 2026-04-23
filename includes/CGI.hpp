#pragma		once

#include	<signal.h>
#include	<string.h>
#include	<fcntl.h>
#include	<sys/wait.h>

#define		TIMEOUT_MS 5000

struct	CGI
{
	int			fd_stdin = -1;
	int			fd_stdout = -1;
	pid_t		pid = -1;
	bool		write_finished = false;
	std::string	body = "";
	ssize_t		body_size = 0;
	ssize_t		body_written = 0;
	bool		read_finished = false;
	bool		cgi_finished = false;
	time_t		start_time;
	std::string	output = "";
};
