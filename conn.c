/* See LICENSE file for copyright and license details. */

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <errno.h>

#include <tls.h>

#include "conf.h"
#include "conn.h"
#include "util.h"

static struct tls *tls_master = NULL;
static struct tls *tls_ctx = NULL;

void servercn(struct conf conf)
{
	if (conf.tls_enable) {
		struct tls_config *cfg;
		if ((cfg = tls_config_new()) == NULL)
			die("tls_config_new: %s", tls_config_error(cfg));
		if (tls_config_set_ca_file(cfg, conf.ca_file) < 0)
			die("tls_config_set_ca_file: %s", tls_config_error(cfg));
		if (tls_config_set_cert_file(cfg, conf.cert_file) < 0)
			die("tls_config_set_cert_file: %s", tls_config_error(cfg));
		if (tls_config_set_key_file(cfg, conf.key_file) < 0)
			die("tls_config_set_key_file: %s", tls_config_error(cfg));
		if ((tls_master = tls_server()) == NULL)
			die("tls_server: %s", tls_error(tls_master));
		if (tls_configure(tls_master, cfg) < 0)
			die("tls_configure: %s", tls_error(tls_master));
		tls_config_free(cfg);
	}
}

void closecn(void)
{
	if (tls_ctx != NULL) {
		tls_close(tls_ctx);
		tls_free(tls_ctx);
	}
	if (tls_master != NULL) {
		tls_close(tls_master);
		tls_free(tls_master);
	}
}

int cnstarttls(void)
{
	if (tls_master == NULL) return -1;
	if (tls_ctx != NULL) return -1;
	return tls_accept_fds(tls_master, &tls_ctx, 0, 1);
}

int cncantls(void)
{
	return tls_master != NULL;
}

static int cngetc(void)
{
	if (tls_ctx != NULL) {
		char c;
		if (tls_read(tls_ctx, &c, 1) != 1)
			return EOF;
		return c;
	} else {
		return getchar();
	}
}

int readline(char line[], int max, int *len)
{
	static int cr = 0;
	int i = 0;
	if (cr) line[i++] = '\r';
	while (i < max) {
		errno = 0;
		int c = cngetc();
		if (c == EOF) {
			if (errno) ioerr("getchar");
			else exit(1);
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

void cnsend(char *buf, int len)
{
	if (tls_ctx != NULL) {
		/* TODO error checking */
		tls_write(tls_ctx, buf, len);
	} else {
		/* TODO error checking */
		write(1, buf, len);
	}
}

