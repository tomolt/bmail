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

#define INDATA 0x01
#define ZOMBIE 0x02
#define DEAD   0x04

struct session
{
	struct pollfd pfd;
	struct str inq;
	struct str outq;
	struct str sender_local;
	struct str sender_domain;
	int socket;
	int flags;
};

static struct session session;

void ioerr(const char *func)
{
	switch (errno) {
	case EINTR: case EAGAIN:
#if EAGAIN != EWOULDBLOCK
	case EWOULDBLOCK:
#endif
	case ECONNABORTED:
		break;
	case EMFILE: case ENFILE:
		syslog(LOG_MAIL | LOG_WARNING, "Running out of file descriptors.");
		break;
	case ENOBUFS: case ENOMEM:
		syslog(LOG_MAIL | LOG_WARNING, "Running out of kernel memory.");
		break;
	case ENETDOWN: case ENETUNREACH:
		syslog(LOG_MAIL | LOG_WARNING, "Network is unreachable.");
		break;
	default:
		syslog(LOG_MAIL | LOG_CRIT, "Bug: %s:", func);
		syslog(LOG_MAIL | LOG_CRIT, "  %s", strerror(errno));
		break;
	}
}

void sioerr(const char *func)
{
	switch (errno) {
	case EPIPE:
		session.flags |= ZOMBIE;
		break;
	case ECONNRESET: case ETIMEDOUT:
		session.flags |= DEAD;
		break;
	default:
		ioerr(func);
		break;
	}
}

int initsession(int fd)
{
	memset(&session, 0, sizeof(session));
	if (mkstr(&session.inq, 128) < 0) return -1;
	if (mkstr(&session.outq, 128) < 0) return -1;
	session.socket = fd;
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
	free(session.sender_local);
	free(session.sender_domain);
	close(session.socket);
}

void ereply1(char *code, char *arg1)
{
	if (strext(&session.outq, strlen(code), code) < 0 ||
		strput(&session.outq,  ' ') < 0 ||
		strext(&session.outq, strlen(arg1), arg1) < 0 ||
		strput(&session.outq, '\r') < 0 ||
		strput(&session.outq, '\n') < 0) {
		session.flags |= ZOMBIE;
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
		session.flags |= ZOMBIE;
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
	if (session.sender_domain.len > 0) {
		ereply1("503", "Bad Sequence");
		return;
	}
	struct str local, domain;
	mkstr(&local, 16);
	mkstr(&domain, 16);
	if (pmail(ptr, &local, &domain)) {
		free(session.sender_local.data);
		free(session.sender_domain.data);
		session.sender_local = local;
		session.sender_domain = domain;
		ereply1("250", "OK");
	} else {
		free(local.data);
		free(domain.data);
		ereply1("501", "Syntax Error");
	}
}

void dorcpt(char **ptr)
{
	if (session.sender_domain.len == 0) {
		ereply1("503", "Bad Sequence");
		return;
	}
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
			session.flags |= INDATA;
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
	} else if (pword(ptr, "RSET")) {
		if (pcrlf(ptr)) {
			free(session.sender_domain.data);
			ereply1("250", "OK");
		} else {
			ereply1("501", "Syntax Error");
		}
	} else if (pword(ptr, "QUIT")) {
		if (pcrlf(ptr)) {
			session.flags |= ZOMBIE;
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
	if (session.flags & INDATA) {
		if (line.data[0] != '.') {
			printf("%.*s", (int) line.len, line.data);
		} else {
			if (line.data[1] == '\r' && line.data[2] == '\n') {
				session.flags &= ~INDATA;
				ereply1("250", "OK");
			} else {
				printf("%.*s", (int) line.len-1, line.data+1);
			}
		}
	} else {
		char *cur = line.data;
		docommand(&cur);
	}
}

void deqlines(void)
{
	char cr;
nextline:
	cr = 0;
	for (size_t i = 0; i < session.inq.len; ++i) {
		char ch = session.inq.data[i];
		if (cr && ch == '\n') {
			doturn(session.inq);
			strdeq(&session.inq, i+1);
			goto nextline;
		}
		cr = (ch == '\r');
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
		while (!(session.flags & DEAD)) {
			int s = poll(&session.pfd, 1, -1);
			if (s < 0) {
				ioerr("poll");
			} else {
				if (session.pfd.revents & POLLIN) {
					char buf[128];
					ssize_t s = read(session.socket, buf, 128);
					if (s < 0) {
						sioerr("read");
					} else if (s > 0) {
						if (strext(&session.inq, s, buf) < 0) session.flags |= ZOMBIE;
						deqlines();
					} else {
						session.flags |= ZOMBIE;
					}
				}
				
				if (session.pfd.revents & POLLOUT) {
					ssize_t s = write(session.socket, session.outq.data, session.outq.len);
					if (s < 0) {
						sioerr("write");
					} else {
						strdeq(&session.outq, s);
					}
				}

				if (!(session.flags & ZOMBIE)) session.pfd.events = POLLIN;
				if (session.outq.len > 0) {
					session.pfd.events |= POLLOUT;
				} else {
					if (session.flags & ZOMBIE) session.flags |= DEAD;
				}
			}
		}
		freesession();
	}

	close(sock);
}

