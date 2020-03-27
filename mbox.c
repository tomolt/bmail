/* See LICENSE file for copyright and license details. */

#include <stdio.h>
#include <string.h>
#include <time.h>
#include <errno.h>
#include <unistd.h>
#include <sys/stat.h>
#include <inttypes.h>

#include "mbox.h"
#include "util.h"

static uint32_t local;

void uniqname(char buf[])
{
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
}

int vrfylocal(const char *name)
{
	/* Make sure name isn't some weird file path. */
	if (name[0] == '\0') return 0;
	if (name[0] == '.') return 0;
	for (const char *c = name; *c; ++c) {
		if (*c == '/') return 0;
	}
	/* Check if a node of that name exists. */
	struct stat info;
	if (stat(name, &info) < 0) return 0; /* TODO maybe log problems? */
	/* Verify that this node is a directory. */
	return S_ISDIR(info.st_mode);
}

