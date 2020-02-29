/* See LICENSE file for copyright and license details. */

/* needs util.h */

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
 * On failure, they return 0 and don't modify cphead.
 * Parsing functions never initialize or free any struct str arguments. */

/* Matches the single character ch. */
int pchar(char ch);
/* Matches CR LF. */
int pcrlf(void);
/* Matches the word exp. exp may only consist of uppercase ASCII characters. */
int pword(char *exp);
/* Parses the local part of an e-mail address, and returns it in str. */
int plocal(struct str *str);
/* Parses the domain part of an e-mail address, and returns it in str. */
int pdomain(struct str *str);
/* Parses an e-mail address, and returns the local and domain part separately. */
int pmailbox(struct str *local, struct str *domain);

