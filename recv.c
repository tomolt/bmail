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

struct tstat
{
	time_t start_time;
	int total_viols;
	int total_trans;
	int total_rcpts;
	char cl_domain[DOMAIN_LEN+1];
};

struct mail
{
	char sender_local[LOCAL_LEN+1];
	char sender_domain[DOMAIN_LEN+1];
	char mail_name[UNIQNAME_LEN+1];
	char rcpt_list[RCPT_MAX][LOCAL_LEN+1];
	unsigned char rcpt_count;
};

struct tstat *tstat;
struct mail *mail;

/* Intentionally empty argument list to allow cleanup() to be used as a signal handler. */
static void cleanup()
{
	int duration = (int) difftime(time(NULL), tstat->start_time);
	fprintf(stderr, "%us\t%uV\t%uT\t%uR\t%s\n",
		duration, tstat->total_viols, tstat->total_trans, tstat->total_rcpts, tstat->cl_domain);
	closecn();
}

static void dohelo(int ext)
{
	(void) ext;
	char domain[DOMAIN_LEN+1];
	if (phelo(domain)) {
		strcpy(tstat->cl_domain, domain);
		if (ext && cncantls()) {
			cnsendnt("250-");
			cnsendnt(my_domain);
			cnsendnt(" Hi\r\n");
			cnsendnt("250 STARTTLS\r\n");
		} else {
			cnsendnt("250 ");
			cnsendnt(my_domain);
			cnsendnt(" Hi\r\n");
		}
	} else {
		cnsendnt("501 Syntax Error\r\n");
		++tstat->total_viols;
	}
}

static void domail(void)
{
	if (pmail(mail->sender_local, mail->sender_domain)) {
		++tstat->total_trans;
		uniqname(mail->mail_name);
		cnsendnt("250 OK\r\n");
	} else {
		cnsendnt("501 Syntax Error\r\n");
		++tstat->total_viols;
	}
}

static void dorcpt(void)
{
	char local[LOCAL_LEN+1];
	char domain[DOMAIN_LEN+1];
	if (!prcpt(local, domain)) {
		cnsendnt("501 Syntax Error\r\n");
		++tstat->total_viols;
		return;
	}
	if (strcmp(domain, my_domain) != 0) {
		cnsendnt("550 User not local\r\n"); /* TODO should this be 551? */
		++tstat->total_viols;
		return;
	}
	if (!vrfylocal(local)) {
		cnsendnt("550 User non-existant\r\n");
		++tstat->total_viols;
		return;
	}
	for (int i = 0; i < mail->rcpt_count; ++i) {
		if (strcmp(local, mail->rcpt_list[i]) == 0) {
			cnsendnt("550 Repeated user\r\n");
			++tstat->total_viols;
			return;
		}
	}
	if (mail->rcpt_count >= RCPT_MAX) {
		cnsendnt("452 Too many users\r\n");
		return;
	}
	++tstat->total_rcpts;
	strcpy(mail->rcpt_list[mail->rcpt_count++], local);
	cnsendnt("250 OK\r\n");
}

static void acdata(int files[])
{
	char inb[512], outb[512];
	int inc = 0, ini = 0, outc = 0, match = 0;
	for (;;) {
		if (ini >= inc) {
			inc = cnrecv(inb, sizeof(inb));
			ini = 0;
		}
		if (outc + 4 > (int) sizeof(outb)) {
			for (int i = 0; i < mail->rcpt_count; ++i) {
				write(files[i], outb, outc); /* TODO error checking & resume after partial write */
			}
			outc = 0;
		}
		if (inb[ini] == "\r\n.\r\n"[match]) {
			if (++match == 5) {
				for (int i = 0; i < mail->rcpt_count; ++i) {
					write(files[i], outb, outc); /* TODO error checking & resume after partial write */
				}
				return;
			}
		} else {
			switch (match) {
			case 1: memcpy(outb+outc, "\r", 1), outc += 1; break;
			case 2: memcpy(outb+outc, "\r\n", 2), outc += 2; break;
			case 3: memcpy(outb+outc, "\r\n", 2), outc += 2; break;
			case 4: memcpy(outb+outc, "\r\n\r", 3), outc += 3; break;
			}
			outb[outc++] = inb[ini];
			match = 0;
		}
		++ini;
	}
}

