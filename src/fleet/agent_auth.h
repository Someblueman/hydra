#ifndef HYDRA_AGENT_AUTH_H
#define HYDRA_AGENT_AUTH_H
#include "fleet.h"
#define AUTH_LIMIT 65536U
/* Paths and JSON arguments are borrowed. Returned JSON is caller-owned. */
int auth_path(const char *agent, char path[F_PATH]);
int auth_read(const char *path, json_object **value, char digest[65]);
int auth_hash(const char *text, char digest[65]);
int auth_store(const char *path, const char *before, json_object *value);
bool auth_agent(const char *agent);
bool auth_valid(const char *agent, json_object *value);
json_object *auth_serve(json_object *request);
json_object *auth_cli(int argc, char **argv);
#endif
