/* See LICENSE file for copyright and license details. */

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#include <fcntl.h>

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

static time_t start_time;
static char cl_domain[DOMAIN_LEN+1];
static int total_viols;
static int total_trans;
static int total_rcpts;

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
	int duration = (int) difftime(time(NULL), start_time);
	fprintf(stderr, "%us\t%uV\t%uT\t%uR\t%s\n",
		duration, total_viols, total_trans, total_rcpts, cl_domain);
	closecn();
}

static void reply(char *line)
{
	cnsend(line, strlen(line));
}

static void dohelo(int ext)
{
	(void) ext;
	char domain[DOMAIN_LEN+1];
	if (phelo(domain)) {
		strcpy(cl_domain, domain);
		if (ext && cncantls()) {
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
		++total_viols;
	}
}

static void domail(void)
{
	if (pmail(sender_local, sender_domain)) {
		++total_trans;
		strcpy(mail_name, uniqname());
		reply("250 OK\r\n");
	} else {
		reply("501 Syntax Error\r\n");
		++total_viols;
	}
}

static void dorcpt(void)
{
	char local[LOCAL_LEN+1];
	char domain[DOMAIN_LEN+1];
	if (!prcpt(local, domain)) {
		reply("501 Syntax Error\r\n");
		++total_viols;
		return;
	}
	if (strcmp(domain, my_domain) != 0) {
		reply("550 User not local\r\n"); /* TODO should this be 551? */
		++total_viols;
		return;
	}
	if (!vrfylocal(local)) {
		reply("550 User non-existant\r\n");
		++total_viols;
		return;
	}
	for (int i = 0; i < rcpt_count; ++i) {
		if (strcmp(local, rcpt_list[i]) == 0) {
			reply("550 Repeated user\r\n");
			++total_viols;
			return;
		}
	}
	if (rcpt_count >= RCPT_MAX) {
		reply("452 Too many users\r\n");
		return;
	}
	++total_rcpts;
	strcpy(rcpt_list[rcpt_count++], local);
	reply("250 OK\r\n");
}

static void acdata(int files[])
{
	const char *pattern = "\r\n.\r\n";
	int match = 0;
	for (;;) {
		char page[512];
		int cnt = 0;
		while (cnt + 4 < (int) sizeof(page)) {
			char b[5];
			int bn = 5 - match;
			cnrecv(b, bn);
			for (int i = 0; i < bn; ++i) {
				if (b[i] == pattern[match]) {
					if (++match == 5) {
						memcpy(page+cnt, pattern, 2), cnt += 2;
						for (int i = 0; i < rcpt_count; ++i) {
							write(files[i], page, cnt); /* TODO error checking */
						}
						return;
					}
				} else {
					switch (match) {
					case 1: memcpy(page+cnt, pattern, 1), cnt += 1; break;
					case 2: memcpy(page+cnt, pattern, 2), cnt += 2; break;
					case 3: memcpy(page+cnt, pattern, 2), cnt += 2; break;
					case 4: memcpy(page+cnt, "\r\n\r", 3), cnt += 3; break;
					}
					page[cnt++] = b[i];
					match = 0;
				}
			}
		}
		for (int i = 0; i < rcpt_count; ++i) {
			write(files[i], page, cnt); /* TODO error checking */
		}
	}
}

static void dodata(void)
{
	if (!pcrlf()) {
		reply("501 Syntax Error\r\n");
		++total_viols;
	}
	reply("354 Listening\r\n");
	int files[RCPT_MAX];
	for (int i = 0; i < rcpt_count; ++i) {
		char name[MAILPATH_LEN+1];
		mailpath(name, rcpt_list[i], "tmp", mail_name);
		files[i] = open(name, O_CREAT | O_EXCL | O_WRONLY, 0440); /* TODO error checking */
	}
	acdata(files);
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
	const char *conf[NUM_CF_FIELDS];
	loadconf(conf, findconf());
	strcpy(my_domain, conf[CF_DOMAIN]); /* There *shouldn't* be an overflow here. */
	servercn(conf);
	dropprivs(conf);
	freeconf(conf);

	handlesignals(cleanup);
	atexit(cleanup);

	start_time = time(NULL);
	strcpy(cl_domain, "<DOMAIN UNKNOWN>");

	reply("220 ");
	reply(my_domain);
	reply(" Ready\r\n");
	for (;;) {
		char line[COMMAND_LEN];
		while (!cnrecvln(line, sizeof(line))) {
			reply("500 Line too Long\r\n");
			++total_viols;
		}
		cphead = line;
		if (pword("HELO")) {
			dohelo(0);
		} else if (pword("EHLO")) {
			dohelo(1);
		} else if (pword("STARTTLS")) {
			if (pcrlf()) {
				reply("220 TLS now\r\n");
				/* TODO Correctly report error to client! */
				if (cnstarttls() < 0) exit(1);
			} else {
				reply("501 Syntax Error\r\n");
				++total_viols;
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
				++total_viols;
			}
		} else if (pword("RSET")) {
			if (pcrlf()) {
				reset();
				reply("250 OK\r\n");
			} else {
				reply("501 Syntax Error\r\n");
				++total_viols;
			}
		} else if (pword("QUIT")) {
			if (pcrlf()) {
				reply("221 ");
				reply(my_domain);
				reply(" Bye\r\n");
				exit(0);
			} else {
				reply("501 Syntax Error\r\n");
				++total_viols;
			}
		} else {
			reply("500 Unknown Command\r\n");
			++total_viols;
		}
	}
}

