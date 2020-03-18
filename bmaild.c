/* See LICENSE file for copyright and license details. */

#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <netdb.h>
#include <fcntl.h>
#include <poll.h>

#include "util.h"

#define MAX_SOCKS 10

static const char *ports[] = { "25", "587", NULL };

static int socks[MAX_SOCKS];
static struct pollfd pfds[MAX_SOCKS];
static int nsocks;

/* Intentionally empty argument list to allow cleanup() to be used as a signal handler. */
static void cleanup()
{
	for (int i = 0; i < nsocks; ++i)
		close(socks[i]);
}

int main()
{
	const int yes = 1;
	for (int p = 0; ports[p] != NULL; ++p) {
		struct addrinfo hints, *list, *ai;
		/* List all plausible addresses to listen on */
		memset(&hints, 0, sizeof(hints));
		hints.ai_family = AF_UNSPEC;
		hints.ai_socktype = SOCK_STREAM;
		hints.ai_flags = AI_PASSIVE;
		int eai = getaddrinfo(NULL, ports[p], &hints, &list);
		if (eai != 0) die("getaddrinfo: %s", gai_strerror(eai));
		/* Open sockets for all addresses */
		for (ai = list; ai != NULL; ai = ai->ai_next) {
			if (nsocks >= MAX_SOCKS) die("Trying to open too many sockets.");
			int sock = socket(ai->ai_family, ai->ai_socktype, ai->ai_protocol);
			if (sock < 0) die("socket:");
			/* Get rid of "Address already in use" problems */
			if (setsockopt(sock, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof(yes)) < 0)
				die("Can't enable address reuse:");
			/* Disable ipv4 tunneling through ipv6, as it's not entirely portable. */
			if (ai->ai_family == AF_INET6) {
				if (setsockopt(sock, IPPROTO_IPV6, IPV6_V6ONLY, &yes, sizeof(yes)) < 0)
					die("Can't disable ipv4-mapped ipv6:");
			}
			/* Configure socket to be non-blocking. */
			int flags = fcntl(sock, F_GETFL, 0);
			if (flags < 0) die("fcntl:");
			flags = fcntl(sock, F_SETFL, flags | O_NONBLOCK);
			if (flags < 0) die("fcntl:");
			/* Bind and listen */
			if (bind(sock, ai->ai_addr, ai->ai_addrlen) < 0) die("bind:");
			if (listen(sock, 8) < 0) die("listen:");
			/* Initialize poll file descriptor */
			struct pollfd pfd = { 0 };
			pfd.fd = sock;
			pfd.events = POLLIN;
			/* Add to socket array */
			pfds[nsocks] = pfd;
			socks[nsocks] = sock;
			++nsocks;
		}
		freeaddrinfo(list);
	}
	/* General process configuration. */
	setpgid(0, 0);
	reapchildren();
	handlesignals(cleanup);
	atexit(cleanup);
	for (;;) {
		if (poll(pfds, nsocks, -1) < 0) {
			ioerr("poll");
			continue;
		}
		for (int i = 0; i < nsocks; ++i) {
			if (!(pfds[i].revents & POLLIN)) continue;
			int s = accept(socks[i], NULL, NULL);
			if (s < 0) {
				ioerr("accept");
				continue;
			}
			pid_t pid = fork();
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
	}
	return 0;
}

