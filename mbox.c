/* See LICENSE file for copyright and license details. */

#include <stdio.h>
#include <time.h>
#include <errno.h>
#include <sys/types.h>
#include <unistd.h>
#include <fcntl.h>
#include <inttypes.h>

#include "util.h"
#include "smtp.h"
#include "mbox.h"

static uint32_t sequence;
static uint32_t local;

void updsequence(void)
{
	/* Try to read sequence file, if it exists. */
	sequence = 0;
	FILE *file = fopen("sequence", "r");
	if (file != NULL) {
		fscanf(file, "%"SCNx32, &sequence);
		fclose(file);
	}
	/* Write next sequence number back into the file. */
	file = fopen("sequence", "w");
	if (file == NULL) die("Can't write sequence file:");
	fprintf(file, "%08"PRIx32, sequence + 1);
	fclose(file);
}

int openmbox(const char *name)
{
	/* Make sure name isn't some weird file path. */
	if (name[0] == '.') return -1;
	for (const char *c = name; *c; ++c) {
		if (*c == '/') return -1;
		if (!islocalc(*c)) return -1;
	}
	/* Try to open directory of the same name. */
	int fd = open(name, O_DIRECTORY);
	if (fd < 0) {
		if (errno != ENOENT) ioerr("open");
		return -1;
	}
	return fd;
}

char *uniqname(void)
{
	static char buf[UNIQNAME_LEN+1];
	uint32_t nums[4];
	/* Uniqueness across boots: */
	nums[0] = sequence;
	/* Uniqueness across concurrent processes: */
	nums[1] = getpid();
	/* Uniqueness despite recycled pids: */
	/* (Nothing bad happens on overflow here!) */
	nums[2] = time(NULL);
	/* Uniqueness within a single process: */
	nums[3] = local++;
	sprintf(buf, "%"PRIx32".%"PRIx32".%"PRIx32".%"PRIx32,
		nums[0], nums[1], nums[2], nums[3]);
	return buf;
}

