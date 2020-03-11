/* See LICENSE file for copyright and license details. */

/* Write a printf-style error message to syslog and terminate.
 * If fmt ends with ':' a textual description of the current
 * state of errno will be written as well. */
void die(const char *fmt, ...);
/* Handle common errno values in the case of an I/O error. */
void ioerr(const char *func);
/* Register a signal handler to be called on termination. */
void handlesignals(void (*handler)(int));
/* Automatically reap all child processes. */
void reapchildren(void);
/* Portably generate cryptographic random 32-bit numbers. */
unsigned long pcrandom32(void);

