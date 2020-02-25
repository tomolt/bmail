#include <syslog.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
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
	struct str outq;
	int socket;
	int mode;
};

static struct session session;

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
		session.socket = accept(sock, NULL, NULL);
		mkstr(&session.outq, 64); /* TODO error checking! */
		session.mode = CHATTING;
		/* TODO log accept errno problems? */
		ereply2("220", conf.domain, "Ready");
		while (session.mode != QUITTING) {
			ssize_t s = write(session.socket, session.outq.data, session.outq.len);
			free(session.outq.data);
			session.outq.cap = 0;
			session.outq.len = 0;
			mkstr(&session.outq, 64);
			if (s < 0) {
				session.mode = QUITTING;
			}
			struct str line;
			if (mkstr(&line, 128) < 0) {
				session.mode = QUITTING;
				continue;
			}
			if (recviline(session.socket, &line) < 0) {
				free(line.data);
				session.mode = QUITTING;
				continue;
			}
			/* TODO we need a write timeout as well! */
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
			free(line.data);
		}
		write(session.socket, session.outq.data, session.outq.len);
		free(session.outq.data);
		session.outq.cap = 0;
		session.outq.len = 0;
		mkstr(&session.outq, 64);
		close(session.socket);
	}

	close(sock);
}

