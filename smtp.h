/* See LICENSE file for copyright and license details. */

#define LOCAL_LEN 64
#define DOMAIN_LEN 255
#define COMMAND_LEN 512
#define PAGE_LEN 256

/* Current read head. Used and modified by all SMTP parsing functions. */
extern char *cphead;

/* Is c a valid character in the local part of an e-mail address? */
int islocalc(char c);
/* Is c a valid character in a generic internet address? */
int isaddrc(char c);
/* Is c a valid character in the domain part of an e-mail address? */
int isdomainc(char c);

/* Functions starting with a single 'p' are SMTP parsing functions.
 * Together, they implement a sort of hand-written LL(1) parser.
 * On sucess, they return 1 and advance cphead behind the matched text.
 * On failure, they return 0 and don't modify cphead. */

/* Matches the single character ch. */
int pchar(char ch);
/* Matches CR LF. */
int pcrlf(void);
/* Matches the word exp. exp may only consist of uppercase ASCII characters. */
int pword(char *exp);
/* Parses the local part of an e-mail address, and returns it in str. */
int plocal(char str[]);
/* Parses the domain part of an e-mail address, and returns it in str. */
int pdomain(char str[]);
/* Parses an e-mail address, and returns the local and domain part separately. */
int pmailbox(char local[], char domain[]);

/* SMTP server-specific parsing functions. */
int phelo(char domain[]);
int pmail(char local[], char domain[]);
int prcpt(char local[], char domain[]);

