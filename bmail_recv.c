/* See LICENSE file for copyright and license details. */

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <syslog.h>

#include "conf.h"
#include "conn.h"
#include "mbox.h"
#include "smtp.h"
#include "util.h"

#define RCPT_MAX 100

static char my_domain[DOMAIN_LEN+1];
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

/* Intentionally empty argument list to allow cleanup() to be used as a signal handler. */
static void cleanup()
{
	closeconn();
}

static void reply(char *line)
{
	connsend(line, strlen(line));
}

/* Block until a line of at most max characters was read.
 * Longer lines are discarded and an error is sent to the client
 * until a short enough line could be read. */
static void readcommand(char line[], int max, int *len)
{
	while (!readline(line, max, len)) {
		while (!readline(line, max, len)) {}
		reply("500 Line too Long\r\n");
	}
}

static void dohelo(int ext)
{
	(void) ext;
	char domain[DOMAIN_LEN+1];
	if (phelo(domain)) {
		syslog(LOG_MAIL | LOG_INFO, "Incoming connection from <%s>.", domain);
		if (ext && tlsallowed()) {
			reply("250-");
			reply(my_domain);
			reply(" Hi\r\n");
			reply("250 STARTTLS\r\n");
		} else {
			reply("250 ");
			reply(my_domain);
			reply(" Hi\r\n");
		}
	} else {
		reply("501 Syntax Error\r\n");
	}
}

static void domail(void)
{
	if (pmail(sender_local, sender_domain)) {
		strcpy(mail_name, uniqname());
		reply("250 OK\r\n");
	} else {
		reply("501 Syntax Error\r\n");
	}
}

static void dorcpt(void)
{
	char local[LOCAL_LEN+1];
	char domain[DOMAIN_LEN+1];
	if (!prcpt(local, domain)) {
		reply("501 Syntax Error\r\n");
		return;
	}
	if (strcmp(domain, my_domain) != 0) {
		reply("550 User not local\r\n"); /* TODO should this be 551? */
		return;
	}
	if (!vrfylocal(local)) {
		reply("550 User non-existant\r\n");
		return;
	}
	if (rcpt_count >= RCPT_MAX) {
		reply("452 Too many users\r\n");
		return;
	}
	strcpy(rcpt_list[rcpt_count++], local);
	reply("250 OK\r\n");
}

static void dodata(void)
{
	if (!pcrlf()) {
		reply("501 Syntax Error\r\n");
	}
	reply("354 Listening\r\n");
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
	reply("250 OK\r\n");
}

int main()
{
	struct conf conf;
	openlog("bmail_recv", 0, LOG_MAIL);
	handlesignals(cleanup);
	atexit(cleanup);
	conf = loadconf(findconf());
	strcpy(my_domain, conf.domain); /* There *shouldn't* be an overflow here. */
	openserver(conf);
	dropprivs(conf);
	freeconf(conf);
	reply("220 ");
	reply(my_domain);
	reply(" Ready\r\n");
	for (;;) {
		char line[COMMAND_LEN];
		int len;
		readcommand(line, sizeof(line), &len);
		cphead = line;
		if (pword("HELO")) {
			dohelo(0);
		} else if (pword("EHLO")) {
			dohelo(1);
		} else if (pword("STARTTLS")) {
			if (pcrlf()) {
				reply("220 TLS now\r\n");
				/* TODO Correctly report error to client! */
				if (starttls() < 0) exit(1);
			} else {
				reply("501 Syntax Error\r\n");
			}
		} else if (pword("MAIL")) {
			domail();
		} else if (pword("RCPT")) {
			dorcpt();
		} else if (pword("DATA")) {
			dodata();
		} else if (pword("NOOP")) {
			if (pcrlf()) {
				reply("250 OK\r\n");
			} else {
				reply("501 Syntax Error\r\n");
			}
		} else if (pword("RSET")) {
			if (pcrlf()) {
				reset();
				reply("250 OK\r\n");
			} else {
				reply("501 Syntax Error\r\n");
			}
		} else if (pword("QUIT")) {
			if (pcrlf()) {
				reply("221 ");
				reply(my_domain);
				reply(" Bye\r\n");
				exit(0);
			} else {
				reply("501 Syntax Error\r\n");
			}
		} else {
			reply("500 Unknown Command\r\n");
		}
	}
}

