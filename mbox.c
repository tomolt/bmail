/* See LICENSE file for copyright and license details. */

#include <stdio.h>
#include <string.h>
#include <time.h>
#include <errno.h>
#include <unistd.h>
#include <sys/stat.h>
#include <inttypes.h>

#include "util.h"
#include "smtp.h"
#include "mbox.h"

static uint32_t local;

char *uniqname(void)
{
	static char buf[UNIQNAME_LEN+1];
	uint32_t nums[4];
	/* Uniqueness across (possibly concurrent) processes: */
	nums[0] = getpid();
	/* Uniqueness despite recycled pids: */
	/* (Nothing bad happens on overflow here!) */
	nums[1] = time(NULL);
	/* Uniqueness in the face of rapid pid-reuse or system clock change: */
	nums[2] = pcrandom32();
	/* Uniqueness within a single process: */
	nums[3] = local++;
	sprintf(buf, "%"PRIx32".%"PRIx32".%"PRIx32".%"PRIx32,
		nums[0], nums[1], nums[2], nums[3]);
	return buf;
}

int vrfylocal(const char *name)
{
	/* Make sure name isn't some weird file path. */
	if (name[0] == '\0') return 0;
	if (name[0] == '.') return 0;
	for (const char *c = name; *c; ++c) {
		if (*c == '/') return 0;
		if (!islocalc(*c)) return 0;
	}
	/* Check if a node of that name exists. */
	struct stat info;
	if (stat(name, &info) < 0) return 0; /* TODO maybe log problems? */
	/* Verify that this node is a directory. */
	return S_ISDIR(info.st_mode);
}

void mailpath(char buf[], char local[], char folder[], char mname[])
{
	int llen = strlen(local);
	memcpy(buf, local, llen);
	*(buf + llen) = '/';
	memcpy(buf + llen + 1, folder, 3);
	*(buf + llen + 4) = '/';
	strcpy(buf + llen + 5, mname);
}

