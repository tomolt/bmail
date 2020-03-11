/* See LICENSE file for copyright and license details. */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <syslog.h>
#include <unistd.h>
#include <fcntl.h>
#include <poll.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <arpa/inet.h>

#include "util.h"

static int sock;

static void cleanup(int sig)
{
	(void) sig;
	close(sock);
	exit(1);
}

static int openmsock(int port)
{
	/* Init TCP socket. */
	int sock = socket(AF_INET6, SOCK_STREAM, 0);
	if (sock < 0) die("Can't open port %d:", port);
	/* Fill in the listening address. */
	struct sockaddr_in6 addr = { 0 };
	addr.sin6_family = AF_INET6;
	addr.sin6_port = htons(port);
	addr.sin6_addr = in6addr_any;
	/* Configure socket to be non-blocking. */
	int flags = fcntl(sock, F_GETFL, 0);
	if (flags < 0) die("Can't read socket flags:");
	flags = fcntl(sock, F_SETFL, flags | O_NONBLOCK);
	if (flags < 0) die("Can't switch socket to non-blocking:");
	/* Open the socket for incoming connections. */
	int s = bind(sock, (struct sockaddr *) &addr, sizeof(addr));
	if (s < 0) die("Can't bind to socket:");
	s = listen(sock, 8);
	if (s < 0) die("Can't listen on socket:");
	return sock;
}

int main()
{
	/* Init master socket and prepare for polling. */
	sock = openmsock(25);
	struct pollfd pfds[1];
	memset(pfds, 0, sizeof(pfds));
	pfds[0].fd = sock;
	pfds[0].events = POLLIN;
	/* General process configuration. */
	setpgid(0, 0);
	handlesignals(cleanup);
	reapchildren();
	for (;;) {
		int s = poll(pfds, 1, -1);
		if (s < 0) {
			ioerr("poll");
			continue;
		}
		s = accept(sock, NULL, NULL);
		if (s < 0) {
			ioerr("accept");
			continue;
		}
		int pid = fork();
		if (pid < 0) {
			ioerr("fork");
		} else if (pid == 0) {
			/* Redirect child stdin & stdout to the socket. */
			if (dup2(s, 0) < 0 || dup2(s, 1) < 0) {
				ioerr("dup2");
				exit(0);
			}
			if (execlp("bmail_recv", "bmail_recv", NULL) < 0) {
				ioerr("execlp");
				exit(0);
			}
		}
		close(s);
	}
	closelog();
	return 0;
}

