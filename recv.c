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
#include "mbox.h"

#define RCPT_MAX 100

static char sender_local[LOCAL_LEN+1];
static char sender_domain[DOMAIN_LEN+1];
static char mail_name[UNIQNAME_LEN+1];
static char rcpt_list[RCPT_MAX][LOCAL_LEN+1];
static int rcpt_count;

static void reset(void)
{
	memset(sender_local, 0, sizeof(sender_local));
	memset(sender_domain, 0, sizeof(sender_domain));
	memset(mail_name, 0, sizeof(mail_name));
	rcpt_count = 0;
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

static int readline(char line[], int max, int *len)
{
	static int cr = 0;
	int i = 0;
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

static void readcommand(char line[], int max, int *len)
{
	for (;;) {
		if (readline(line, max, len)) return;
		while (!readline(line, max, len)) {}
		ereply1("500", "Line too long");
	}
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
		strcpy(mail_name, uniqname());
		ereply1("250", "OK");
	} else {
		ereply1("501", "Syntax Error");
	}
}

static void dorcpt(void)
{
	char local[LOCAL_LEN+1];
	char domain[DOMAIN_LEN+1];
	if (!prcpt(local, domain)) {
		ereply1("501", "Syntax Error");
		return;
	}
	if (strcmp(domain, conf.domain) != 0) {
		ereply1("550", "User not local"); /* TODO should this be 551? */
		return;
	}
	if (!vrfylocal(local)) {
		ereply1("550", "User non-existant");
		return;
	}
	if (rcpt_count >= RCPT_MAX) {
		ereply1("452", "Too many users");
		return;
	}
	strcpy(rcpt_list[rcpt_count++], local);
	ereply1("250", "OK");
}

static void dodata(void)
{
	if (!pcrlf()) {
		ereply1("501", "Syntax Error");
	}
	ereply1("354", "Listening");
	FILE *files[RCPT_MAX];
	for (int i = 0; i < rcpt_count; ++i) {
		char name[LOCAL_LEN + UNIQNAME_LEN + 6];
		int rlen = strlen(rcpt_list[i]);
		memcpy(name, rcpt_list[i], rlen);
		memcpy(name + rlen, "/tmp/", 5);
		strcpy(name + rlen + 5, mail_name);
		files[i] = fopen(name, "wb"); /* TODO error checking */
	}
	int begs = 1;
	for (;;) {
		char line[PAGE_LEN], *data = line;
		int len, ends = readline(line, sizeof(line), &len);
		if (begs && line[0] == '.') {
			if (ends && len == 3) break;
			++data, --len;
		}
		for (int i = 0; i < rcpt_count; ++i) {
			fwrite(data, 1, len, files[i]); /* TODO error checking */
		}
		begs = ends;
	}
	for (int i = 0; i < rcpt_count; ++i) {
		fclose(files[i]);
	}
	reset();
	ereply1("250", "OK");
}

void recvmail(void)
{
	handlesignals(cleanup);
	reset();
	ereply2("220", conf.domain, "Ready");
	for (;;) {
		char line[COMMAND_LEN];
		int len;
		readcommand(line, sizeof(line), &len);
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

