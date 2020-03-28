/* See LICENSE file for copyright and license details. */

int crit_insert(void **crit, const char *str);
void crit_walkleaves(void *crit, void (*fn)(const char *, void *), void *arg);
void crit_free(void *crit);

