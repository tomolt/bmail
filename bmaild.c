#include <syslog.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <arpa/inet.h>

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

int main()
{
	openlog("bmaild", 0, LOG_MAIL);
	syslog(LOG_MAIL | LOG_INFO, "bmaild is starting up.");
	int sock = socket(AF_INET6, SOCK_STREAM, 0);
	if (sock < 0) die("Can't open port %d.", PORT);
	
	struct sockaddr_in6 addr;
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
			printf("%s", line);
			write(client, "ACK\n", 4);
			free(line);
		}
		close(client);
	}

	close(sock);
	closelog();
	return 0;
}

