/* See LICENSE file for copyright and license details. */

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <fcntl.h>
#include <errno.h>
#include <syslog.h>
#include <unistd.h>

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
	sender_local[0] = 0;
	sender_domain[0] = 0;
	mail_name[0] = 0;
	rcpt_count = 0;
}

static void cleanup(int sig)
{
	(void) sig;
	reset();
	exit(1);
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

/* Block until a line of at most max characters was read.
 * Longer lines are discarded and an error is sent to the client
 * until a short enough line could be read. */
static void readcommand(char line[], int max, int *len)
{
	while (!readline(line, max, len)) {
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
		reply1("250", env_domain);
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
	if (strcmp(domain, env_domain) != 0) {
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
	int files[RCPT_MAX];
	for (int i = 0; i < rcpt_count; ++i) {
		char name[MAILPATH_LEN+1];
		mailpath(name, rcpt_list[i], "tmp", mail_name);
		files[i] = open(name, O_CREAT | O_EXCL | O_WRONLY, 0440); /* TODO error checking */
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
			write(files[i], data, len); /* TODO error checking */
		}
		begs = ends;
	}
	for (int i = 0; i < rcpt_count; ++i) {
		close(files[i]);
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
	openlog("bmaild", 0, LOG_MAIL);
	loadenv();
	handlesignals(cleanup);
	reply2("220", env_domain, "Ready");
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
				reply2("221", env_domain, "Bye");
				exit(0);
			} else {
				reply1("501", "Syntax Error");
			}
		} else {
			reply1("500", "Unknown Command");
		}
	}
}

