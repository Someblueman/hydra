#ifndef HYDRA_FLEET_TRANSPORT_REMOTE_H
#define HYDRA_FLEET_TRANSPORT_REMOTE_H
#include "fleet/fleet.h"
#include <json-c/json.h>

struct f_remote { char name[128], target[256], hydra[F_PATH], home[F_PATH]; bool multiplex, require_existing_master;
    /* Transient discovery policy only; never persisted in an alias record. */
    char ssh_config[F_PATH], peer_fingerprint[256], control_path[F_PATH], ssh_log[F_PATH];
    char principal[128], project[F_PATH], accepted_host_key[256];
};
struct f_capture;
/* Inputs are borrowed; returned JSON belongs to the caller. SSH capture uses f_capture_free. */
int f_ssh(const struct f_remote *remote, const char *command, const char *input, size_t size, unsigned seconds, bool tty, struct f_capture *cap);
/* Returns the fingerprint reported by the authenticated SSH peer. The value is
 * caller-owned and must be freed; no known_hosts text is treated as identity. */
/* Terminal variant of the -t path: forks ssh with the same strict options,
 * waits, restores the local terminal attributes, and stores the child exit
 * status (128+n when ssh or the local command was ended by signal n) in
 * *exit_status. seconds bounds the connection only, never the session.
 * Returns -1 (exit_status -1) when ssh could not be started or waited for;
 * an exec failure is reported as exit status 127. */
int f_ssh_interactive(const struct f_remote *remote, const char *command, unsigned seconds, int *exit_status);
/* Effective client configuration from `ssh -G [-F config] [-l principal] --
 * target` as a caller-owned object of lowercase keyword -> first value (for
 * example "hostname", "port", "hostkeyalias", "userknownhostsfile",
 * "hashknownhosts"). NULL when ssh cannot evaluate the configuration. */
json_object *f_ssh_query_config(const struct f_remote *remote, unsigned seconds);
/* Typed transport failure for a completed SSH capture (timeout, offline,
 * host_key_failed, authentication_failed, ...). Static string. */
const char *f_transport_code(const struct f_capture *cap);
/* OpenSSH's own reason for a failure: the last stderr line that is not -v
 * debug output or a success banner ("ssh: Could not resolve hostname h: ...",
 * "Permission denied (publickey).", "Connection timed out ..."), with control
 * characters replaced by '?'. Empty when there is none. */
void f_ssh_reason(const char *err, char *out, size_t size);
/* First "Server host key: ... SHA256:..." fingerprint in an ssh -v log;
 * caller-owned, NULL when absent. */
char *f_peer_from_log(const char *text);
char *f_peer_fingerprint(struct f_remote *remote, unsigned seconds);
void f_peer_close(struct f_remote *remote);
bool f_target(const char *value);
int f_remote_load(const char *name, struct f_remote *remote);
int f_remote_save(const struct f_remote *remote);
int f_remote_enrolled(const struct f_remote *remote, bool publish);
json_object *f_remotes(void);
json_object *f_remote_cli(int argc, char **argv);
json_object *f_request(const struct f_remote *remote, json_object *request, unsigned seconds);
/* As f_request, also expose application-protocol byte counts from the
 * completed stdin/stdout pump.  SSH framing and stderr diagnostics are not
 * included. */
json_object *f_request_measured(const struct f_remote *remote, json_object *request,
                                unsigned seconds, size_t *request_bytes,
                                size_t *response_bytes, bool *request_complete);
/* Borrowed handshake data; shares the fleet observation compatibility gate. */
bool f_handshake_compatible(json_object *data);
json_object *f_observe(const struct f_remote *remote, const char *action, unsigned seconds);
json_object *f_aggregate(const char *action, unsigned seconds, unsigned jobs);
/* Bounded remote observation aggregation with host-local stale-cache projection. */
json_object *f_observation_aggregate(unsigned seconds, unsigned jobs);
#endif
