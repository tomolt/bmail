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
		for (;;) {
			char *line = getiline(client);
			if (line == NULL) break;
			char *cur = line;
			if (pmailbox(&cur)) {
				printf("proper mailbox.\n");
			} else {
				printf("bad input.\n");
			}
			free(line);
		}
		close(client);
	}

	close(sock);
	closelog();
	return 0;
}

/* 
 * HELO <SP> <domain> <CRLF>
 * MAIL <SP> FROM:<reverse-path> <CRLF>
 * RCPT <SP> TO:<forward-path> <CRLF>
 * DATA <CRLF>
 * RSET <CRLF>
 * NOOP <CRLF>
 * QUIT <CRLF>
 */
