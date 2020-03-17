#include <unistd.h>
#include <stdio.h>
#include <stdlib.h>
#include <errno.h>

#include <tls.h>

#include "util.h"
#include "conf.h"
#include "conn.h"

static struct tls *sec_master = NULL;
static struct tls *sec = NULL;

void openserver(struct conf conf)
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
		if ((sec_master = tls_server()) == NULL)
			die("tls_server: %s", tls_error(sec_master));
		if (tls_configure(sec_master, cfg) < 0)
			die("tls_configure: %s", tls_error(sec_master));
		tls_config_free(cfg);
	}
}

void closeconn(void)
{
	if (sec != NULL) {
		tls_close(sec);
		tls_free(sec);
	}
	if (sec_master != NULL) {
		tls_close(sec_master);
		tls_free(sec_master);
	}
}

int starttls(void)
{
	if (sec_master == NULL) return -1;
	if (sec != NULL) return -1;
	return tls_accept_fds(sec_master, &sec, 0, 1);
}

int tlsallowed(void)
{
	return sec_master != NULL;
}

int conngetc(void)
{
	if (sec != NULL) {
		char c;
		if (tls_read(sec, &c, 1) != 1)
			return EOF;
		return c;
	} else {
		return getchar();
	}
}

void connsend(char *buf, int len)
{
	if (sec != NULL) {
		/* TODO error checking */
		tls_write(sec, buf, len);
	} else {
		/* TODO error checking */
		write(1, buf, len);
	}
}

int readline(char line[], int max, int *len)
{
	static int cr = 0;
	int i = 0;
	if (cr) line[i++] = '\r';
	while (i < max) {
		errno = 0;
		int c = conngetc();
		if (c == EOF) {
			if (errno) ioerr("getchar");
			else abort();
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

