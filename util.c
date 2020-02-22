#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include <syslog.h>
#include <errno.h>

#include "util.h"

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

