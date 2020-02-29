/* See LICENSE file for copyright and license details. */

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

void clrstr(struct str *str)
{
	free(str->data);
	str->data = NULL;
	str->len = 0;
	str->cap = 0;
}

int strext(struct str *str, size_t len, char *ext)
{
	int idx = str->len;
	str->len += len;
	if (str->len > str->cap) {
		str->cap *= 2;
		void *mem = realloc(str->data, str->cap);
		if (mem == NULL) return -1;
		str->data = mem;
	}
	memcpy(str->data + idx, ext, len);
	return 0;
}

int strput(struct str *str, char c)
{
	return strext(str, 1, &c);
}

void strdeq(struct str *str, size_t len)
{
	str->len -= len;
	memmove(str->data, str->data-len, str->len);
	size_t cap = str->cap;
	while (cap > 2 * str->len && cap > 16) cap /= 2;
	if (str->cap == cap) return;
	void *mem = realloc(str->data, cap);
	if (mem == NULL) return;
	str->cap = cap;
	str->data = mem;
}

