/* See LICENSE file for copyright and license details. */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>

#include <tls.h>

#include "conf.h"
#include "conn.h"
#include "util.h"

static int sock;
static struct tls *tls_master = NULL;
static struct tls *tls_ctx = NULL;

static void tlserr(const char *func)
{
	fprintf(stderr, "%s: %s\n", func, tls_error(tls_ctx));
	exit(1);
}

void servercn(const char *conf[], int psock)
{
	sock = psock;
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

void clientcn(const char *conf[], int psock)
{
	sock = psock;
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
	close(sock);
}

int cnstarttls(void)
{
	if (tls_master == NULL) return -1;
	if (tls_ctx != NULL) return -1;
	return tls_accept_socket(tls_master, &tls_ctx, sock);
}

int cncantls(void)
{
	return tls_master != NULL;
}

int cnrecv(char *buf, int max)
{
	ssize_t s;
	if (tls_ctx != NULL) {
		s = tls_read(tls_ctx, buf, max);
		if (s < 0) tlserr("tls_read");
	} else {
		s = read(sock, buf, max);
		if (s < 0) ioerr("read");
	}
	if (s == 0) {
		exit(1);
	}
	return (int) s;
}

int cnsend(char *buf, int max)
{
	ssize_t s;
	if (tls_ctx != NULL) {
		s = tls_write(tls_ctx, buf, max);
		if (s < 0) tlserr("tls_write");
	} else {
		s = write(sock, buf, max);
		if (s < 0) ioerr("write");
	}
	if (s == 0) {
		exit(1);
	}
	return (int) s;
}

int cnrecvln(char *buf, int max)
{
	int cr = 0;
	while (max > 0) {
		int adv = cnrecv(buf, max);
		for (int i = 0; i < adv; ++i) {
			if (cr && buf[i] == '\n') return 1;
			cr = (buf[i] == '\r');
		}
		buf += adv, max -= adv;
	}
	for (;;) {
		char spill[128];
		int adv = cnrecv(spill, sizeof(spill));
		for (int i = 0; i < adv; ++i) {
			if (cr && spill[i] == '\n') return 0;
			cr = (spill[i] == '\r');
		}
	}
}

void cnsendnt(char *buf)
{
	int len = strlen(buf);
	while (len > 0) {
		int adv = cnsend(buf, len);
		buf += adv, len -= adv;
	}
}

