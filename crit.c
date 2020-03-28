/* See LICENSE file for copyright and license details. */

#include <stdlib.h>
#include <string.h>
#include <stdint.h>

#define ISNODE(x) ((uintptr_t) (x) & 1)
#define MKNODE(x) ((void *) ((uintptr_t) (x) + 1))
#define NODE(x)   ((struct node *) ((uintptr_t) (x) - 1))

struct node
{
	void *child[2];
	unsigned short byte;
	unsigned short bit;
};

static void **step(void *ptr, const char *str, size_t len)
{
	struct node *node = NODE(ptr);
	uint8_t byte = node->byte < len ? str[node->byte] : 0;
	int dir = (byte >> node->bit) & 1;
	return &node->child[dir];
}

int crit_insert(void **crit, const char *str)
{
	size_t len = strlen(str);
	const uint8_t *a, *b;
	void **ptr;
	struct node *junc;
	int dir;
	unsigned int diff;
	/* Special case: crit-bit tree is empty. */
	if (*crit == NULL) {
		*crit = strdup(str); /* TODO error checking */
		return 0;
	}
	/* Find the nearest string that already exists in the tree. */
	ptr = crit;
	while (ISNODE(*ptr)) {
		ptr = step(*ptr, str, len);
	}
	/* Find the first bytes in which the two differ. */
	a = (const uint8_t *) str, b = *ptr;
	while (*a && *a == *b) ++a, ++b;
	/* If they don't differ at all, str already exists in the tree. */
	if (*a == *b) return 1;
	/* Allocate the new junction node that distinguishes str from the nearest string. */
	junc = calloc(1, sizeof(*junc));
	if (junc == NULL) return -1;
	/* Compute exact position and direction of the critical bit. */
	junc->byte = (const uint8_t *) str - a;
	junc->bit = 0;
	diff = *a ^ *b;
	while (diff >>= 1) ++junc->bit;
	dir = (*a >> junc->bit) & 1;
	junc->child[dir] = strdup(str); /* TODO error checking */
	/* Walk the tree until we can insert the junction. */
	ptr = crit;
	while (ISNODE(*ptr)) {
		struct node *node = NODE(*ptr);
		if (node->byte > junc->byte) break;
		if (node->byte == junc->byte && node->bit < junc->bit) break;
		ptr = step(*ptr, str, len);
	}
	/* Insert the junction into the tree. */
	junc->child[!dir] = *ptr;
	*ptr = MKNODE(junc);
	return 0;
}

void crit_walkleaves(void *crit, void (*fn)(const char *, void *), void *arg)
{
	if (ISNODE(crit)) {
		struct node *node = NODE(crit);
		crit_walkleaves(node->child[0], fn, arg);
		crit_walkleaves(node->child[1], fn, arg);
	} else {
		fn(crit, arg);
	}
}

void crit_free(void *crit)
{
	if (ISNODE(crit)) {
		struct node *node = NODE(crit);
		crit_free(node->child[0]);
		crit_free(node->child[1]);
		free(node);
	} else {
		free(crit);
	}
}

#if 0

#include <stdio.h>

static void printleaf(const char *leaf, void *arg)
{
	(void) arg;
	printf("# %s\n", leaf);
}

static void *crit = NULL;

static int turn(void)
{
	char line[128];
	if (fgets(line, 128, stdin) == NULL) return 0;
	size_t len = strlen(line);
	line[--len] = 0;
	if (len == 0) return 0;
	crit_insert(&crit, line);
	crit_walkleaves(crit, printleaf, NULL);
	return 1;
}

static void quit(void)
{
	crit_free(crit);
}

int main()
{
	while (turn()) {}
	quit();
}

#endif

