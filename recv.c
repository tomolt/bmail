/* See LICENSE file for copyright and license details. */

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <fcntl.h>
#include <errno.h>
#include <syslog.h>

#include "conf.h"
#include "util.h"
#include "smtp.h"

static char sender_local[LOCAL_LEN+1];
static char sender_domain[DOMAIN_LEN+1];
static int rcpt_dir;

static void reset(void)
{
	memset(sender_local, 0, sizeof(sender_local));
	memset(sender_domain, 0, sizeof(sender_domain));
	rcpt_dir = -1;
}

static void disconnect(void)
{
	reset();
	exit(0);
}

static void cleanup(int sig)
{
	(void) sig;
	disconnect();
}

/* Session-specific handling of I/O errors. */
static void sioerr(const char *func)
{
	switch (errno) {
	case EPIPE:
	case ECONNRESET: case ETIMEDOUT:
		disconnect();
		break;
	default:
		ioerr(func);
		break;
	}
}

static void ereply1(char *code, char *arg1)
{
	/* TODO error checking */
	printf("%s %s\r\n", code, arg1);
	fflush(stdout);
}

static void ereply2(char *code, char *arg1, char *arg2)
{
	/* TODO error checking */
	printf("%s %s %s\r\n", code, arg1, arg2);
	fflush(stdout);
}

int readline(char line[], int *len)
{
	static int cr = 0;
	int i = 0, max = *len;
	if (cr) line[i++] = '\r';
	while (i < max) {
		errno = 0;
		int c = getchar();
		if (c == EOF) {
			if (errno) sioerr("getchar");
			else disconnect();
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

static void readcommand(char line[], int *len)
{
	for (;;) {
		if (readline(line, len)) return;
		while (!readline(line, len)) {}
		ereply1("500", "Line too long");
	}
}

static int openmbox(const char *name)
{
	/* Make sure name isn't some weird file path. */
	if (name[0] == '.') return -1;
	for (const char *c = name; *c; ++c) {
		if (*c == '/') return -1;
		if (!islocalc(*c)) return -1;
	}
	/* Try to open directory of the same name. */
	int fd = open(name, O_DIRECTORY);
	if (fd < 0) {
		if (errno != ENOENT) ioerr("open");
		return -1;
	}
	return fd;
}

/* SMTP server-specific parsing functions. See smtp.h for conventions. */

static int phelo(char domain[])
{
	return pchar(' ') && pdomain(domain) && pcrlf();
}

static int pmail(char local[], char domain[])
{
	int s =  pchar(' ') && pword("FROM") && pchar(':');
	s = s && pchar('<') && pmailbox(local, domain) && pchar('>');
	return   pcrlf();
}

static int prcpt(char local[], char domain[])
{
	int s =  pchar(' ') && pword("TO") && pchar(':');
	s = s && pchar('<') && pmailbox(local, domain) && pchar('>');
	return   pcrlf();
}

static void dohelo(int ext)
{
	(void) ext;
	char domain[DOMAIN_LEN+1];
	if (phelo(domain)) {
		syslog(LOG_MAIL | LOG_INFO, "Incoming connection from <%s>.", domain);
		ereply1("250", conf.domain);
	} else {
		ereply1("501", "Syntax Error");
	}
}

static void domail(void)
{
	if (pmail(sender_local, sender_domain)) {
		ereply1("250", "OK");
	} else {
		ereply1("501", "Syntax Error");
	}
}

static void dorcpt(void)
{
	char local[LOCAL_LEN+1];
	char domain[DOMAIN_LEN+1];
	if (prcpt(local, domain)) {
		ereply1("250", "OK");
	} else {
		ereply1("501", "Syntax Error");
	}
}

static void dodata(void)
{
	if (!pcrlf()) {
		ereply1("501", "Syntax Error");
	}
	ereply1("354", "Listening");
	for (;;) {
		char line[128];
		int len = sizeof(line);
		if (readline(line, &len) && line[0] == '.') {
			if (len == 3) break;
			fprintf(stderr, "%.*s", len-1, line+1);
		} else {
			fprintf(stderr, "%.*s", len, line);
		}
	}
	ereply1("250", "OK");
}

void recvmail(void)
{
	handlesignals(cleanup);
	reset();
	ereply2("220", conf.domain, "Ready");
	for (;;) {
		char line[128];
		int len = sizeof(line);
		readcommand(line, &len);
		cphead = line;
		if (pword("HELO")) {
			dohelo(0);
		} else if (pword("EHLO")) {
			dohelo(1);
		} else if (pword("MAIL")) {
			domail();
		} else if (pword("RCPT")) {
			dorcpt();
		} else if (pword("DATA")) {
			dodata();
		} else if (pword("NOOP")) {
			if (pcrlf()) {
				ereply1("250", "OK");
			} else {
				ereply1("501", "Syntax Error");
			}
		} else if (pword("RSET")) {
			if (pcrlf()) {
				reset();
				ereply1("250", "OK");
			} else {
				ereply1("501", "Syntax Error");
			}
		} else if (pword("QUIT")) {
			if (pcrlf()) {
				ereply2("221", conf.domain, "Bye");
				disconnect();
			} else {
				ereply1("501", "Syntax Error");
			}
		} else {
			ereply1("500", "Unknown Command");
		}
	}
}

