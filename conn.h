/* See LICENSE file for copyright and license details. */

void servercn(const char *conf[]);
void clientcn(const char *conf[]);
void closecn(void);
int cnstarttls(void);
int cncantls(void);
int cnrecv(char *buf, int max);
void cnsend(char *buf, int len);
int cnrecvln(char *buf, int max);
void cnsendnt(char *buf);
