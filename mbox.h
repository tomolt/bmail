/* See LICENSE file for copyright and license details. */

#define UNIQNAME_LEN 35
#define MAILPATH_LEN (LOCAL_LEN+5+UNIQNAME_LEN)

char *uniqname(void);
int vrfylocal(const char *name);
void mailpath(char buf[], char local[], char folder[], char mname[]);

