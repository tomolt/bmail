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
static int sessock;

void disconnect(void)
{
	clrstr(&sender_local);
	clrstr(&sender_domain);
	close(sessock);
	exit(0);
}

/* Session-specific handling of I/O errors. */
void sioerr(const char *func)
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

void ereply1(char *code, char *arg1)
{
	/* TODO error checking */
	dprintf(sessock, "%s %s\r\n", code, arg1);
}

void ereply2(char *code, char *arg1, char *arg2)
{
	/* TODO error checking */
	dprintf(sessock, "%s %s %s\r\n", code, arg1, arg2);
}

int readline(char line[], int *len)
{
	static int cr = 0;
	int i = 0, max = *len;
	if (cr) line[i++] = '\r';
	while (i < max) {
		char c;
		/* TODO Work on a FILE* instead of file descriptor. */
		ssize_t s = read(sessock, &c, 1);
		if (s == 0) disconnect();
		if (s < 0) sioerr("read");
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

void readcommand(char line[], int *len)
{
	for (;;) {
		if (readline(line, len)) return;
		while (!readline(line, len)) {}
		ereply1("500", "Line too long");
	}
}

/* SMTP server-specific parsing functions. See smtp.h for conventions. */

int phelo(struct str *domain)
{
	return pchar(' ') && pdomain(domain) && pcrlf();
}

int pmail(struct str *local, struct str *domain)
{
	int s =  pchar(' ') && pword("FROM") && pchar(':');
	s = s && pchar('<') && pmailbox(local, domain) && pchar('>');
	return   pcrlf();
}

int prcpt(struct str *local, struct str *domain)
{
	int s =  pchar(' ') && pword("TO") && pchar(':');
	s = s && pchar('<') && pmailbox(local, domain) && pchar('>');
	return   pcrlf();
}

void dohelo(int ext)
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

void domail(void)
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

void dorcpt(void)
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

void dodata(void)
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
			printf("%.*s", len-1, line+1);
		} else {
			printf("%.*s", len, line);
		}
	}
	ereply1("250", "OK");
}

void cmdloop(int s)
{
	sessock = s;
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
				clrstr(&sender_local);
				clrstr(&sender_domain);
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

void server(void)
{
	/* Open the master socket. */
	int sock = socket(AF_INET6, SOCK_STREAM, 0);
	if (sock < 0) die("Can't open port %d:", PORT);

	struct sockaddr_in6 addr = { 0 };
	addr.sin6_family = AF_INET6;
	addr.sin6_port = htons(PORT);
	addr.sin6_addr = in6addr_any;

	int s = bind(sock, (struct sockaddr *) &addr, sizeof(addr));
	if (s < 0) die("Can't bind to socket:");
	s = listen(sock, 1);
	if (s < 0) die("Can't listen on socket:");

	int flags = fcntl(sock, F_GETFL, 0);
	if (flags < 0) die("Can't read socket flags:");
	flags = fcntl(sock, F_SETFL, flags | O_NONBLOCK);
	if (flags < 0) die("Can't switch socket to non-blocking:");

	struct pollfd pollfds[1];
	memset(pollfds, 0, sizeof(pollfds));
	pollfds[0].fd = sock;
	pollfds[0].events = POLLIN;

	for (;;) {
		int s = poll(pollfds, 1, -1);
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
		if (pid < 0) ioerr("fork");
		else if (pid == 0) cmdloop(s);
	}

	close(sock);
}

