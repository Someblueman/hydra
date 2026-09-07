#include "fleet/support/json.h"
#include "fleet/support/files.h"
#include "fleet/agent/agent.h"
#include "fleet/task/task.h"
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

static const struct { const char *name, *json; } builtins[] = {
    {"codex", "{\"schema_version\":1,\"executable\":\"codex\",\"argv\":[\"exec\",\"--json\",\"--color\",\"never\",\"-\"],\"resume_argv\":[\"exec\",\"resume\",\"--json\",{\"input\":\"session_id\"},\"-\"],\"prompt\":\"stdin\",\"adapter\":\"codex-jsonl\",\"session\":\"observed\",\"probe_argv\":[\"exec\",\"--help\"],\"probe_tokens\":[\"--json\",\"resume\"]}"},
    {"claude", "{\"schema_version\":1,\"executable\":\"claude\",\"argv\":[\"--print\",\"--verbose\",\"--output-format\",\"stream-json\",\"--session-id\",{\"input\":\"session_id\"},\"--\",{\"input\":\"prompt\"}],\"resume_argv\":[\"--print\",\"--verbose\",\"--output-format\",\"stream-json\",\"--resume\",{\"input\":\"session_id\"},\"--\",{\"input\":\"prompt\"}],\"prompt\":\"argument\",\"adapter\":\"claude-jsonl\",\"session\":\"generated\",\"probe_argv\":[\"--help\"],\"probe_tokens\":[\"--print\",\"--session-id\",\"--resume\",\"stream-json\"]}"},
    {"pi", "{\"schema_version\":1,\"executable\":\"pi\",\"argv\":[\"--print\",\"--mode\",\"json\",\"--session-id\",{\"input\":\"session_id\"},\"--\",{\"input\":\"prompt\"}],\"resume_argv\":[\"--print\",\"--mode\",\"json\",\"--session\",{\"input\":\"session_id\"},\"--\",{\"input\":\"prompt\"}],\"prompt\":\"argument\",\"adapter\":\"pi-jsonl\",\"session\":\"generated\",\"probe_argv\":[\"--help\"],\"probe_tokens\":[\"--print\",\"--session-id\",\"--session\",\"json\"]}"},
    {"opencode", "{\"schema_version\":1,\"executable\":\"opencode\",\"argv\":[\"run\",\"--format\",\"json\",\"--\",{\"input\":\"prompt\"}],\"resume_argv\":[\"run\",\"--format\",\"json\",\"--session\",{\"input\":\"session_id\"},\"--\",{\"input\":\"prompt\"}],\"prompt\":\"argument\",\"adapter\":\"opencode-jsonl\",\"session\":\"observed\",\"probe_argv\":[\"run\",\"--help\"],\"probe_tokens\":[\"--format\",\"--session\"]}"},
    {"agy", "{\"schema_version\":1,\"executable\":\"agy\",\"argv\":[\"--output-format\",\"stream-json\",\"--disable-slash-commands\",\"--print\",{\"input\":\"prompt\"}],\"resume_argv\":[\"--output-format\",\"stream-json\",\"--disable-slash-commands\",\"--conversation\",{\"input\":\"session_id\"},\"--print\",{\"input\":\"prompt\"}],\"prompt\":\"argument\",\"adapter\":\"agy-jsonl\",\"session\":\"observed\",\"probe_argv\":[\"--help\"],\"probe_tokens\":[\"--print\",\"--conversation\",\"--disable-slash-commands\",\"stream-json\"]}"},
    {"cursor", "{\"schema_version\":1,\"executable\":\"cursor-agent\",\"argv\":[\"--print\",\"--output-format\",\"stream-json\",\"--\",{\"input\":\"prompt\"}],\"resume_argv\":[\"--print\",\"--output-format\",\"stream-json\",\"--resume\",{\"input\":\"session_id\"},\"--\",{\"input\":\"prompt\"}],\"prompt\":\"argument\",\"adapter\":\"cursor-jsonl\",\"session\":\"observed\",\"probe_argv\":[\"--help\"],\"probe_tokens\":[\"--print\",\"--resume\",\"stream-json\"]}"},
    {NULL, NULL}
};
static bool argument_list(json_object *list, bool substitutions) {
    const char *const keys[] = {"input", NULL}; size_t i;
    if (!json_object_is_type(list, json_type_array) || json_object_array_length(list) > AGENT_ARGS) return false;
    for (i = 0; i < json_object_array_length(list); i++) {
        json_object *arg = json_object_array_get_idx(list, i); const char *text = f_text(arg);
        if (text) { if (strlen(text) > 4096) return false; }
        else {
            const char *slot = f_string(arg, "input");
            if (!substitutions || !task_keys(arg, keys) || !slot || (strcmp(slot, "prompt") && strcmp(slot, "task_file") &&
                strcmp(slot, "session_id") && strcmp(slot, "output_dir") && strcmp(slot, "input_dir"))) return false;
        }
    }
    return true;
}
static unsigned slots(json_object *list, const char *slot) {
    size_t i; unsigned count = 0;
    for (i = 0; i < json_object_array_length(list); i++) {
        const char *input = f_string(json_object_array_get_idx(list, i), "input");
        if (input && !strcmp(input, slot)) count++;
    }
    return count;
}
static bool transport(json_object *list, const char *prompt, bool resume, const char *session) {
    if (!argument_list(list, true)) return false;
    if (slots(list, "prompt") != (unsigned)!strcmp(prompt, "argument") || slots(list, "task_file") != (unsigned)!strcmp(prompt, "file")) return false;
    if (resume && slots(list, "session_id") != 1) return false;
    if (!resume && slots(list, "session_id") != (unsigned)!strcmp(session, "generated")) return false;
    return true;
}
json_object *agent_profile_validate(json_object *input) {
    const char *const keys[] = {"schema_version", "executable", "argv", "resume_argv", "prompt", "adapter", "session", "probe_argv", "probe_tokens", NULL};
    const char *executable = f_string(input, "executable"), *prompt = f_string(input, "prompt"), *adapter = f_string(input, "adapter"), *session = f_string(input, "session");
    json_object *copy = NULL;
    if (!task_keys(input, keys) || !f_number_is(input, "schema_version", 1) || !executable || !*executable || strlen(executable) >= F_PATH ||
        (executable[0] != '/' && (!f_name(executable) || strchr(executable, '/'))) || !prompt || !adapter || !session ||
        (strcmp(prompt, "none") && strcmp(prompt, "stdin") && strcmp(prompt, "argument") && strcmp(prompt, "file")) ||
        (strcmp(adapter, "none") && strcmp(adapter, "canonical-jsonl") && strcmp(adapter, "codex-jsonl") && strcmp(adapter, "claude-jsonl") && strcmp(adapter, "pi-jsonl") && strcmp(adapter, "opencode-jsonl") && strcmp(adapter, "agy-jsonl") && strcmp(adapter, "cursor-jsonl")) ||
        (strcmp(session, "none") && strcmp(session, "generated") && strcmp(session, "observed")) ||
        !transport(f_field(input, "argv"), prompt, false, session) ||
        (f_field(input, "resume_argv") && (!strcmp(session, "none") || !transport(f_field(input, "resume_argv"), prompt, true, session))) ||
        !argument_list(f_field(input, "probe_argv"), false) || !argument_list(f_field(input, "probe_tokens"), false)) return NULL;
    if (strcmp(session, "none") && !strcmp(adapter, "none")) return NULL;
    if (json_object_deep_copy(input, &copy, NULL)) return NULL;
    return copy;
}
json_object *agent_profile(const char *name) {
    char path[F_PATH], scalar[F_PATH]; json_object *input = NULL, *profile; size_t i;
    if (!agent_name(name)) return NULL;
    if (snprintf(path, sizeof(path), "%s/profiles/%s/adapter.json", f_home, name) >= (int)sizeof(path)) return NULL;
    /* Custom contracts are explicit. Existing scalar profiles remain launch-only. */
    if (!access(path, F_OK)) input = f_read_json(path, AGENT_PROMPT_LIMIT);
    else {
        if (snprintf(scalar, sizeof(scalar), "%s/profiles/%s/executable", f_home, name) >= (int)sizeof(scalar) || !access(scalar, F_OK)) return NULL;
        for (i = 0; builtins[i].name; i++) if (!strcmp(name, builtins[i].name)) { input = f_parse(builtins[i].json); break; }
    }
    profile = agent_profile_validate(input); json_object_put(input); return profile;
}
bool agent_name(const char *name) { return name && *name && strlen(name) <= 64 && strspn(name, "abcdefghijklmnopqrstuvwxyz0123456789_-") == strlen(name); }
bool agent_builtin(const char *name) {
    size_t i;
    for (i = 0; builtins[i].name; i++) if (!strcmp(name, builtins[i].name)) return true;
    return !strcmp(name, "none") || !strcmp(name, "copilot") || !strcmp(name, "aider") || !strcmp(name, "gemini");
}
bool agent_capability(json_object *profile, const char *capability) {
    if (!strcmp(capability, "headless") || !strcmp(capability, "cancel")) return true;
    if (!strcmp(capability, "prompt") || !strcmp(capability, "safe-point")) return strcmp(f_string(profile, "prompt"), "none") != 0;
    if (!strcmp(capability, "resume")) return f_field(profile, "resume_argv") != NULL;
    if (!strcmp(capability, "observations")) return strcmp(f_string(profile, "adapter"), "none") != 0;
    if (!strcmp(capability, "permission-requests")) return !strcmp(f_string(profile, "adapter"), "canonical-jsonl");
    if (!strcmp(capability, "usage")) return strcmp(f_string(profile, "adapter"), "none") && strcmp(f_string(profile, "adapter"), "cursor-jsonl");
    return false;
}
json_object *agent_capabilities(json_object *profile) {
    const char *names[] = {"headless", "prompt", "cancel", "resume", "safe-point", "observations", "permission-requests", "usage", "cost-limit", NULL};
    json_object *caps = json_object_new_object(); size_t i;
    for (i = 0; names[i]; i++) json_object_object_add(caps, names[i], json_object_new_boolean(agent_capability(profile, names[i])));
    return caps;
}
int agent_profile_hash(json_object *profile, char digest[65]) {
    char scratch[] = "/tmp/hydra-agent-profile.XXXXXX"; int status;
    if (!mkdtemp(scratch)) return -1;
    status = task_json_hash(profile, scratch, digest); f_remove_tree(scratch); return status;
}
json_object *agent_arguments(json_object *profile, const char *mode, json_object *values) {
    json_object *definition = f_field(profile, mode), *args = json_object_new_array(); size_t i;
    if (!definition) goto bad;
    json_object_array_add(args, json_object_new_string(f_string(profile, "executable")));
    for (i = 0; i < json_object_array_length(definition); i++) {
        json_object *arg = json_object_array_get_idx(definition, i); const char *text = f_text(arg);
        if (!text) {
            const char *slot = f_string(arg, "input");
            text = slot ? f_string(values, slot) : NULL;
            if (!text || (!*text && strcmp(slot, "prompt"))) goto bad;
        }
        json_object_array_add(args, json_object_new_string(text));
    }
    return args;
bad:
    json_object_put(args); return NULL;
}
