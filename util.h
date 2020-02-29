/* See LICENSE file for copyright and license details. */

/* needs stddef.h */

void die(const char *fmt, ...);
/* Handle common errno values in the case of an I/O error. */
void ioerr(const char *func);

/* struct str is a helper struct for dynamically-allocated strings.
 * These strings keep track of their own length and as such are not NUL-terminated. */

struct str
{
	char *data;
	size_t len;
	size_t cap;
};

int mkstr(struct str *str, size_t init);
void clrstr(struct str *str);
int strext(struct str *str, size_t len, char *ext);
int strput(struct str *str, char c);
void strdeq(struct str *str, size_t len);

