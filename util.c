/* See LICENSE file for copyright and license details. */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include <stdint.h>
#include <signal.h>
#include <errno.h>

#ifdef __linux__
# include <sys/random.h>
#endif

#include "util.h"

void die(const char *fmt, ...)
{
	int err = errno;
	va_list va;
	va_start(va, fmt);
	vfprintf(stderr, fmt, va);
	va_end(va);
	if (fmt[0] && fmt[strlen(fmt)-1] == ':') {
		fprintf(stderr, "  %s", strerror(err));
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
	case EPIPE:
	case ECONNRESET: case ETIMEDOUT:
		exit(1);
		break;
	case EMFILE: case ENFILE:
		fprintf(stderr, "! Running out of file descriptors.\n");
		break;
	case ENOBUFS: case ENOMEM:
		fprintf(stderr, "! Running out of kernel memory.\n");
		break;
	case ENETDOWN: case ENETUNREACH:
		fprintf(stderr, "! Network is unreachable.\n");
		break;
	default:
		fprintf(stderr, "! BUG: %s: %s\n", func, strerror(errno));
		break;
	}
}

void handlesignals(void (*handler)(int))
{
	struct sigaction sa = { .sa_handler = handler };
	sigemptyset(&sa.sa_mask);
	sigaction(SIGABRT, &sa, NULL);
	sigaction(SIGTERM, &sa, NULL);
	/* Not sure if this is a good idea or not. */
	sigaction(SIGHUP,  &sa, NULL);
	sigaction(SIGINT,  &sa, NULL);
	sigaction(SIGQUIT, &sa, NULL);
}

void reapchildren(void)
{
	struct sigaction ign = { .sa_handler = SIG_IGN };
	sigemptyset(&ign.sa_mask);
	ign.sa_flags = SA_RESTART;
	sigaction(SIGCHLD, &ign, NULL);
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

