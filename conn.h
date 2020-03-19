/* See LICENSE file for copyright and license details. */

/* needs conf.h */

void servercn(struct conf conf);
void clientcn(struct conf conf);
void closecn(void);
int cnstarttls(void);
int cncantls(void);
void cnsend(char *buf, int len);
/* Read a CRLF-terminated line from stdin into line, and its length into len.
 * The line is not NULL-terminated and may contain any raw byte values,
 * including NULL. If a line is longer than max readline() will only return
 * up to the first max characters. The rest of the line can be read with
 * subsequent calls to readline(). Incomplete lines like these are guaranteed
 * to never contain only part of a CRLF sequence.
 * Returns 0 if line hasn't been read completely and 1 otherwise. */
int readline(char line[], int max, int *len);
