/* needs stddef.h */

struct str
{
	char *data;
	size_t len;
	size_t cap;
};

void die(const char *fmt, ...);
int mkstr(struct str *str, size_t init);
int strext(struct str *str, size_t len, char *ext);
int strput(struct str *str, char c);

