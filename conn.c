/* See LICENSE file for copyright and license details. */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include <tls.h>

#include "conf.h"
#include "conn.h"
#include "util.h"

int cnsock;
struct tls *cntls = NULL;
int (*cread)(char *buf, int max);
int (*cwrite)(char *buf, int max);

static void tlserr(const char *func)
{
	fprintf(stderr, "%s: %s\n", func, tls_error(cntls));
	exit(1);
}

struct tls_config *conftls(const char *conf[])
{
	struct tls_config *cfg;
	if (!yesno(conf[CF_TLS_ENABLE])) return NULL;
	if ((cfg = tls_config_new()) == NULL)
		die("tls_config_new: %s", tls_config_error(cfg));
	if (tls_config_set_ca_file(cfg, conf[CF_CA_FILE]) < 0)
		die("tls_config_set_ca_file: %s", tls_config_error(cfg));
	if (tls_config_set_cert_file(cfg, conf[CF_CERT_FILE]) < 0)
		die("tls_config_set_cert_file: %s", tls_config_error(cfg));
	if (tls_config_set_key_file(cfg, conf[CF_KEY_FILE]) < 0)
		die("tls_config_set_key_file: %s", tls_config_error(cfg));
	return cfg;
}

int cread_plain(char *buf, int max)
{
	ssize_t s = read(cnsock, buf, max);
	if (s < 0) ioerr("read");
	if (s == 0) exit(1);
	return (int) s;
}

int cwrite_plain(char *buf, int max)
{
	ssize_t s = write(cnsock, buf, max);
	if (s < 0) ioerr("write");
	if (s == 0) exit(1);
	return (int) s;
}

int cread_tls(char *buf, int max)
{
	ssize_t s = tls_read(cntls, buf, max);
	if (s < 0) tlserr("tls_read");
	if (s == 0) exit(1);
	return (int) s;
}

int cwrite_tls(char *buf, int max)
{
	ssize_t s = tls_write(cntls, buf, max);
	if (s < 0) tlserr("tls_write");
	if (s == 0) exit(1);
	return (int) s;
}

int creadln(char *buf, int max)
{
	char c;
	int cr = 0;
	for (int i = 0; i < max; ++i) {
		cread(&c, 1);
		buf[i] = c;
		if (cr && c == '\n') return 1;
		cr = (c == '\r');
	}
	for (;;) {
		cread(&c, 1);
		if (cr && c == '\n') return 0;
		cr = (c == '\r');
	}
}

void cwritent(char *buf)
{
	int len = strlen(buf);
	while (len > 0) {
		int adv = cwrite(buf, len);
		buf += adv, len -= adv;
	}
}

