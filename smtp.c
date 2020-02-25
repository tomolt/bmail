#include <stdlib.h>
#include <unistd.h>
#include <assert.h>

#include "util.h"
#include "smtp.h"

int recviline(int fd, struct str *str)
{
	char cr = 0;
	for (;;) {
		char ch;
		ssize_t s = read(fd, &ch, 1);
		if (s <= 0) return -1;
		if (strput(str, ch) < 0) return -1;
		if (cr && ch == '\n') return 0;
		cr = (ch == '\r');
	}
}

int islocalc(char c)
{
	/* !#$%&'*+-./09=?AZ^_`az{|}~ */
	if (c == 33) return 1;
	if (c > 34 && c < 40) return 1;
	if (c == 42 || c == 43) return 1;
	if (c > 44 && c < 58) return 1;
	if (c == 61) return 1;
	if (c > 64 && c <  91) return 1;
	if (c > 93 && c < 127) return 1;
	return 0;
}

int isaddrc(char c)
{
	/* anything except whitespace and [\] */
	if (c > 32 && c <  91) return 1;
	if (c > 93 && c < 127) return 1;
	return 0;
}

int isdomainc(char c)
{
	/* -.AZaz */
	if (c == 45 || c == 46) return 1;
	if (c > 64 && c <  91) return 1;
	if (c > 96 && c < 123) return 1;
	return 0;
}

int pchar(char **ptr, char ch)
{
	int s = (**ptr == ch);
	*ptr += s;
	return s;
}

int pcrlf(char **ptr)
{
	return pchar(ptr, '\r') && pchar(ptr, '\n');
}

int pword(char **ptr, char *exp)
{
	char *cur = *ptr;
	do {
		char ec = *exp;
		assert(ec > 64 && ec < 91);
		char cc = *cur & 0xDF;
		if (ec != cc) return 0;
		++exp, ++cur;
	} while (*exp);
	*ptr = cur;
	return 1;
}

int plocal(char **ptr, struct str *str)
{
	char *c = *ptr;
	/* TODO quoted local */
	if (!islocalc(*c)) return 0;
	do if (strput(str, *c++) < 0) return 0;
	while (islocalc(*c));
	*ptr = c;
	return 1;
}

int pdomain(char **ptr, struct str *str)
{
	char *c = *ptr;
	if (*c == '[') {
		do if (strput(str, *c++) < 0) return 0;
		while (isaddrc(*c));
		if (*c != ']') return 0;
		if (strput(str, *c++) < 0) return 0;
	} else {
		if (!isdomainc(*c)) return 0;
		do if (strput(str, *c++) < 0) return 0;
		while (isdomainc(*c));
	}
	*ptr = c;
	return 1;
}

int pmailbox(char **ptr, struct str *local, struct str *domain)
{
	return plocal(ptr, local) && pchar(ptr, '@') && pdomain(ptr, domain);
}

