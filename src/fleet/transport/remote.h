#ifndef HYDRA_FLEET_TRANSPORT_REMOTE_H
#define HYDRA_FLEET_TRANSPORT_REMOTE_H
#include "fleet/fleet.h"
#include <json-c/json.h>

struct f_remote { char name[128], target[256], hydra[F_PATH], home[F_PATH]; bool multiplex; };
struct f_capture;
/* Inputs are borrowed; returned JSON belongs to the caller. SSH capture uses f_capture_free. */
int f_ssh(const struct f_remote *remote, const char *command, const char *input, size_t size, unsigned seconds, bool tty, struct f_capture *cap);
bool f_target(const char *value);
int f_remote_load(const char *name, struct f_remote *remote);
int f_remote_save(const struct f_remote *remote);
json_object *f_remotes(void);
json_object *f_remote_cli(int argc, char **argv);
json_object *f_request(const struct f_remote *remote, json_object *request, unsigned seconds);
json_object *f_observe(const struct f_remote *remote, const char *action, unsigned seconds);
json_object *f_aggregate(const char *action, unsigned seconds, unsigned jobs);
/* Bounded remote observation aggregation with host-local stale-cache projection. */
json_object *f_observation_aggregate(unsigned seconds, unsigned jobs);
#endif
