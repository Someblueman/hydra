#ifndef HYDRA_FLEET_TRANSPORT_REMOTE_H
#define HYDRA_FLEET_TRANSPORT_REMOTE_H
#include "fleet/fleet.h"
#include <json-c/json.h>

struct f_remote { char name[128], target[256], hydra[F_PATH], home[F_PATH]; bool multiplex, require_existing_master;
    /* Transient discovery policy only; never persisted in an alias record. */
    char ssh_config[F_PATH], peer_fingerprint[256];
};
struct f_capture;
/* Inputs are borrowed; returned JSON belongs to the caller. SSH capture uses f_capture_free. */
int f_ssh(const struct f_remote *remote, const char *command, const char *input, size_t size, unsigned seconds, bool tty, struct f_capture *cap);
/* Returns the fingerprint reported by the authenticated SSH peer. The value is
 * caller-owned and must be freed; no known_hosts text is treated as identity. */
char *f_peer_fingerprint(struct f_remote *remote, unsigned seconds);
bool f_target(const char *value);
int f_remote_load(const char *name, struct f_remote *remote);
int f_remote_save(const struct f_remote *remote);
json_object *f_remotes(void);
json_object *f_remote_cli(int argc, char **argv);
json_object *f_request(const struct f_remote *remote, json_object *request, unsigned seconds);
/* Borrowed handshake data; shares the fleet observation compatibility gate. */
bool f_handshake_compatible(json_object *data);
json_object *f_observe(const struct f_remote *remote, const char *action, unsigned seconds);
json_object *f_aggregate(const char *action, unsigned seconds, unsigned jobs);
/* Bounded remote observation aggregation with host-local stale-cache projection. */
json_object *f_observation_aggregate(unsigned seconds, unsigned jobs);
#endif
