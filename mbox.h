/* See LICENSE file for copyright and license details. */

#define UNIQNAME_LEN 35
#define MAILPATH_LEN (LOCAL_LEN+5+UNIQNAME_LEN)

void uniqname(char buf[]);
int vrfylocal(const char *name);

