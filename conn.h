/* See LICENSE file for copyright and license details. */

/* needs conf.h */

void servercn(struct conf conf);
void clientcn(struct conf conf);
void closecn(void);
int cnstarttls(void);
int cncantls(void);
void cnrecv(char *buf, int len);
void cnsend(char *buf, int len);
int cnrecvln(char *buf, int max);
