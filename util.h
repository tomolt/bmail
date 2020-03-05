/* See LICENSE file for copyright and license details. */

/* needs stddef.h */

void die(const char *fmt, ...);
/* Handle common errno values in the case of an I/O error. */
void ioerr(const char *func);

void handlesignals(void (*handler)(int));

