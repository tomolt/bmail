#include <syslog.h>
#include <stdio.h>
#include <stdlib.h>
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
	int socket;
	int mode;
};

static struct session session;

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
		syslog(LOG_MAIL | LOG_INFO, "Incoming connection from <%.*s>.", (int) domain.len, domain.data);
		dprintf(session.socket, "250 %s\r\n", conf.domain);
	} else {
		dprintf(session.socket, "501 Syntax Error\r\n");
	}
	free(domain.data);
}

void domail(char **ptr)
{
	struct str local, domain;
	mkstr(&local, 16);
	mkstr(&domain, 16);
	if (pmail(ptr, &local, &domain)) {
		dprintf(session.socket, "250 OK\r\n");
	} else {
		dprintf(session.socket, "501 Syntax Error\r\n");
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
		dprintf(session.socket, "250 OK\r\n");
	} else {
		dprintf(session.socket, "501 Syntax Error\r\n");
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
			dprintf(session.socket, "354 Listening\r\n");
		} else {
			dprintf(session.socket, "501 Syntax Error\r\n");
		}
	} else if (pword(ptr, "NOOP")) {
		if (pcrlf(ptr)) {
			dprintf(session.socket, "250 OK\r\n");
		} else {
			dprintf(session.socket, "501 Syntax Error\r\n");
		}
	} else if (pword(ptr, "QUIT")) {
		if (pcrlf(ptr)) {
			session.mode = QUITTING;
			dprintf(session.socket, "221 %s Bye\r\n", conf.domain);
		} else {
			dprintf(session.socket, "501 Syntax Error\r\n");
		}
	} else {
		dprintf(session.socket, "500 Unknown Command\r\n");
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
		/* TODO log accept errno problems? */
		dprintf(session.socket, "220 %s Ready\r\n", conf.domain);
		session.mode = CHATTING;
		while (session.mode != QUITTING) {
			struct str line;
			if (getiline(session.socket, &line) < 0) {
				free(line.data);
				break;
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
						dprintf(session.socket, "250 OK\r\n");
						session.mode = CHATTING;
					} else {
						printf("%.*s", (int) line.len - 1, line.data + 1);
					}
				}
				break;
			}
			free(line.data);
		}
		close(session.socket);
	}

	close(sock);
}

