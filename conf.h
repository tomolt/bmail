/* See LICENSE file for copyright and license details. */

struct conf
{
	char *_data;
	char *domain;
	char *spool;
	char *user;
	char *group;
	char *ca_file;
	char *cert_file;
	char *key_file;
	int tls_enable;
};

const char *findconf(void);
struct conf loadconf(const char *filename);
void freeconf(struct conf conf);
void dropprivs(struct conf conf);

