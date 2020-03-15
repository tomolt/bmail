/* See LICENSE file for copyright and license details. */

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <stdarg.h>
#include <stdint.h>
#include <syslog.h>
#include <signal.h>
#include <errno.h>
#include <unistd.h>
#include <limits.h>
#include <pwd.h>
#include <grp.h>

#ifdef __linux__
# include <sys/random.h>
#endif

#include "util.h"

char *env_domain;

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
	case EPIPE:
	case ECONNRESET: case ETIMEDOUT:
		abort();
		break;
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

void loadenv(void)
{
	struct group *grp = NULL;
	struct passwd *pwd = NULL;
	char *spool, *user, *group;
	/* Load config from environment variables, falling back to defaults if neccessary. */
	if ((env_domain = getenv("BMAIL_DOMAIN")) == NULL) {
		static char buf[HOST_NAME_MAX];
		gethostname(buf, HOST_NAME_MAX); /* No error checking neccessary here. */
		env_domain = buf;
	}
	if ((spool = getenv("BMAIL_SPOOL")) == NULL) {
		spool = "/var/spool/mail";
	}
	if ((user = getenv("BMAIL_USER")) == NULL) {
		user = "nobody";
	}
	if ((group = getenv("BMAIL_GROUP")) == NULL) {
		group = "nogroup";
	}
	/* Check supplied user and group. */
	errno = 0;
	if (user && !(pwd = getpwnam(user))) {
		die("getpwnam '%s': %s", user, errno ? strerror(errno) :
		    "Entry not found");
	}
	errno = 0;
	if (group && !(grp = getgrnam(group))) {
		die("getgrnam '%s': %s", group, errno ? strerror(errno) :
		    "Entry not found");
	}
	/* Chdir into spool an chroot there. */
	if (chdir(spool) < 0) die("chdir:");
	if (chroot(".") < 0) die("chroot:");
	/* Drop user, group and supplementary groups in correct order. */
	if (grp && setgroups(1, &(grp->gr_gid)) < 0) {
		die("setgroups:");
	}
	if (grp && setgid(grp->gr_gid) < 0) {
		die("setgid:");
	}
	if (pwd && setuid(pwd->pw_uid) < 0) {
		die("setuid:");
	}
	/* Make sure priviledge dropping worked. */
	if (getuid() == 0) {
		die("Won't run as root user.");
	}
	if (getgid() == 0) {
		die("Won't run as root group.");
	}
}

int readline(char line[], int max, int *len)
{
	static int cr = 0;
	int i = 0;
	if (cr) line[i++] = '\r';
	while (i < max) {
		errno = 0;
		int c = getchar();
		if (c == EOF) {
			if (errno) ioerr("getchar");
			else abort();
		}
		line[i++] = c;
		if (cr && c == '\n') {
			cr = 0;
			*len = i;
			return 1;
		}
		cr = (c == '\r');
	}
	if (cr) --i;
	*len = i;
	return 0;
}

