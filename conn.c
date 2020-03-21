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

void servercn(const char *conf[])
{
	if (yesno(conf[CF_TLS_ENABLE])) {
		struct tls_config *cfg;
		if ((cfg = tls_config_new()) == NULL)
			die("tls_config_new: %s", tls_config_error(cfg));
		if (tls_config_set_ca_file(cfg, conf[CF_CA_FILE]) < 0)
			die("tls_config_set_ca_file: %s", tls_config_error(cfg));
		if (tls_config_set_cert_file(cfg, conf[CF_CERT_FILE]) < 0)
			die("tls_config_set_cert_file: %s", tls_config_error(cfg));
		if (tls_config_set_key_file(cfg, conf[CF_KEY_FILE]) < 0)
			die("tls_config_set_key_file: %s", tls_config_error(cfg));
		if ((tls_master = tls_server()) == NULL)
			die("tls_server: %s", tls_error(tls_master));
		if (tls_configure(tls_master, cfg) < 0)
			die("tls_configure: %s", tls_error(tls_master));
		tls_config_free(cfg);
	}
}

void clientcn(const char *conf[])
{
	if (yesno(conf[CF_TLS_ENABLE])) {
		struct tls_config *cfg;
		if ((cfg = tls_config_new()) == NULL)
			die("tls_config_new: %s", tls_config_error(cfg));
		if (tls_config_set_ca_file(cfg, conf[CF_CA_FILE]) < 0)
			die("tls_config_set_ca_file: %s", tls_config_error(cfg));
		if ((tls_master = tls_client()) == NULL)
			die("tls_client: %s", tls_error(tls_master));
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

void cnrecv(char *buf, int len)
{
	if (tls_ctx != NULL) {
		/* TODO error checking */
		tls_read(tls_ctx, buf, len);
	} else {
		/* TODO error checking */
		fread(buf, len, 1, stdin);
	}
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

int cnrecvln(char *buf, int max)
{
	char c, cr = 0;
	for (int i = 0; i < max; ++i) {
		cnrecv(&c, 1);
		buf[i] = c;
		if (cr && c == '\n') return 1;
		cr = (c == '\r');
	}
	for (;;) {
		cnrecv(&c, 1);
		if (cr && c == '\n') return 0;
		cr = (c == '\r');
	}
}

