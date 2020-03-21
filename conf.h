/* See LICENSE file for copyright and license details. */

enum {
	CF_DOMAIN,
	CF_SPOOL,
	CF_USER,
	CF_GROUP,
	CF_TLS_ENABLE,
	CF_CA_FILE,
	CF_CERT_FILE,
	CF_KEY_FILE,
	CF__DATA_,
	NUM_CF_FIELDS
};

const char *findconf(void);
void loadconf(const char *conf[], const char *filename);
void freeconf(const char *conf[]);
int yesno(const char *value);
void dropprivs(const char *conf[]);

