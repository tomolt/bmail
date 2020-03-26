/* See LICENSE file for copyright and license details. */

#include <string.h>
#include <unistd.h>
#include <netdb.h>
#include <fcntl.h>
#include <poll.h>
/* #include <signal.h> */

#include <tls.h>

#include "util.h"
#include "conf.h"
#include "conn.h"

#define MAX_SOCKS 10

char my_domain[256];

static const char *ports[] = { "25", "587", NULL };

static struct tls *tlssrv = NULL;
static int socks[MAX_SOCKS];
static struct pollfd pfds[MAX_SOCKS];
static int nsocks;

extern void recvmail(void);

static void teardown(int sig)
{
	(void) sig;
	/* kill(0, sig); */
	_exit(1);
}

int main()
{
	const char *conf[NUM_CF_FIELDS];
	struct tls_config *tlscfg;

	const int yes = 1;
	for (int p = 0; ports[p] != NULL; ++p) {
		struct addrinfo hints, *list, *ai;
		/* List all plausible addresses to listen on */
		memset(&hints, 0, sizeof(hints));
		hints.ai_family = AF_UNSPEC;
		hints.ai_socktype = SOCK_STREAM;
		hints.ai_flags = AI_PASSIVE;
		int eai = getaddrinfo(NULL, ports[p], &hints, &list);
		if (eai != 0) die("getaddrinfo: %s", gai_strerror(eai));
		/* Open sockets for all addresses */
		for (ai = list; ai != NULL; ai = ai->ai_next) {
			if (nsocks >= MAX_SOCKS) die("Trying to open too many sockets.");
			int sock = socket(ai->ai_family, ai->ai_socktype, ai->ai_protocol);
			if (sock < 0) die("socket:");
			/* Get rid of "Address already in use" problems */
			if (setsockopt(sock, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof(yes)) < 0)
				die("Can't enable address reuse:");
			/* Disable ipv4 tunneling through ipv6, as it's not entirely portable. */
			if (ai->ai_family == AF_INET6) {
				if (setsockopt(sock, IPPROTO_IPV6, IPV6_V6ONLY, &yes, sizeof(yes)) < 0)
					die("Can't disable ipv4-mapped ipv6:");
			}
			/* Set close-on-exec flag so child processes don't have access to the master sockets. */
			int flags = fcntl(sock, F_GETFD, 0);
			if (flags < 0) die("fcntl:");
			flags = fcntl(sock, F_SETFD, flags | FD_CLOEXEC);
			if (flags < 0) die("fcntl:");
			/* Configure socket to be non-blocking. */
			flags = fcntl(sock, F_GETFL, 0);
			if (flags < 0) die("fcntl:");
			flags = fcntl(sock, F_SETFL, flags | O_NONBLOCK);
			if (flags < 0) die("fcntl:");
			/* Bind and listen */
			if (bind(sock, ai->ai_addr, ai->ai_addrlen) < 0) die("bind:");
			if (listen(sock, 8) < 0) die("listen:");
			/* Initialize poll file descriptor */
			struct pollfd pfd = { 0 };
			pfd.fd = sock;
			pfd.events = POLLIN;
			/* Add to socket array */
			pfds[nsocks] = pfd;
			socks[nsocks] = sock;
			++nsocks;
		}
		freeaddrinfo(list);
	}
	/* Loading the config file. */
	loadconf(conf, findconf());
	if ((tlscfg = conftls(conf)) != NULL) {
		if ((tlssrv = tls_server()) == NULL)
			die("tls_server: %s", tls_error(tlssrv));
		if (tls_configure(tlssrv, tlscfg) < 0)
			die("tls_configure: %s", tls_error(tlssrv));
		tls_config_free(tlscfg);
	}
	if (strlen(conf[CF_DOMAIN]) > sizeof(my_domain))
		die("Domain name is too long.");
	strcpy(my_domain, conf[CF_DOMAIN]);
	dropprivs(conf);
	freeconf(conf);
	/* General process configuration. */
	setpgid(0, 0);
	reapchildren();
	handlesignals(teardown);
	for (;;) {
		if (poll(pfds, nsocks, -1) < 0) {
			ioerr("poll");
			continue;
		}
		for (int i = 0; i < nsocks; ++i) {
			if (!(pfds[i].revents & POLLIN)) continue;
			int s = accept(socks[i], NULL, NULL);
			if (s < 0) {
				ioerr("accept");
				continue;
			}
			pid_t pid = fork();
			if (pid < 0) {
				ioerr("fork");
			} else if (pid == 0) {
				cnsock = s;
				if (tlssrv != NULL) {
					tls_accept_socket(tlssrv, &cntls, cnsock); /* TODO error checkng */
				}
				cread = cread_plain;
				cwrite = cwrite_plain;
				recvmail();
			}
			close(s);
		}
	}
}

