/* See LICENSE file for copyright and license details. */

#include <syslog.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <poll.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <errno.h>

#include "util.h"
#include "smtp.h"
#include "conf.h"

#define PORT 5000

static struct str sender_local;
/* Empty sender_domain means no sender specified yet. */
static struct str sender_domain;

static void reset(void)
{
	clrstr(&sender_local);
	clrstr(&sender_domain);
}

static void disconnect(void)
{
	reset();
	exit(0);
}

/* Session-specific handling of I/O errors. */
static void sioerr(const char *func)
{
	switch (errno) {
	case EPIPE:
	case ECONNRESET: case ETIMEDOUT:
		disconnect();
		break;
	default:
		ioerr(func);
		break;
	}
}

static void ereply1(char *code, char *arg1)
{
	/* TODO error checking */
	printf("%s %s\r\n", code, arg1);
	fflush(stdout);
}

static void ereply2(char *code, char *arg1, char *arg2)
{
	/* TODO error checking */
	printf("%s %s %s\r\n", code, arg1, arg2);
	fflush(stdout);
}

int readline(char line[], int *len)
{
	static int cr = 0;
	int i = 0, max = *len;
	if (cr) line[i++] = '\r';
	while (i < max) {
		errno = 0;
		int c = getchar();
		if (c == EOF) {
			if (errno) sioerr("getchar");
			else disconnect();
		}
		line[i++] = c;
		if (cr && c == '\n') {
			cr = 0;
			*len = i;
			return 1;
		}
		cr = (c == '\r');
	}
	if (cr) --i;
	*len = i;
	return 0;
}

static void readcommand(char line[], int *len)
{
	for (;;) {
		if (readline(line, len)) return;
		while (!readline(line, len)) {}
		ereply1("500", "Line too long");
	}
}

/* SMTP server-specific parsing functions. See smtp.h for conventions. */

static int phelo(struct str *domain)
{
	return pchar(' ') && pdomain(domain) && pcrlf();
}

static int pmail(struct str *local, struct str *domain)
{
	int s =  pchar(' ') && pword("FROM") && pchar(':');
	s = s && pchar('<') && pmailbox(local, domain) && pchar('>');
	return   pcrlf();
}

static int prcpt(struct str *local, struct str *domain)
{
	int s =  pchar(' ') && pword("TO") && pchar(':');
	s = s && pchar('<') && pmailbox(local, domain) && pchar('>');
	return   pcrlf();
}

static void dohelo(int ext)
{
	(void) ext;
	struct str domain;
	mkstr(&domain, 16);
	if (phelo(&domain)) {
		syslog(LOG_MAIL | LOG_INFO,
			"Incoming connection from <%.*s>.",
			(int) domain.len, domain.data);
		ereply1("250", conf.domain);
	} else {
		ereply1("501", "Syntax Error");
	}
	clrstr(&domain);
}

static void domail(void)
{
	if (sender_domain.len > 0) {
		ereply1("503", "Bad Sequence");
		return;
	}
	struct str local, domain;
	mkstr(&local, 16);
	mkstr(&domain, 16);
	if (pmail(&local, &domain)) {
		clrstr(&sender_local);
		clrstr(&sender_domain);
		sender_local = local;
		sender_domain = domain;
		ereply1("250", "OK");
	} else {
		clrstr(&local);
		clrstr(&domain);
		ereply1("501", "Syntax Error");
	}
}

static void dorcpt(void)
{
	if (sender_domain.len == 0) {
		ereply1("503", "Bad Sequence");
		return;
	}
	struct str local, domain;
	mkstr(&local, 16);
	mkstr(&domain, 16);
	if (prcpt(&local, &domain)) {
		ereply1("250", "OK");
	} else {
		ereply1("501", "Syntax Error");
	}
	clrstr(&local);
	clrstr(&domain);
}

static void dodata(void)
{
	if (!pcrlf()) {
		ereply1("501", "Syntax Error");
	}
	ereply1("354", "Listening");
	for (;;) {
		char line[128];
		int len = sizeof(line);
		if (readline(line, &len) && line[0] == '.') {
			if (len == 3) break;
			fprintf(stderr, "%.*s", len-1, line+1);
		} else {
			fprintf(stderr, "%.*s", len, line);
		}
	}
	ereply1("250", "OK");
}

static void cmdloop(void)
{
	/* TODO termination handler */

	ereply2("220", conf.domain, "Ready");
	for (;;) {
		char line[128];
		int len = sizeof(line);
		readcommand(line, &len);
		cphead = line;
		if (pword("HELO")) {
			dohelo(0);
		} else if (pword("EHLO")) {
			dohelo(1);
		} else if (pword("MAIL")) {
			domail();
		} else if (pword("RCPT")) {
			dorcpt();
		} else if (pword("DATA")) {
			dodata();
		} else if (pword("NOOP")) {
			if (pcrlf()) {
				ereply1("250", "OK");
			} else {
				ereply1("501", "Syntax Error");
			}
		} else if (pword("RSET")) {
			if (pcrlf()) {
				reset();
				ereply1("250", "OK");
			} else {
				ereply1("501", "Syntax Error");
			}
		} else if (pword("QUIT")) {
			if (pcrlf()) {
				ereply2("221", conf.domain, "Bye");
				disconnect();
			} else {
				ereply1("501", "Syntax Error");
			}
		} else {
			ereply1("500", "Unknown Command");
		}
	}
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

void server(void)
{
	/* TODO termination handler */

	int sock = openmsock(PORT);

	struct pollfd pfds[1];
	memset(pfds, 0, sizeof(pfds));
	pfds[0].fd = sock;
	pfds[0].events = POLLIN;

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
			cmdloop();
		}
		close(s);
	}

	close(sock);
}

