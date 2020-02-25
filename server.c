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

#include "util.h"
#include "smtp.h"
#include "conf.h"

#define PORT 5000

enum { CHATTING, LISTENING, QUITTING };

struct session
{
	struct pollfd pfd;
	struct str inq;
	struct str outq;
	int socket;
	int mode;
};

static struct session session;

int initsession(int fd)
{
	if (mkstr(&session.inq, 128) < 0) return -1;
	if (mkstr(&session.outq, 128) < 0) return -1;
	session.socket = fd;
	session.mode = CHATTING;
	int flags = fcntl(fd, F_GETFL, 0);
	if (flags < 0) return -1;
	flags = fcntl(fd, F_SETFL, flags | O_NONBLOCK);
	if (flags < 0) return -1;
	session.pfd.fd = fd;
	session.pfd.events = POLLIN | POLLOUT;
	return 0;
}

void freesession(void)
{
	free(session.inq.data);
	free(session.outq.data);
	close(session.socket);
}

void ereply1(char *code, char *arg1)
{
	if (strext(&session.outq, strlen(code), code) < 0 ||
		strput(&session.outq,  ' ') < 0 ||
		strext(&session.outq, strlen(arg1), arg1) < 0 ||
		strput(&session.outq, '\r') < 0 ||
		strput(&session.outq, '\n') < 0) {
		session.mode = QUITTING;
	}
}

void ereply2(char *code, char *arg1, char *arg2)
{
	if (strext(&session.outq, strlen(code), code) < 0 ||
		strput(&session.outq,  ' ') < 0 ||
		strext(&session.outq, strlen(arg1), arg1) < 0 ||
		strput(&session.outq,  ' ') < 0 ||
		strext(&session.outq, strlen(arg2), arg2) < 0 ||
		strput(&session.outq, '\r') < 0 ||
		strput(&session.outq, '\n') < 0) {
		session.mode = QUITTING;
	}
}

int phelo(char **ptr, struct str *domain)
{
	return pchar(ptr, ' ') && pdomain(ptr, domain) && pcrlf(ptr);
}

int pmail(char **ptr, struct str *local, struct str *domain)
{
	int s =  pchar(ptr, ' ') && pword(ptr, "FROM") && pchar(ptr, ':');
	s = s && pchar(ptr, '<') && pmailbox(ptr, local, domain) && pchar(ptr, '>');
	return   pcrlf(ptr);
}

int prcpt(char **ptr, struct str *local, struct str *domain)
{
	int s =  pchar(ptr, ' ') && pword(ptr, "TO") && pchar(ptr, ':');
	s = s && pchar(ptr, '<') && pmailbox(ptr, local, domain) && pchar(ptr, '>');
	return   pcrlf(ptr);
}

void dohelo(char **ptr, int ext)
{
	(void) ext;
	struct str domain;
	mkstr(&domain, 16);
	if (phelo(ptr, &domain)) {
		syslog(LOG_MAIL | LOG_INFO,
			"Incoming connection from <%.*s>.",
			(int) domain.len, domain.data);
		ereply1("250", conf.domain);
	} else {
		ereply1("501", "Syntax Error");
	}
	free(domain.data);
}

void domail(char **ptr)
{
	struct str local, domain;
	mkstr(&local, 16);
	mkstr(&domain, 16);
	if (pmail(ptr, &local, &domain)) {
		ereply1("250", "OK");
	} else {
		ereply1("501", "Syntax Error");
	}
	free(local.data);
	free(domain.data);
}

void dorcpt(char **ptr)
{
	struct str local, domain;
	mkstr(&local, 16);
	mkstr(&domain, 16);
	if (prcpt(ptr, &local, &domain)) {
		ereply1("250", "OK");
	} else {
		ereply1("501", "Syntax Error");
	}
	free(local.data);
	free(domain.data);
}

void docommand(char **ptr)
{
	if (pword(ptr, "HELO")) {
		dohelo(ptr, 0);
	} else if (pword(ptr, "EHLO")) {
		dohelo(ptr, 1);
	} else if (pword(ptr, "MAIL")) {
		domail(ptr);
	} else if (pword(ptr, "RCPT")) {
		dorcpt(ptr);
	} else if (pword(ptr, "DATA")) {
		if (pcrlf(ptr)) {
			session.mode = LISTENING;
			ereply1("354", "Listening");
		} else {
			ereply1("501", "Syntax Error");
		}
	} else if (pword(ptr, "NOOP")) {
		if (pcrlf(ptr)) {
			ereply1("250", "OK");
		} else {
			ereply1("501", "Syntax Error");
		}
	} else if (pword(ptr, "QUIT")) {
		if (pcrlf(ptr)) {
			session.mode = QUITTING;
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
	char *cur;
	switch (session.mode) {
	case CHATTING:
		cur = line.data;
		docommand(&cur);
		break;
	case LISTENING:
		if (line.data[0] != '.') {
			printf("%.*s", (int) line.len, line.data);
		} else {
			if (line.data[1] == '\r' && line.data[2] == '\n') {
				session.mode = CHATTING;
				ereply1("250", "OK");
			} else {
				printf("%.*s", (int) line.len-1, line.data+1);
			}
		}
		break;
	}
}

int deqlines(void)
{
	char cr;
nextline:
	cr = 0;
	for (size_t i = 0; i < session.inq.len; ++i) {
		char ch = session.inq.data[i];
		if (cr && ch == '\n') {
			doturn(session.inq);
			if (strdeq(&session.inq, i+1) < 0) return -1;
			goto nextline;
		}
		cr = (ch == '\r');
	}
	return 0;
}

void server(void)
{
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

	for (;;) {
		int fd = accept(sock, NULL, NULL);
		if (fd < 0) continue;
		if (initsession(fd) < 0) continue;
		/* TODO log accept errno problems? */
		ereply2("220", conf.domain, "Ready");
		for (;;) {
			int s = poll(&session.pfd, 1, -1);
			if (s < 0) goto endofsession;

			if (session.pfd.revents & POLLIN) {
				char buf[128];
				ssize_t s = read(session.socket, buf, 128);
				if (s < 0) goto endofsession;
				if (strext(&session.inq, s, buf) < 0) goto endofsession;
				if (deqlines() < 0) goto endofsession;
			}
			
			if (session.pfd.revents & POLLOUT) {
				ssize_t s = write(session.socket, session.outq.data, session.outq.len);
				if (s < 0) goto endofsession;
				if (strdeq(&session.outq, s) < 0) goto endofsession;
			}

			session.pfd.events = POLLIN;
			if (session.outq.len > 0) {
				session.pfd.events |= POLLOUT;
			} else {
				if (session.mode == QUITTING) break;
			}
		}
	endofsession:
		freesession();
	}

	close(sock);
}

