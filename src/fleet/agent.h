#ifndef HYDRA_AGENT_H
#define HYDRA_AGENT_H
#include "workflow_data.h"

#define AGENT_ARGS 128U
#define AGENT_PROMPT_LIMIT 65536U
#define AGENT_EVENT_LIMIT 32768U
#define AGENT_OUTPUT_LIMIT (1024U * 1024U)
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
    size_t consumed, received;
    bool malformed, stale, failed, permission, session_seen, observation_failed;
    json_object *events, *usage;
};
void agent_observe(void *context, const char *text, size_t size);
bool agent_stop(void *context);
int agent_retain(const char *head, const char *run, const struct f_capture *capture);
#endif
