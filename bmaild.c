#include <syslog.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <assert.h>

#define PORT 5000
#define DOMAIN "bmaild.domain"

enum {
	HELO, EHLO, MAIL, RCPT, NOOP, QUIT
};

void die(const char *format, ...)
{
	va_list va;
	va_start(va, format);
	vsyslog(LOG_MAIL | LOG_EMERG, format, va);
	va_end(va);
	exit(-1);
}

char *getiline(int fd)
{
	size_t len = 0;
	size_t cap = 128;
	char *buf = malloc(cap);
	char cr = 0;
	for (;;) {
		char ch;
		ssize_t s = read(fd, &ch, 1);
		if (s <= 0) {
			free(buf);
			return NULL;
		}
		size_t idx = len++;
		if (len > cap) {
			cap *= 2;
			buf = realloc(buf, cap);
		}
		buf[idx] = ch;
		if (cr && ch == '\n') break;
		cr = (ch == '\r');
	}
	buf[len] = '\0';
	return buf;
}

int icmpadv(char **ptr, char *exp)
{
	char *cur = *ptr;
	do {
		char ec = *exp;
		assert(ec > 64 && ec < 91);
		char cc = *cur & 0xDF;
		if (ec != cc) return 0;
		++exp, ++cur;
	} while (*exp);
	*ptr = cur;
	return 1;
}

int islocalc(char c)
{
	/* !#$%&'*+-./09=?AZ^_`az{|}~ */
	if (c == 33) return 1;
	if (c > 34 && c < 40) return 1;
	if (c == 42 || c == 43) return 1;
	if (c > 44 && c < 58) return 1;
	if (c == 61) return 1;
	if (c > 64 && c <  91) return 1;
	if (c > 93 && c < 127) return 1;
	return 0;
}

int isaddrc(char c)
{
	/* anything except whitespace and [\] */
	if (c > 32 && c <  91) return 1;
	if (c > 93 && c < 127) return 1;
	return 0;
}

int isdomainc(char c)
{
	/* -.AZaz */
	if (c == 45 || c == 46) return 1;
	if (c > 64 && c <  91) return 1;
	if (c > 96 && c < 123) return 1;
	return 0;
}

int pcrlf(char **ptr)
{
	char *c = *ptr;
	if (*c != '\r') return 0;
	++c;
	if (*c != '\n') return 0;
	++c;
	*ptr = c;
	return 1;
}

int plocal(char **ptr)
{
	char *c = *ptr;
	/* TODO quoted local */
	if (!islocalc(*c)) return 0;
	do ++c;
	while (islocalc(*c));
	*ptr = c;
	return 1;
}

int pdomain(char **ptr)
{
	char *c = *ptr;
	if (*c == '[') {
		++c;
		while (isaddrc(*c)) ++c;
		if (*c != ']') return 0;
		++c;
	} else {
		if (!isdomainc(*c)) return 0;
		do ++c;
		while (isdomainc(*c));
	}
	*ptr = c;
	return 1;
}

int pmailbox(char **ptr)
{
	int s;
	s = plocal(ptr);
	if (!s) return 0;
	if (**ptr != '@') return 0;
	++*ptr;
	s = pdomain(ptr);
	if (!s) return 0;
	return 1;
}

int pcommand(char **ptr, int *cmd)
{
	if (icmpadv(ptr, "HELO")) {
		if (**ptr != ' ') return 0;
		++*ptr;
		if (!pdomain(ptr)) return 0;
		if (!pcrlf(ptr)) return 0;
		*cmd = HELO;
		return 1;
	}
	if (icmpadv(ptr, "EHLO")) {
		if (**ptr != ' ') return 0;
		++*ptr;
		if (!pdomain(ptr)) return 0;
		if (!pcrlf(ptr)) return 0;
		*cmd = EHLO;
		return 1;
	}
	if (icmpadv(ptr, "MAIL")) {
		if (**ptr != ' ') return 0;
		++*ptr;
		if (!icmpadv(ptr, "FROM")) return 0;
		if (**ptr != ':') return 0;
		++*ptr;
		if (**ptr != '<') return 0;
		++*ptr;
		if (!pmailbox(ptr)) return 0;
		if (**ptr != '>') return 0;
		++*ptr;
		if (!pcrlf(ptr)) return 0;
		*cmd = MAIL;
		return 1;
	}
	if (icmpadv(ptr, "RCPT")) {
		if (**ptr != ' ') return 0;
		++*ptr;
		if (!icmpadv(ptr, "TO")) return 0;
		if (**ptr != ':') return 0;
		++*ptr;
		if (**ptr != '<') return 0;
		++*ptr;
		if (!pmailbox(ptr)) return 0;
		if (**ptr != '>') return 0;
		++*ptr;
		if (!pcrlf(ptr)) return 0;
		*cmd = RCPT;
		return 1;
	}
	if (icmpadv(ptr, "NOOP")) {
		if (!pcrlf(ptr)) return 0;
		*cmd = NOOP;
		return 1;
	}
	if (icmpadv(ptr, "QUIT")) {
		if (!pcrlf(ptr)) return 0;
		*cmd = QUIT;
		return 1;
	}
	return 0;
}

int main()
{
	openlog("bmaild", 0, LOG_MAIL);
	syslog(LOG_MAIL | LOG_INFO, "bmaild is starting up.");
	int sock = socket(AF_INET6, SOCK_STREAM, 0);
	if (sock < 0) die("Can't open port %d.", PORT);
	
	struct sockaddr_in6 addr = { 0 };
	addr.sin6_family = AF_INET6;
	addr.sin6_port = htons(PORT);
	addr.sin6_addr = in6addr_any;

	bind(sock, (struct sockaddr *) &addr, sizeof(addr));
	listen(sock, 1);

	for (;;) {
		int client = accept(sock, NULL, NULL);
		dprintf(client, "220 %s Ready\r\n", DOMAIN);
		for (;;) {
			char *line = getiline(client);
			if (line == NULL) break;
			char *cur = line;
			int quit = 0, cmd = 0;
			if (!pcommand(&cur, &cmd)) {
				dprintf(client, "500 Syntax Error\r\n");
			} else {
				switch (cmd) {
				case HELO:
					dprintf(client, "250 %s\r\n", DOMAIN);
					break;
				case EHLO:
					dprintf(client, "250 %s\r\n", DOMAIN);
					break;
				case MAIL:
					dprintf(client, "250 OK\r\n");
					break;
				case RCPT:
					dprintf(client, "250 OK\r\n");
					break;
				case NOOP:
					dprintf(client, "250 OK\r\n");
					break;
				case QUIT:
					dprintf(client, "221 %s Bye\r\n", DOMAIN);
					quit = 1;
					break;
				}
			}
			free(line);
			if (quit) break;
		}
		close(client);
	}

	close(sock);
	closelog();
	return 0;
}

/* 
 * HELO <SP> <domain> <CRLF>
 * EHLO <SP> <domain> <CRLF>
 * MAIL <SP> FROM:<reverse-path> <CRLF>
 * RCPT <SP> TO:<forward-path> <CRLF>
 * DATA <CRLF>
 * RSET <CRLF>
 * NOOP <CRLF>
 * QUIT <CRLF>
 * VRFY <SP> <string> <CRLF>
 */
