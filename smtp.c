/* See LICENSE file for copyright and license details. */

#include <stdlib.h>
#include <unistd.h>
#include <assert.h>

#include "util.h"
#include "smtp.h"

char *cphead;

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

int pchar(char ch)
{
	int s = (*cphead == ch);
	cphead += s;
	return s;
}

int pcrlf(void)
{
	return pchar('\r') && pchar('\n');
}

int pword(char *exp)
{
	char *head = cphead;
	do {
		char ec = *exp;
		assert(ec > 64 && ec < 91);
		char hc = *head & 0xDF;
		if (ec != hc) return 0;
		++exp, ++head;
	} while (*exp);
	cphead = head;
	return 1;
}

int plocal(struct str *str)
{
	char *c = cphead;
	/* TODO quoted local */
	if (!islocalc(*c)) return 0;
	do if (strput(str, *c++) < 0) return 0;
	while (islocalc(*c));
	cphead = c;
	return 1;
}

int pdomain(struct str *str)
{
	char *c = cphead;
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
	cphead = c;
	return 1;
}

int pmailbox(struct str *local, struct str *domain)
{
	return plocal(local) && pchar('@') && pdomain(domain);
}

