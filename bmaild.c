#include <syslog.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

#include "arg.h"
#include "util.h"
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
	conf.spool = "/var/spool/mail";
	ARGBEGIN {
	case 'D':
		conf.domain = EARGF(usage());
		break;
	case 'S':
		conf.spool = EARGF(usage());
		break;
	default:
		usage();
	} ARGEND
	if (argc) usage();
	if (conf.domain == NULL) usage();

	openlog("bmaild", 0, LOG_MAIL);
	syslog(LOG_MAIL | LOG_INFO, "bmaild is starting up.");
	if (chdir(conf.spool) < 0) die("Can't chdir into spool:");
	server();
	closelog();
	return 0;
}