static void dodata(void)
{
	if (!pcrlf()) {
		cnsendnt("501 Syntax Error\r\n");
		++tstat->total_viols;
	}
	cnsendnt("354 Listening\r\n");
	int files[RCPT_MAX];
	for (int i = 0; i < mail->rcpt_count; ++i) {
		char name[MAILPATH_LEN+1];
		mailpath(name, mail->rcpt_list[i], "tmp", mail->mail_name);
		files[i] = open(name, O_CREAT | O_EXCL | O_WRONLY, 0440); /* TODO error checking */
	}
	acdata(files);
	for (int i = 0; i < mail->rcpt_count; ++i) {
		close(files[i]);
		char tmpname[MAILPATH_LEN+1];
		mailpath(tmpname, mail->rcpt_list[i], "tmp", mail->mail_name);
		char newname[MAILPATH_LEN+1];
		mailpath(newname, mail->rcpt_list[i], "new", mail->mail_name);
		rename(tmpname, newname); /* TODO error checking */
	}
	memset(mail, 0, sizeof(*mail));
	cnsendnt("250 OK\r\n");
}

void recvmail(int socket)
{
	const char *conf[NUM_CF_FIELDS];
	struct tstat tstat_buf;
	struct mail mail_buf;
	char line[COMMAND_LEN];

	loadconf(conf, findconf());
	strcpy(my_domain, conf[CF_DOMAIN]); /* There *shouldn't* be an overflow here. */
	servercn(conf, socket);
	dropprivs(conf);
	freeconf(conf);

	tstat = &tstat_buf;
	memset(tstat, 0, sizeof(*tstat));
	tstat->start_time = time(NULL);
	strcpy(tstat->cl_domain, "<DOMAIN UNKNOWN>");

	mail = &mail_buf;
	memset(mail, 0, sizeof(*mail));

	handlesignals(cleanup);
	atexit(cleanup);

	cnsendnt("220 ");
	cnsendnt(my_domain);
	cnsendnt(" Ready\r\n");
	for (;;) {
		while (!cnrecvln(line, sizeof(line))) {
			cnsendnt("500 Line too Long\r\n");
			++tstat->total_viols;
		}
		cphead = line;
		if (pword("HELO")) {
			dohelo(0);
		} else if (pword("EHLO")) {
			dohelo(1);
		} else if (pword("STARTTLS")) {
			if (pcrlf()) {
				cnsendnt("220 TLS now\r\n");
				/* TODO Correctly report error to client! */
				if (cnstarttls() < 0) exit(1);
			} else {
				cnsendnt("501 Syntax Error\r\n");
				++tstat->total_viols;
			}
		} else if (pword("MAIL")) {
			domail();
		} else if (pword("RCPT")) {
			dorcpt();
		} else if (pword("DATA")) {
			dodata();
		} else if (pword("NOOP")) {
			if (pcrlf()) {
				cnsendnt("250 OK\r\n");
			} else {
				cnsendnt("501 Syntax Error\r\n");
				++tstat->total_viols;
			}
		} else if (pword("RSET")) {
			if (pcrlf()) {
				memset(mail, 0, sizeof(*mail));
				cnsendnt("250 OK\r\n");
			} else {
				cnsendnt("501 Syntax Error\r\n");
				++tstat->total_viols;
			}
		} else if (pword("QUIT")) {
			if (pcrlf()) {
				cnsendnt("221 ");
				cnsendnt(my_domain);
				cnsendnt(" Bye\r\n");
				exit(0);
			} else {
				cnsendnt("501 Syntax Error\r\n");
				++tstat->total_viols;
			}
		} else {
			cnsendnt("500 Unknown Command\r\n");
			++tstat->total_viols;
		}
	}
}

