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
static int state;

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

int pchar(char **ptr, char ch)
{
	int s = (**ptr == ch);
	*ptr += s;
	return s;
}

int pcrlf(char **ptr)
{
	return pchar(ptr, '\r') && pchar(ptr, '\n');
}

int pword(char **ptr, char *exp)
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

int plocal(char **ptr, struct str *str)
{
	char *c = *ptr;
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
	return plocal(ptr, local) && pchar(ptr, '@') && pdomain(ptr, domain);
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
		dprintf(client, "250 %s\r\n", DOMAIN);
	} else {
		dprintf(client, "501 Syntax Error\r\n");
	}
	free(domain.data);
}

void domail(char **ptr)
{
	struct str local, domain;
	mkstr(&local, 16);
	mkstr(&domain, 16);
	if (pmail(ptr, &local, &domain)) {
		dprintf(client, "250 OK\r\n");
	} else {
		dprintf(client, "501 Syntax Error\r\n");
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
		dprintf(client, "250 OK\r\n");
	} else {
		dprintf(client, "501 Syntax Error\r\n");
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
			state = LISTENING;
			dprintf(client, "354 Listening\r\n");
		} else {
			dprintf(client, "501 Syntax Error\r\n");
		}
	} else if (pword(ptr, "NOOP")) {
		if (pcrlf(ptr)) {
			dprintf(client, "250 OK\r\n");
		} else {
			dprintf(client, "501 Syntax Error\r\n");
		}
	} else if (pword(ptr, "QUIT")) {
		if (pcrlf(ptr)) {
			state = QUITTING;
			dprintf(client, "221 %s Bye\r\n", DOMAIN);
		} else {
			dprintf(client, "501 Syntax Error\r\n");
		}
	} else {
		dprintf(client, "500 Unknown Command\r\n");
	}
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
		state = CHATTING;
		while (state != QUITTING) {
			alarm(TIMEOUT);
			struct str line;
			if (getiline(client, &line) < 0) {
				free(line.data);
				break;
			}
			alarm(0);
			/* TODO we need a write timeout as well! */
			char *cur;
			switch (state) {
			case CHATTING:
				cur = line.data;
				docommand(&cur);
				break;
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

