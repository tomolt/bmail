#include <syslog.h>

void server(void);

int main()
{
	openlog("bmaild", 0, LOG_MAIL);
	syslog(LOG_MAIL | LOG_INFO, "bmaild is starting up.");
	server();
	closelog();
	return 0;
}

