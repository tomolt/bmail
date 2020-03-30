/* See LICENSE file for copyright and license details. */

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/stat.h>

#include <tls.h>

#include "conn.h"
#include "mbox.h"
#include "smtp.h"
#include "util.h"

extern char my_domain[256];

struct tstat
{
	time_t start_time;
	int total_viols;
	int total_trans;
	int total_rcpts;
	char cl_domain[DOMAIN_LEN+1];
};

struct addr
{
	char *local;
	char *domain;
};

static struct tstat *tstat;
static struct addr sender;
static struct addr *rcpts = NULL;
static int nrcpts;
static int crcpts;

static void reset(void)
{
	memset(sender.local, 0, LOCAL_LEN + 1);
	memset(sender.domain, 0, DOMAIN_LEN + 1);
	for (int i = 0; i < nrcpts; ++i)
		free(rcpts[i].local);
	free(rcpts);
	rcpts = NULL;
	nrcpts = 0;
	crcpts = 0;
}

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
	char local[LOCAL_LEN+1];
	char domain[DOMAIN_LEN+1];
	if (pmail(local, domain)) {
		strcpy(sender.local, local);
		strcpy(sender.domain, domain);
		++tstat->total_trans;
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

	if (nrcpts + 1 > crcpts) {
		int cap = crcpts == 0 ? 16 : 2 * crcpts;
		void *mem = reallocarray(rcpts, cap, sizeof(rcpts[0]));
		if (mem == NULL) {
			cwritent("450 Insufficient RAM\r\n"); /* FIXME is this the right status code? */
			return;
		}
		rcpts = mem;
		crcpts = cap;
	}

	size_t local_len = strlen(local);
	size_t domain_len = strlen(domain);
	struct addr addr;
	addr.local = malloc(local_len + domain_len + 2);
	if (addr.local == NULL) {
		cwritent("450 Insufficient RAM\r\n"); /* FIXME is this the right status code? */
		return;
	}
	addr.domain = addr.local + local_len + 1;
	strcpy(addr.local, local);
	strcpy(addr.domain, domain);
	rcpts[nrcpts++] = addr;

	++tstat->total_rcpts;
	cwritent("250 OK\r\n");
}

static void acdata(int fd)
{
	char inb[512], outb[512];
	int inc = 0, ini = 0, outc = 0, match = 0;
	for (;;) {
		if (ini >= inc) {
			inc = cread(inb, sizeof(inb));
			ini = 0;
		}
		if (outc + 4 > (int) sizeof(outb)) {
			write(fd, outb, outc); /* TODO error checking & resume after partial write */
			outc = 0;
		}
		if (inb[ini] == "\r\n.\r\n"[match]) {
			if (++match == 5) {
				write(fd, outb, outc); /* TODO error checking & resume after partial write */
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


static int addrcmp(const void *p1, const void *p2)
{
	const struct addr a1 = *(const struct addr *) p1;
	const struct addr a2 = *(const struct addr *) p2;
	int c = strcmp(a1.domain, a2.domain);
	if (c) return c;
	c = strcmp(a1.local, a2.local);
	return c;
}

/* FIXME This entire function doesn't do error handling! */
static void dodata(void)
{
	if (!pcrlf()) {
		cwritent("501 Syntax Error\r\n");
		++tstat->total_viols;
	}
	cwritent("354 Listening\r\n");
	chdir(".queue");

	char tmp_msg[32], tmp_env[32];
	sprintf(tmp_msg, "tmp/%d.msg", getpid());
	sprintf(tmp_env, "tmp/%d.env", getpid());

	int datafd = open(tmp_msg, O_CREAT | O_WRONLY);
	acdata(datafd);
	close(datafd);

	qsort(rcpts, nrcpts, sizeof(rcpts[0]), addrcmp);

	int i = 0;
	while (i < nrcpts) {
		FILE *envf = fopen(tmp_env, "w");

		struct stat info;
		stat(tmp_env, &info);
		char prm_msg[32], prm_env[32];
		sprintf(prm_msg, "msg/%lu", info.st_ino);
		sprintf(prm_env, "env/%lu", info.st_ino);

		char *domain = rcpts[i].domain;
		fprintf(envf, "bq1\n%s\n%s\n%s\n--\n",
			domain, sender.local, sender.domain);

		fprintf(envf, "%s\n", rcpts[i++].local);
		while (i < nrcpts) {
			if (strcmp(rcpts[i].domain, domain) != 0) break;
			if (strcmp(rcpts[i].local, rcpts[i-1].local) != 0) {
				fprintf(envf, "%s\n", rcpts[i].local);
			}
			++i;
		}

		fclose(envf);
		link(tmp_msg, prm_msg);
		rename(tmp_env, prm_env);
	}
	unlink(tmp_msg);

	chdir("..");
	reset();
	cwritent("250 OK\r\n");
}

void recvmail(void)
{
	struct tstat tstat_buf;
	char sender_local_buf[LOCAL_LEN + 1];
	char sender_domain_buf[DOMAIN_LEN + 1];
	char line[COMMAND_LEN];

	tstat = &tstat_buf;
	memset(tstat, 0, sizeof(*tstat));
	tstat->start_time = time(NULL);
	strcpy(tstat->cl_domain, "<DOMAIN UNKNOWN>");

	sender.local = sender_local_buf;
	sender.domain = sender_domain_buf;
	memset(sender.local, 0, LOCAL_LEN + 1);
	memset(sender.domain, 0, DOMAIN_LEN + 1);

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
				reset();
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

