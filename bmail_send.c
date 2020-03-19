/* See LICENSE file for copyright and license details. */

#include <stdlib.h>

#include "util.h"
#include "conf.h"
#include "conn.h"

static void cleanup()
{
	closecn();
}

int main()
{
	handlesignals(cleanup);
	atexit(cleanup);
	struct conf conf = loadconf(findconf());
	clientcn(conf);
	dropprivs(conf);
	freeconf(conf);
}

