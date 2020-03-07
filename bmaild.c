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
#include "arg.h"
#include "mbox.h"
#include "conf.h"

#define PORT 5000

char *argv0;
struct conf conf;
static int sock;

extern void recvmail(void);

static void cleanup(int sig)
{
	(void) sig;
	close(sock);
	exit(1);
}

static int openmsock(int port)
{
	updsequence();

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

/* Print usage and exit. */
static void usage(void)
{
	printf("usage: %s -D domain [-S spool]\n", argv0);
	exit(0);
}

int main(int argc, char *argv[])
{
	setpgid(0, 0);
	/* Handle command line arguments. */
	conf.spool = "/var/spool/mail";
	ARGBEGIN {
	case 'D':
		conf.domain = EARGF(usage());
		break;
	case 'S':
		conf.spool = EARGF(usage());
		break;
	default:
		usage();
	} ARGEND
	if (argc) usage();
	if (conf.domain == NULL) usage();
	/* Start logging. */
	openlog("bmaild", 0, LOG_MAIL);
	syslog(LOG_MAIL | LOG_INFO, "bmaild is starting up.");
	if (chdir(conf.spool) < 0) die("Can't chdir into spool:");
	/* Init master socket and prepare for polling. */
	sock = openmsock(PORT);
	struct pollfd pfds[1];
	memset(pfds, 0, sizeof(pfds));
	pfds[0].fd = sock;
	pfds[0].events = POLLIN;
	/* Set up termination handlers. */
	handlesignals(cleanup);
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
			if (dup2(s, 0) < 0) {
				ioerr("dup2");
				exit(0);
			}
			if (dup2(s, 1) < 0) {
				ioerr("dup2");
				exit(0);
			}
			recvmail();
		}
		close(s);
	}
	closelog();
	return 0;
}

