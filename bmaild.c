#include <syslog.h>
#include <stdarg.h>
#include <errno.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <assert.h>

#define CONF_PATH "bmaild.conf"
#define DOMAIN "bmaild.domain"
#define PORT 5000
#define TIMEOUT 300

enum { CHATTING, LISTENING, QUITTING };

struct str
{
	char *data;
	size_t len;
	size_t cap;
};

static int client;

void die(const char *fmt, ...)
{
	int err = errno;
	const int prio = LOG_MAIL | LOG_EMERG;
	va_list va;
	va_start(va, fmt);
	vsyslog(prio, fmt, va);
	va_end(va);
	if (fmt[0] && fmt[strlen(fmt)-1] == ':') {
		syslog(prio, "  %s", strerror(err));
	}
	exit(1);
}

int mkstr(struct str *str, size_t init)
{
	str->len = 0;
	str->cap = init;
	str->data = malloc(str->cap);
	if (str->data == NULL) return -1;
	return 0;
}

int strput(struct str *str, char c)
{
	int idx = str->len++;
	if (str->len > str->cap) {
		str->cap *= 2;
		void *mem = realloc(str->data, str->cap);
		if (mem == NULL) return -1;
		str->data = mem;
	}
	str->data[idx] = c;
	return 0;
}

static void timeout(int signal)
{
	(void) signal;
	close(client);
}

#if 0
int isconfkc(char c)
{
	if (c >= '0' && c <= '9') return 1;
	if (c >= 'A' && c <= 'Z') return 1;
	if (c == '_') return 1;
	if (c >= 'a' && c <= 'z') return 1;
	return 0;
}

void loadconf(void)
{
	errno = 0;
	FILE *file = fopen(CONF_PATH, "r");
	if (file == NULL) die("Can't open %s:", CONF_PATH);
	char *line = NULL;
	size_t len = 0;
	for (;;) {
		/* read a single line */
		int s = getline(&line, &len);
		if (s < 0) die("Can't read %s:", CONF_PATH);
		char *cur = line, *key, *val;
		/* skip comments */
		if (*cur == '#') continue;
		/* read key and '=' */
		key = cur;
		if (!isconfkc(*cur)) die("conf: Missing key.");
		do ++cur;
		while (isconfkc(*cur));
		if (*cur != '=') die("conf: Missing '='.");
		*cur = 0, ++cur;
		/* skip opening '"' */
		if (*cur != '"') die("conf: Missing opening '\"'.");
		++cur;
		/* read value */
		/* FIXME proper backslash escapes! */
		val = cur;
		int esc = 0;
		for (;;) {
			if (!*cur) die("conf: Missing closing '\"'.");
			if (!esc && *cur == '"') break;
			esc = (*cur == '\\');
		}
		*cur = 0, ++cur;
		/* skip trailing whitespace */
		while (*cur == ' ') ++cur;
		/* skip closing newline, if any */
		if (*cur == '\n') ++cur;
		/* ensure there is nothing after the key-value pair */
		if (*cur) die("conf: Garbage after key-value pair.");

		if (strcmp(key, "domain") == 0) {
			conf.domain = val;
		} else if (strcmp(key, "port") == 0) {
			conf.port = val;
		} else {
			free(val);
			die("conf: No such key '%s'.", key);
		}
	}
	free(line);
	fclose(file);
}
#endif

int getiline(int fd, struct str *str)
{
	if (mkstr(str, 128) < 0) return -1;
	char cr = 0;
	for (;;) {
		char ch;
		ssize_t s = read(fd, &ch, 1);
		if (s <= 0) {
			free(str->data);
			return -1;
		}
		if (strput(str, ch) < 0) return -1;
		if (cr && ch == '\n') return 0;
		cr = (ch == '\r');
	}
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

int plocal(char **ptr, struct str *str)
{
	char *c = *ptr;
	mkstr(str, 16);
	/* TODO quoted local */
	if (!islocalc(*c)) return 0;
	do if (strput(str, *c++) < 0) return 0;
	while (islocalc(*c));
	*ptr = c;
	return 1;
}

int pdomain(char **ptr, struct str *str)
{
	char *c = *ptr;
	mkstr(str, 16);
	if (*c == '[') {
		do if (strput(str, *c++) < 0) return 0;
		while (isaddrc(*c));
		if (*c != ']') return 0;
		if (strput(str, *c++) < 0) return 0;
	} else {
		if (!isdomainc(*c)) return 0;
		do if (strput(str, *c++) < 0) return 0;
		while (isdomainc(*c));
	}
	*ptr = c;
	return 1;
}

int pmailbox(char **ptr, struct str *local, struct str *domain)
{
	if (!plocal(ptr, local)) return 0;
	if (**ptr != '@') return 0;
	++*ptr;
	if (!pdomain(ptr, domain)) return 0;
	return 1;
}

int pcommand(char **ptr)
{
	if (icmpadv(ptr, "HELO")) {
		if (**ptr != ' ') return 0;
		++*ptr;
		if (!pdomain(ptr)) return 0;
		if (!pcrlf(ptr)) return 0;
		dprintf(client, "250 %s\r\n", DOMAIN);
		return 1;
	}
	if (icmpadv(ptr, "EHLO")) {
		if (**ptr != ' ') return 0;
		++*ptr;
		if (!pdomain(ptr)) return 0;
		if (!pcrlf(ptr)) return 0;
		dprintf(client, "250 %s\r\n", DOMAIN);
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
		dprintf(client, "250 OK\r\n");
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
		dprintf(client, "250 OK\r\n");
		return 1;
	}
	if (icmpadv(ptr, "DATA")) {
		if (!pcrlf(ptr)) return 0;
		dprintf(client, "354 Listening\r\n");
		state = LISTENING;
		return 1;
	}
	if (icmpadv(ptr, "NOOP")) {
		if (!pcrlf(ptr)) return 0;
		dprintf(client, "250 OK\r\n");
		return 1;
	}
	if (icmpadv(ptr, "QUIT")) {
		if (!pcrlf(ptr)) return 0;
		dprintf(client, "221 %s Bye\r\n", DOMAIN);
		state = QUITTING;
		return 1;
	}
	return 0;
}

int main()
{
	openlog("bmaild", 0, LOG_MAIL);
	syslog(LOG_MAIL | LOG_INFO, "bmaild is starting up.");
	if (signal(SIGALRM, timeout) == SIG_ERR) die("Can't set up timeout signal:");

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
		client = accept(sock, NULL, NULL);
		/* TODO log accept errno problems? */
		dprintf(client, "220 %s Ready\r\n", DOMAIN);
		int state = CHATTING;
		while (state != QUITTING) {
			alarm(TIMEOUT);
			struct str line;
			if (getiline(client, &line) < 0) {
				free(line.data);
				break;
			}
			alarm(0);
			/* TODO we need a write timeout as well! */
			switch (state) {
			case CHATTING: {
				char *cur = line.data;
				if (!pcommand(&cur)) {
					dprintf(client, "500 Syntax Error\r\n");
				}
			} break;
			case LISTENING:
				if (line.data[0] != '.') {
					printf("%.*s", (int) line.len, line.data);
				} else {
					if (line.data[1] == '\r' && line.data[2] == '\n') {
						dprintf(client, "250 OK\r\n");
						state = CHATTING;
					} else {
						printf("%.*s", (int) line.len - 1, line.data + 1);
					}
				}
				break;
			}
			free(line.data);
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
