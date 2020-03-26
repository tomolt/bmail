/* See LICENSE file for copyright and license details. */

/* needs tls.h */

extern int cnsock;
extern struct tls *cntls;
extern int (*cread)(char *buf, int max);
extern int (*cwrite)(char *buf, int max);

struct tls_config *conftls(const char *conf[]);
int cread_plain(char *buf, int max);
int cwrite_plain(char *buf, int max);
int cread_tls(char *buf, int max);
int cwrite_tls(char *buf, int max);
int creadln(char *buf, int max);
void cwritent(char *buf);

