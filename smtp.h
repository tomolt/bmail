/* needs util.h */

int islocalc(char c);
int isaddrc(char c);
int isdomainc(char c);

int pchar(char **ptr, char ch);
int pcrlf(char **ptr);
int pword(char **ptr, char *exp);
int plocal(char **ptr, struct str *str);
int pdomain(char **ptr, struct str *str);
int pmailbox(char **ptr, struct str *local, struct str *domain);

