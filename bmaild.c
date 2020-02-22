#include <syslog.h>
#include <stdio.h>
#include <stdlib.h>

#include "arg.h"
#include "conf.h"

void server(void);

char *argv0;
struct conf conf;

static void usage(void)
{
	printf("usage: %s -D domain [-S spool]\n", argv0);
	exit(0);
}

int main(int argc, char *argv[])
{
	ARGBEGIN {
	case 'D':
		conf.domain = EARGF(usage());
		break;
	case 'S':
		conf.spool = EARGF(usage());
	default:
		usage();
	} ARGEND
	if (argc) usage();
	if (conf.domain == NULL) usage();

	openlog("bmaild", 0, LOG_MAIL);
	syslog(LOG_MAIL | LOG_INFO, "bmaild is starting up.");
	server();
	closelog();
	return 0;
}

