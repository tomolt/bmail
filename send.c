/* See LICENSE file for copyright and license details. */

#include <stdlib.h>
#include <string.h>

#include "util.h"
#include "conf.h"
#include "conn.h"

#define REPLY_LEN 512

static void cleanup()
{
	closecn();
}

void sendmail(int sock)
{
	const char *conf[NUM_CF_FIELDS];
	loadconf(conf, findconf());
	clientcn(conf, sock);
	dropprivs(conf);
	freeconf(conf);

	handlesignals(cleanup);
	atexit(cleanup);

	char line[REPLY_LEN];
	cnrecvln(line, sizeof(line));
	/* truncate if neccessary, we don't care. */
	memcpy(line+REPLY_LEN-2, "\r\n", 2);
}

