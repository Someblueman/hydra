#ifndef HYDRA_AGENT_H
#define HYDRA_AGENT_H
#include "fleet/fleet.h"
#include "fleet/support/json.h"

struct f_capture;

#define AGENT_ARGS 128U
#define AGENT_PROMPT_LIMIT 65536U
#define AGENT_EVENT_LIMIT 32768U
#define AGENT_OUTPUT_LIMIT (1024U * 1024U)
#define AGENT_DIAGNOSTIC_LIMIT 4096U
/* All returned JSON objects are owned by the caller. */
json_object *agent_profile(const char *name);
json_object *agent_profile_validate(json_object *input);
json_object *agent_profile_cli(int argc, char **argv);
json_object *agent_capabilities(json_object *profile);
json_object *agent_probe(json_object *profile);
bool agent_capability(json_object *profile, const char *capability);
int agent_profile_hash(json_object *profile, char digest[65]);
bool agent_builtin(const char *name);
bool agent_name(const char *name);
/* Recorded agent locations ($HYDRA_HOME/agents/locations/EXECUTABLE), shared
 * with lib/profiles.sh. A path is valid when it is absolute, ends in
 * /executable, names an executable regular file (symlinks are followed but the
 * invoked name is kept), the entry and its target belong to the user or root,
 * and the target is not group- or world-writable. The record file itself must
 * be a regular non-symlink file owned by the user and not group- or
 * world-writable. Executable resolution is PATH first, then a valid record. */
bool agent_location_name(const char *executable);
bool agent_location_valid(const char *executable, const char *path);
/* Caller-owned recorded path, or NULL. */
char *agent_location_recorded(const char *executable);
/* Remote inventory client (agent_inventory.c). remote is borrowed.
 * agent_remote_inventory returns a caller-owned copy of the receiver's
 * agent-inventory data, or NULL when the receiver lacks the capability or
 * answered invalidly. Rows and locations are borrowed from the inventory. */
struct f_remote;
bool agent_remote_capability(const struct f_remote *remote, const char *capability, unsigned seconds);
json_object *agent_remote_request(const struct f_remote *remote, const char *action, const char *const *args, unsigned seconds);
json_object *agent_remote_inventory(const struct f_remote *remote, unsigned seconds);
json_object *agent_inventory_row(json_object *inventory, const char *executable);
/* Absolute path to run: the PATH hit, the valid record, or the single
 * recordable candidate of a found_off_path row; NULL otherwise. */
const char *agent_inventory_location(json_object *row);
/* Argument strings belong to the returned JSON array; keep it alive while used. */
json_object *agent_arguments(json_object *profile, const char *mode, json_object *values);
json_object *agent_run_cli(int argc, char **argv);
struct agent_event {
    const char *kind, *status, *session, *text, *permission;
    json_object *usage;
};
/* Event strings borrow the input; usage is owned and must be released. */
int agent_decode(const char *adapter, json_object *input, struct agent_event *event);
struct agent_stream {
    const char *adapter, *branch, *instance, *current_path;
    char session[129];
    char *answer;
    size_t consumed, received, line_start;
    bool malformed, stale, failed, permission, session_seen, observation_failed;
    json_object *events, *usage;
    /* A model or effort the provider itself reported in its event stream. */
    char observed_model[97], observed_effort[33];
};
void agent_observe(void *context, const char *text, size_t size);
bool agent_stop(void *context);
/* Caller-owned provider configuration record, or NULL; see agent_config.c. */
json_object *agent_configuration(json_object *profile, json_object *args);
/* Read-only views: one receipt summary row, and readable provider events. */
json_object *agent_view_cli(int argc, char **argv);
int agent_retain(const char *head, const char *run, const struct f_capture *capture);
/* Bounded UTF-8-safe excerpt {text,bytes,truncated} of size bytes, or NULL when empty. */
json_object *agent_excerpt(const char *text, size_t size);
/* Adds "diagnostic" to a failed receipt only: undecoded stdout and stderr excerpts. */
void agent_diagnose(json_object *record, const struct agent_stream *stream, const struct f_capture *cap, int status);
#endif
