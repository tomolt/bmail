#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <limits.h>
#include <errno.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <pwd.h>
#include <grp.h>

#include "util.h"
#include "conf.h"

static int iskeyc(int c)
{
	if (c >= '0' && c <= '9') return 1;
	if (c >= 'A' && c <= 'Z') return 1;
	if (c >= 'a' && c <= 'z') return 1;
	if (c == '_') return 1;
	return 0;
}

static void synerr(void)
{
	die("Syntax error in bmailrc.");
}

static int yesno(char *value)
{
	if (strcmp(value, "YES") == 0) return 1;
	if (strcmp(value, "NO") == 0) return 0;
	die("Config value must be either YES or NO.");
	return 0;
}

static void assign(struct conf *conf, char *key, char *value)
{
	if (strcmp(key, "domain") == 0) conf->domain = value;
	else if (strcmp(key, "spool") == 0) conf->spool = value;
	else if (strcmp(key, "user") == 0) conf->user = value;
	else if (strcmp(key, "group") == 0) conf->group = value;
	else if (strcmp(key, "tls_enable") == 0) conf->tls_enable = yesno(value);
	else if (strcmp(key, "ca_file") == 0) conf->ca_file = value;
	else if (strcmp(key, "cert_file") == 0) conf->cert_file = value;
	else if (strcmp(key, "key_file") == 0) conf->key_file = value;
	else die("bmailrc assigns value to non-option.");
}

static char *readfile(const char *filename)
{
	int fd;
	char *data;
	struct stat info;
	if ((fd = open(filename, O_RDONLY)) < 0)
		die("Can't open config file:");
	if (fstat(fd, &info) < 0)
		die("Can't stat config file:");
	if ((data = malloc(info.st_size+1)) == NULL)
		die("malloc:");
	if (read(fd, data, info.st_size) != info.st_size)
		die("Can't read config file:");
	data[info.st_size] = 0;
	close(fd);
	return data;
}

const char *findconf(void)
{
	const char *bmailrc = getenv("BMAILRC");
	if (bmailrc == NULL) bmailrc = "/etc/bmail.conf";
	return bmailrc;
}

struct conf loadconf(const char *filename)
{
	struct conf conf = { 0 };
	conf.spool = "/var/spool/mail";
	conf.user = "nobody";
	conf.group = "nogroup";
	conf._data = readfile(filename);
	int s = 1;
	char *p, c, *k = NULL, *v = NULL;
	for (p = conf._data, c = *p; s; c = *++p) {
		switch (s) {
		case 1:
			if (c == 0) s = 0;
			else if (iskeyc(c)) s = 2, k = p;
			else if (c == '#') s = 7;
			else if (c != ' ' && c != '\n') synerr();
			break;
		case 2:
			if (c == ' ') s = 3, *p = 0;
			else if (c == '=') s = 4, *p = 0;
			else if (!iskeyc(c)) synerr();
			break;
		case 3:
			if (c == '=') s = 4;
			else if (c != ' ') synerr();
			break;
		case 4:
			if (c == '"') s = 5, v = p + 1;
			else if (c != ' ') synerr();
			break;
		case 5:
			if (c == '"') s = 6, *p = 0, assign(&conf, k, v);
			else if (c == 0 || c == '\n') synerr();
			break;
		case 6:
			if (c == 0) s = 0;
			else if (c == '\n') s = 1;
			else if (c == '#') s = 7;
			else if (c != ' ') synerr();
			break;
		case 7:
			if (c == 0) s = 0;
			else if (c == '\n') s = 1;
			break;
		}
	}
	return conf;
}

void freeconf(struct conf conf)
{
	free(conf._data);
}

void dropprivs(struct conf conf)
{
	struct group *grp = NULL;
	struct passwd *pwd = NULL;
	/* Check supplied user and group. */
	errno = 0;
	if (conf.user && !(pwd = getpwnam(conf.user))) {
		die("getpwnam '%s': %s", conf.user, errno ? strerror(errno) :
		    "Entry not found");
	}
	errno = 0;
	if (conf.group && !(grp = getgrnam(conf.group))) {
		die("getgrnam '%s': %s", conf.group, errno ? strerror(errno) :
		    "Entry not found");
	}
	/* Chdir into spool an chroot there. */
	if (chdir(conf.spool) < 0) die("chdir:");
	if (chroot(".") < 0) die("chroot:");
	/* Drop user, group and supplementary groups in correct order. */
	if (grp && setgroups(1, &(grp->gr_gid)) < 0) {
		die("setgroups:");
	}
	if (grp && setgid(grp->gr_gid) < 0) {
		die("setgid:");
	}
	if (pwd && setuid(pwd->pw_uid) < 0) {
		die("setuid:");
	}
	/* Make sure priviledge dropping worked. */
	if (getuid() == 0) {
		die("Won't run as root user.");
	}
	if (getgid() == 0) {
		die("Won't run as root group.");
	}
}

