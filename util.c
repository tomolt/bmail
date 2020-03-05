/* See LICENSE file for copyright and license details. */

#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include <syslog.h>
#include <signal.h>
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
	sigaction(SIGHUP,  &sa, NULL);
	sigaction(SIGINT,  &sa, NULL);
	sigaction(SIGQUIT, &sa, NULL);
}

