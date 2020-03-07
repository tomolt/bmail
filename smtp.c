/* See LICENSE file for copyright and license details. */

#include <stdlib.h>
#include <unistd.h>
#include <assert.h>

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

int plocal(char str[])
{
	char *c = cphead;
	/* TODO quoted local */
	if (!islocalc(*c)) return 0;
	int i = 0;
	do {
		if (i >= LOCAL_LEN) return 0;
		str[i++] = *c++;
	} while (islocalc(*c));
	str[i] = 0;
	cphead = c;
	return 1;
}

int pdomain(char str[])
{
	char *c = cphead;
	int i = 0;
	if (*c == '[') {
		do {
			if (i >= DOMAIN_LEN-1) return 0;
			str[i++] = *c++;
		} while (isaddrc(*c));
		if (*c != ']') return 0;
		str[i++] = *c++;
	} else {
		if (!isdomainc(*c)) return 0;
		do {
			if (i >= DOMAIN_LEN) return 0;
			str[i++] = *c++;
		} while (isdomainc(*c));
	}
	str[i] = 0;
	cphead = c;
	return 1;
}

int pmailbox(char local[], char domain[])
{
	return plocal(local) && pchar('@') && pdomain(domain);
}

int phelo(char domain[])
{
	return pchar(' ') && pdomain(domain) && pcrlf();
}

int pmail(char local[], char domain[])
{
	int s =  pchar(' ') && pword("FROM") && pchar(':');
	s = s && pchar('<') && pmailbox(local, domain) && pchar('>');
	return   pcrlf();
}

int prcpt(char local[], char domain[])
{
	int s =  pchar(' ') && pword("TO") && pchar(':');
	s = s && pchar('<') && pmailbox(local, domain) && pchar('>');
	return   pcrlf();
}

