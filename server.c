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

/* Session flags: */
/* Currently receiving the body of a message. */
#define INDATA 0x01
/* Session should be closed after flushing I/O. */
#define ZOMBIE 0x02
/* Session should be closed immediately. */
#define DEAD   0x04

struct session
{
	struct pollfd pfd;
	struct str inq;
	struct str outq;
	struct str sender_local;
	/* Empty sender_domain means no sender specified yet. */
	struct str sender_domain;
	int socket;
	int flags;
};

static struct session bsession;
/* Pointer to the currently active session. */
static struct session *csession = &bsession;

/* Session-specific handling of I/O errors. */
void sioerr(const char *func)
{
	switch (errno) {
	case EPIPE:
		csession->flags |= ZOMBIE;
		break;
	case ECONNRESET: case ETIMEDOUT:
		csession->flags |= DEAD;
		break;
	default:
		ioerr(func);
		break;
	}
}

int initsession(int fd)
{
	memset(csession, 0, sizeof(struct session));
	if (mkstr(&csession->inq, 128) < 0) return -1;
	if (mkstr(&csession->outq, 128) < 0) return -1;
	csession->socket = fd;
	int flags = fcntl(fd, F_GETFL, 0);
	if (flags < 0) return -1;
	flags = fcntl(fd, F_SETFL, flags | O_NONBLOCK);
	if (flags < 0) return -1;
	csession->pfd.fd = fd;
	csession->pfd.events = POLLIN | POLLOUT;
	return 0;
}

void freesession(void)
{
	clrstr(&csession->inq);
	clrstr(&csession->outq);
	clrstr(&csession->sender_local);
	clrstr(&csession->sender_domain);
	close(csession->socket);
}

void sread(void)
{
	char buf[128];
	ssize_t s = read(csession->socket, buf, 128);
	if (s < 0) {
		sioerr("read");
	} else if (s == 0) {
		csession->flags |= ZOMBIE;
	} else {
		if (strext(&csession->inq, s, buf) < 0) {
			csession->flags |= ZOMBIE;
		}
	}
}

void swrite(void)
{
	ssize_t s = write(csession->socket, csession->outq.data, csession->outq.len);
	if (s < 0) {
		sioerr("write");
	} else {
		strdeq(&csession->outq, s);
	}
}

void ereply1(char *code, char *arg1)
{
	if (strext(&csession->outq, strlen(code), code) < 0 ||
		strput(&csession->outq,  ' ') < 0 ||
		strext(&csession->outq, strlen(arg1), arg1) < 0 ||
		strput(&csession->outq, '\r') < 0 ||
		strput(&csession->outq, '\n') < 0) {
		csession->flags |= ZOMBIE;
	}
}

void ereply2(char *code, char *arg1, char *arg2)
{
	if (strext(&csession->outq, strlen(code), code) < 0 ||
		strput(&csession->outq,  ' ') < 0 ||
		strext(&csession->outq, strlen(arg1), arg1) < 0 ||
		strput(&csession->outq,  ' ') < 0 ||
		strext(&csession->outq, strlen(arg2), arg2) < 0 ||
		strput(&csession->outq, '\r') < 0 ||
		strput(&csession->outq, '\n') < 0) {
		csession->flags |= ZOMBIE;
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
	if (csession->sender_domain.len > 0) {
		ereply1("503", "Bad Sequence");
		return;
	}
	struct str local, domain;
	mkstr(&local, 16);
	mkstr(&domain, 16);
	if (pmail(&local, &domain)) {
		clrstr(&csession->sender_local);
		clrstr(&csession->sender_domain);
		csession->sender_local = local;
		csession->sender_domain = domain;
		ereply1("250", "OK");
	} else {
		clrstr(&local);
		clrstr(&domain);
		ereply1("501", "Syntax Error");
	}
}

void dorcpt(void)
{
	if (csession->sender_domain.len == 0) {
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
	free(local.data);
	free(domain.data);
}

void docommand(void)
{
	if (pword("HELO")) {
		dohelo(0);
	} else if (pword("EHLO")) {
		dohelo(1);
	} else if (pword("MAIL")) {
		domail();
	} else if (pword("RCPT")) {
		dorcpt();
	} else if (pword("DATA")) {
		if (pcrlf()) {
			csession->flags |= INDATA;
			ereply1("354", "Listening");
		} else {
			ereply1("501", "Syntax Error");
		}
	} else if (pword("NOOP")) {
		if (pcrlf()) {
			ereply1("250", "OK");
		} else {
			ereply1("501", "Syntax Error");
		}
	} else if (pword("RSET")) {
		if (pcrlf()) {
			free(csession->sender_domain.data);
			ereply1("250", "OK");
		} else {
			ereply1("501", "Syntax Error");
		}
	} else if (pword("QUIT")) {
		if (pcrlf()) {
			csession->flags |= ZOMBIE;
			ereply2("221", conf.domain, "Bye");
		} else {
			ereply1("501", "Syntax Error");
		}
	} else {
		ereply1("500", "Unknown Command");
	}
}

void doturn(struct str line)
{
	if (csession->flags & INDATA) {
		if (line.data[0] != '.') {
			printf("%.*s", (int) line.len, line.data);
		} else {
			if (line.data[1] == '\r' && line.data[2] == '\n') {
				csession->flags &= ~INDATA;
				ereply1("250", "OK");
			} else {
				printf("%.*s", (int) line.len-1, line.data+1);
			}
		}
	} else {
		cphead = line.data;
		docommand();
		cphead = NULL;
	}
}

void deqlines(void)
{
	char cr;
nextline:
	cr = 0;
	for (size_t i = 0; i < csession->inq.len; ++i) {
		char ch = csession->inq.data[i];
		if (cr && ch == '\n') {
			doturn(csession->inq);
			strdeq(&csession->inq, i+1);
			goto nextline;
		}
		cr = (ch == '\r');
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
		if (initsession(s) < 0) continue;
		ereply2("220", conf.domain, "Ready");
		while (!(csession->flags & DEAD)) {
			int s = poll(&csession->pfd, 1, -1);
			if (s < 0) {
				ioerr("poll");
			} else {
				if (csession->pfd.revents & POLLIN) {
					sread();
					deqlines();
				}
				
				if (csession->pfd.revents & POLLOUT) {
					swrite();
				}

				if (!(csession->flags & ZOMBIE)) csession->pfd.events = POLLIN;
				if (csession->outq.len > 0) {
					csession->pfd.events |= POLLOUT;
				} else {
					if (csession->flags & ZOMBIE) csession->flags |= DEAD;
				}
			}
		}
		freesession();
	}

	close(sock);
}

