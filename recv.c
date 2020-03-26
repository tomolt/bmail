/* See LICENSE file for copyright and license details. */

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#include <fcntl.h>

#include <tls.h>

#include "conn.h"
#include "mbox.h"
#include "smtp.h"
#include "util.h"

#define RCPT_MAX 100

extern char my_domain[256];

struct tstat
{
	time_t start_time;
	int total_viols;
	int total_trans;
	int total_rcpts;
	char cl_domain[DOMAIN_LEN+1];
};

struct envel
{
	char mail_name[UNIQNAME_LEN+1];
	char sender_local[LOCAL_LEN+1];
	char sender_domain[DOMAIN_LEN+1];
	char rcpt_list[RCPT_MAX][LOCAL_LEN+1];
	int rcpt_count;
};

static struct tstat *tstat;
static struct envel *envel;

static void dohelo(int ext)
{
	char domain[DOMAIN_LEN+1];
	if (phelo(domain)) {
		strcpy(tstat->cl_domain, domain);
		if (ext && cntls != NULL) {
			cwritent("250-");
			cwritent(my_domain);
			cwritent(" Hi\r\n");
			cwritent("250 STARTTLS\r\n");
		} else {
			cwritent("250 ");
			cwritent(my_domain);
			cwritent(" Hi\r\n");
		}
	} else {
		cwritent("501 Syntax Error\r\n");
		++tstat->total_viols;
	}
}

static void domail(void)
{
	if (pmail(envel->sender_local, envel->sender_domain)) {
		++tstat->total_trans;
		uniqname(envel->mail_name);
		cwritent("250 OK\r\n");
	} else {
		cwritent("501 Syntax Error\r\n");
		++tstat->total_viols;
	}
}

static void dorcpt(void)
{
	char local[LOCAL_LEN+1];
	char domain[DOMAIN_LEN+1];
	if (!prcpt(local, domain)) {
		cwritent("501 Syntax Error\r\n");
		++tstat->total_viols;
		return;
	}
	if (strcmp(domain, my_domain) != 0) {
		cwritent("550 User not local\r\n"); /* TODO should this be 551? */
		++tstat->total_viols;
		return;
	}
	if (!vrfylocal(local)) {
		cwritent("550 User non-existant\r\n");
		++tstat->total_viols;
		return;
	}
	for (int i = 0; i < envel->rcpt_count; ++i) {
		if (strcmp(local, envel->rcpt_list[i]) == 0) {
			cwritent("550 Repeated user\r\n");
			++tstat->total_viols;
			return;
		}
	}
	if (envel->rcpt_count >= RCPT_MAX) {
		cwritent("452 Too many users\r\n");
		return;
	}
	++tstat->total_rcpts;
	strcpy(envel->rcpt_list[envel->rcpt_count++], local);
	cwritent("250 OK\r\n");
}

static void acdata(int files[])
{
	char inb[512], outb[512];
	int inc = 0, ini = 0, outc = 0, match = 0;
	for (;;) {
		if (ini >= inc) {
			inc = cread(inb, sizeof(inb));
			ini = 0;
		}
		if (outc + 4 > (int) sizeof(outb)) {
			for (int i = 0; i < envel->rcpt_count; ++i) {
				write(files[i], outb, outc); /* TODO error checking & resume after partial write */
			}
			outc = 0;
		}
		if (inb[ini] == "\r\n.\r\n"[match]) {
			if (++match == 5) {
				for (int i = 0; i < envel->rcpt_count; ++i) {
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
		cwritent("501 Syntax Error\r\n");
		++tstat->total_viols;
	}
	cwritent("354 Listening\r\n");
	int files[RCPT_MAX];
	for (int i = 0; i < envel->rcpt_count; ++i) {
		char name[MAILPATH_LEN+1];
		mailpath(name, envel->rcpt_list[i], "tmp", envel->mail_name);
		files[i] = open(name, O_CREAT | O_EXCL | O_WRONLY, 0440); /* TODO error checking */
	}
	acdata(files);
	for (int i = 0; i < envel->rcpt_count; ++i) {
		close(files[i]);
		char tmpname[MAILPATH_LEN+1];
		mailpath(tmpname, envel->rcpt_list[i], "tmp", envel->mail_name);
		char newname[MAILPATH_LEN+1];
		mailpath(newname, envel->rcpt_list[i], "new", envel->mail_name);
		rename(tmpname, newname); /* TODO error checking */
	}
	memset(envel, 0, sizeof(*envel));
	cwritent("250 OK\r\n");
}

void recvmail(void)
{
	struct tstat tstat_buf;
	struct envel envel_buf;
	char line[COMMAND_LEN];

	tstat = &tstat_buf;
	memset(tstat, 0, sizeof(*tstat));
	tstat->start_time = time(NULL);
	strcpy(tstat->cl_domain, "<DOMAIN UNKNOWN>");

	envel = &envel_buf;
	memset(envel, 0, sizeof(*envel));

	cwritent("220 ");
	cwritent(my_domain);
	cwritent(" Ready\r\n");
	for (;;) {
		while (!creadln(line, sizeof(line))) {
			cwritent("500 Line too Long\r\n");
			++tstat->total_viols;
		}
		cphead = line;
		if (pword("HELO")) {
			dohelo(0);
		} else if (pword("EHLO")) {
			dohelo(1);
		} else if (pword("STARTTLS")) {
			if (pcrlf()) {
				cwritent("220 TLS now\r\n");
				cread = cread_tls;
				cwrite = cwrite_tls;
				/* TODO reset here? */
			} else {
				cwritent("501 Syntax Error\r\n");
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
				cwritent("250 OK\r\n");
			} else {
				cwritent("501 Syntax Error\r\n");
				++tstat->total_viols;
			}
		} else if (pword("RSET")) {
			if (pcrlf()) {
				memset(envel, 0, sizeof(*envel));
				cwritent("250 OK\r\n");
			} else {
				cwritent("501 Syntax Error\r\n");
				++tstat->total_viols;
			}
		} else if (pword("QUIT")) {
			if (pcrlf()) {
				cwritent("221 ");
				cwritent(my_domain);
				cwritent(" Bye\r\n");
				int duration = (int) difftime(time(NULL), tstat->start_time);
				fprintf(stderr, "%us\t%uV\t%uT\t%uR\t%s\n",
					duration, tstat->total_viols, tstat->total_trans, tstat->total_rcpts, tstat->cl_domain);
				if (cntls != NULL) {
					tls_close(cntls);
					tls_free(cntls);
				}
				exit(0);
			} else {
				cwritent("501 Syntax Error\r\n");
				++tstat->total_viols;
			}
		} else {
			cwritent("500 Unknown Command\r\n");
			++tstat->total_viols;
		}
	}
}

