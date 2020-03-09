/* See LICENSE file for copyright and license details. */

#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include <stdint.h>
#include <syslog.h>
#include <signal.h>
#include <errno.h>

#ifdef __linux__
# include <sys/random.h>
#endif

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

void handlesignals(void (*handler)(int))
{
	struct sigaction sa = { .sa_handler = handler };
	sigemptyset(&sa.sa_mask);
	sigaction(SIGTERM, &sa, NULL);
	/* Not sure if this is a good idea or not. */
	/* sigaction(SIGHUP,  &sa, NULL); */
	sigaction(SIGINT,  &sa, NULL);
	sigaction(SIGQUIT, &sa, NULL);
}

unsigned long atolx(char *a)
{
	unsigned long lx = 0;
	for (char *p = a; p < a + 8; ++p) {
		char c = *p;
		unsigned long d;
		if (c >= '0' && c <= '9') d = c - '0';
		else if (c >= 'A' && c <= 'F') d = c - ('A' - 0xA);
		else if (c >= 'a' && c <= 'f') d = c - ('a' - 0xA);
		else break;
		lx = lx * 0x10 + d;
	}
	return lx;
}

char *lxtoa(unsigned long lx)
{
	static char a[9];
	a[8] = 0;
	for (char *p = a + 7; p >= a; --p) {
		unsigned long d = lx % 0x10;
		lx /= 0x10;
		char c;
		if (d < 0xA) c = d + '0';
		else c = d + ('A' - 0xA);
		*p = c;
	}
	return a;
}

unsigned long pcrandom32(void)
{
#ifdef __linux__
	uint32_t buf;
	getrandom(&buf, 4, 0);
	return buf;
#else
	return arc4random();
#endif
}

