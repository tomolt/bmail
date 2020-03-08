/* See LICENSE file for copyright and license details. */

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <fcntl.h>
#include <errno.h>
#include <syslog.h>

#include "util.h"
#include "smtp.h"
#include "mbox.h"

#define RCPT_MAX 100

static char *my_domain;
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

/* receiver-specific handling of I/O errors. */
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

static void reply1(char *code, char *arg1)
{
	/* TODO error checking */
	printf("%s %s\r\n", code, arg1);
	fflush(stdout);
}

static void reply2(char *code, char *arg1, char *arg2)
{
	/* TODO error checking */
	printf("%s %s %s\r\n", code, arg1, arg2);
	fflush(stdout);
}

/* Read a CRLF-terminated line from stdin into line, and its length into len.
 * The line is not NULL-terminated and may contain any raw byte values,
 * including NULL. If a line is longer than max readline() will only return
 * up to the first max characters. The rest of the line can be read with
 * subsequent calls to readline(). Incomplete lines like these are guaranteed
 * to never contain only part of a CRLF sequence.
 * Returns 0 if line hasn't been read completely and 1 otherwise. */
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

/* Block until a line of at most max characters was read.
 * Longer lines are discarded and an error is sent to the client
 * until a short enough line could be read. */
static void readcommand(char line[], int max, int *len)
{
	for (;;) {
		if (readline(line, max, len)) return;
		while (!readline(line, max, len)) {}
		reply1("500", "Line too long");
	}
}

static void dohelo(int ext)
{
	(void) ext;
	char domain[DOMAIN_LEN+1];
	if (phelo(domain)) {
		syslog(LOG_MAIL | LOG_INFO, "Incoming connection from <%s>.", domain);
		reply1("250", my_domain);
	} else {
		reply1("501", "Syntax Error");
	}
}

static void domail(void)
{
	if (pmail(sender_local, sender_domain)) {
		strcpy(mail_name, uniqname());
		reply1("250", "OK");
	} else {
		reply1("501", "Syntax Error");
	}
}

static void dorcpt(void)
{
	char local[LOCAL_LEN+1];
	char domain[DOMAIN_LEN+1];
	if (!prcpt(local, domain)) {
		reply1("501", "Syntax Error");
		return;
	}
	if (strcmp(domain, my_domain) != 0) {
		reply1("550", "User not local"); /* TODO should this be 551? */
		return;
	}
	if (!vrfylocal(local)) {
		reply1("550", "User non-existant");
		return;
	}
	if (rcpt_count >= RCPT_MAX) {
		reply1("452", "Too many users");
		return;
	}
	strcpy(rcpt_list[rcpt_count++], local);
	reply1("250", "OK");
}

static void dodata(void)
{
	if (!pcrlf()) {
		reply1("501", "Syntax Error");
	}
	reply1("354", "Listening");
	FILE *files[RCPT_MAX];
	for (int i = 0; i < rcpt_count; ++i) {
		char name[MAILPATH_LEN+1];
		mailpath(name, rcpt_list[i], "tmp", mail_name);
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
		char tmpname[MAILPATH_LEN+1];
		mailpath(tmpname, rcpt_list[i], "tmp", mail_name);
		char newname[MAILPATH_LEN+1];
		mailpath(newname, rcpt_list[i], "new", mail_name);
		rename(tmpname, newname); /* TODO error checking */
	}
	reset();
	reply1("250", "OK");
}

int main()
{
	my_domain = getenv("BMAIL_DOMAIN");
	if (my_domain == NULL) exit(-1); /* TODO maybe die() here? */
	char *csequence = getenv("BMAIL_SEQUENCE");
	if (csequence == NULL) exit(-1);
	sscanf(csequence, "%lx", &sequence); /* TODO error checking */
	handlesignals(cleanup);
	reset();
	reply2("220", my_domain, "Ready");
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
				reply1("250", "OK");
			} else {
				reply1("501", "Syntax Error");
			}
		} else if (pword("RSET")) {
			if (pcrlf()) {
				reset();
				reply1("250", "OK");
			} else {
				reply1("501", "Syntax Error");
			}
		} else if (pword("QUIT")) {
			if (pcrlf()) {
				reply2("221", my_domain, "Bye");
				disconnect();
			} else {
				reply1("501", "Syntax Error");
			}
		} else {
			reply1("500", "Unknown Command");
		}
	}
}

