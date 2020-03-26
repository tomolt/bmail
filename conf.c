/* See LICENSE file for copyright and license details. */

#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <limits.h>
#include <errno.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <pwd.h>
#include <grp.h>

#include "conf.h"
#include "util.h"

static const char *field_names[] = {
	"domain",
	"spool",
	"user",
	"group",
	"tls_enable",
	"ca_file",
	"cert_file",
	"key_file",
};

static const char *field_defaults[] = {
	"",
	"/var/spool/mail",
	"nobody",
	"nogroup",
	"NO",
	"",
	"",
	"",
};

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

const char *findconf(void)
{
	const char *bmailrc = getenv("BMAILRC");
	if (bmailrc == NULL) bmailrc = "/etc/bmail.conf";
	return bmailrc;
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
	ssize_t got = 0;
	while (got < info.st_size) {
		ssize_t s = read(fd, data + got, info.st_size - got);
		if (s < 0) ioerr("read");
		if (s == 0) die("Config file changed size.");
		got += s;
	}
	data[info.st_size] = 0;
	close(fd);
	return data;
}

static void assign(const char *conf[], char *key, char *value)
{
	for (int i = 0; i < CF__DATA_; ++i) {
		if (strcmp(key, field_names[i]) == 0) {
			conf[i] = value;
			return;
		}
	}
	die("bmailrc assigns value to non-option.");
}

void loadconf(const char *conf[], const char *filename)
{
	int i, s = 1;
	char *p, c, *k = NULL, *v = NULL;
	for (i = 0; i < CF__DATA_; ++i) {
		conf[i] = field_defaults[i];
	}
	p = readfile(filename);
	conf[CF__DATA_] = p;
	do {
		c = *p;
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
			if (c == '"') s = 6, *p = 0, assign(conf, k, v);
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
		++p;
	} while (s);
}

void freeconf(const char *conf[])
{
	free((char *) conf[CF__DATA_]);
}

int yesno(const char *value)
{
	if (strcmp(value, "YES") == 0) return 1;
	if (strcmp(value, "NO") == 0) return 0;
	die("Config value must be either YES or NO.");
	return 0;
}

void dropprivs(const char *conf[])
{
	struct group *grp = NULL;
	struct passwd *pwd = NULL;
	/* Check supplied user and group. */
	errno = 0;
	if (!(pwd = getpwnam(conf[CF_USER]))) {
		die("getpwnam '%s': %s", conf[CF_USER], errno ? strerror(errno) :
		    "Entry not found");
	}
	errno = 0;
	if (!(grp = getgrnam(conf[CF_GROUP]))) {
		die("getgrnam '%s': %s", conf[CF_GROUP], errno ? strerror(errno) :
		    "Entry not found");
	}
	/* Chdir into spool an chroot there. */
	if (chdir(conf[CF_SPOOL]) < 0) die("chdir:");
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

