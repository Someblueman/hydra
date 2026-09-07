#include "fleet/support/json.h"
#include "fleet/support/files.h"
#include "fleet/support/process.h"
#include "fleet/agent/agent.h"
#include "fleet/task/task.h"
#include <errno.h>
#include <fcntl.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>

/* Borrowed strings from the unchanged shell/native argv transport. */
struct run_request {
    const char *profile, *prompt_path, *directory, *project, *head, *instance;
    const char *resume_run, *answer_path, *seconds_text, *required, *retain_text;
    bool preflight;
};
static int run_request_parse(int argc, char **argv, struct run_request *request) {
    memset(request, 0, sizeof(*request));
    request->preflight = argc == 4 && !strcmp(argv[0], "preflight");
    if (!request->preflight && (argc != 12 || strcmp(argv[0], "run"))) return -1;
    request->profile = argv[1];
    request->prompt_path = argv[2];
    if (request->preflight) { request->required = argv[3]; return 0; }
    request->directory = argv[3];
    request->project = argv[4];
    request->head = argv[5];
    request->instance = argv[6];
    request->resume_run = argv[7];
    request->answer_path = argv[8];
    request->seconds_text = argv[9];
    request->required = argv[10];
    request->retain_text = argv[11];
    return 0;
}

static char *prompt_read(const char *path) {
    int fd = open(path, O_RDONLY | O_NOFOLLOW | O_NONBLOCK); struct stat st; char *text = NULL;
    if (fd < 0) return NULL;
    if (fstat(fd, &st) || !S_ISREG(st.st_mode) || st.st_size < 0 || st.st_size > AGENT_PROMPT_LIMIT) goto done;
    text = calloc(AGENT_PROMPT_LIMIT + 2, 1);
    if (text) {
        size_t count = 0; bool failed = false;
        while (count <= AGENT_PROMPT_LIMIT) {
            ssize_t size = read(fd, text + count, AGENT_PROMPT_LIMIT + 1 - count);
            if (size < 0 && errno == EINTR) continue;
            if (size < 0) failed = true;
            if (size <= 0) break;
            count += (size_t)size;
        }
        if (failed || count != (size_t)st.st_size || count > AGENT_PROMPT_LIMIT || memchr(text, '\0', count)) { free(text); text = NULL; }
        else {
            json_object *string = json_object_new_string(text), *checked = f_parse_value(json_object_to_json_string_ext(string, JSON_C_TO_STRING_PLAIN));
            if (!checked) { free(text); text = NULL; }
            json_object_put(string); json_object_put(checked);
        }
    }
done:
    close(fd); return text;
}
static bool requires(json_object *profile, const char *list) {
    char *copy = strdup(list), *cursor = copy; bool supported = copy != NULL;
    while (supported && cursor && *cursor) {
        char *name = cursor; cursor = strchr(cursor, ','); if (cursor) *cursor++ = '\0';
        supported = agent_capability(profile, name) && (!cursor || *cursor);
    }
    free(copy); return supported;
}
static bool identity(const char *value, const char *prefix) { return f_name(value) && !strncmp(value, prefix, strlen(prefix)) && strlen(value) < 128; }
static int session_new(char output[129]) {
    FILE *random = fopen("/dev/urandom", "rb"); unsigned char bytes[16];
    if (!random) return -1;
    size_t count = fread(bytes, 1, sizeof(bytes), random); fclose(random); if (count != sizeof(bytes)) return -1;
    bytes[6] = (bytes[6] & 15) | 64; bytes[8] = (bytes[8] & 63) | 128;
    snprintf(output, 129, "%02x%02x%02x%02x-%02x%02x-%02x%02x-%02x%02x-%02x%02x%02x%02x%02x%02x",
        bytes[0], bytes[1], bytes[2], bytes[3], bytes[4], bytes[5], bytes[6], bytes[7], bytes[8], bytes[9], bytes[10], bytes[11], bytes[12], bytes[13], bytes[14], bytes[15]);
    return 0;
}
static int resume_session(json_object *profile, const struct run_request *request, const char *worktree,
                          const char *hash, struct agent_stream *stream, json_object **prior) {
    char path[F_PATH];
    if (*request->resume_run) {
        if (!identity(request->resume_run, "run_") || !agent_capability(profile, "resume") ||
            snprintf(path, sizeof(path), "%s/state/v2/projects/%s/exec/%s/%s/agent.json", f_home, request->project, request->resume_run, request->head) >= (int)sizeof(path)) return -1;
        *prior = f_read_json(path, AGENT_OUTPUT_LIMIT);
        if (!f_number_is(*prior, "schema_version", 1) || !f_string(*prior, "instance_id") || strcmp(f_string(*prior, "instance_id"), request->instance) ||
            !f_string(*prior, "project_id") || strcmp(f_string(*prior, "project_id"), request->project) || !f_string(*prior, "head_id") || strcmp(f_string(*prior, "head_id"), request->head) ||
            !f_string(*prior, "worktree") || strcmp(f_string(*prior, "worktree"), worktree) ||
            !f_string(*prior, "profile_sha256") || strcmp(f_string(*prior, "profile_sha256"), hash) || !f_string(*prior, "session_id") ||
            !f_number_is(*prior, "exit_status", 0) || f_copy(stream->session, sizeof(stream->session), f_string(*prior, "session_id"))) return -1;
    } else if (!strcmp(f_string(profile, "session"), "generated") && session_new(stream->session)) return -1;
    return 0;
}
static int apply_steering(json_object *profile, char **prompt, struct agent_stream *stream, size_t *steering_bytes) {
    if (!agent_capability(profile, "safe-point")) return 0;
    const char *separator = "\n\nQueued steering at this turn boundary:\n";
    size_t used = strlen(*prompt), separator_size = strlen(separator);
    char budget[32]; struct f_capture messages = {0};
    snprintf(budget, sizeof(budget), "%zu", used + separator_size < AGENT_PROMPT_LIMIT ? AGENT_PROMPT_LIMIT - used - separator_size : 0);
    char *drain[] = {(char *)f_hydra, "adapter", "safe-point", (char *)stream->branch, (char *)stream->instance, budget, NULL};
    if (f_run(drain, NULL, 0, 5, &messages) || messages.status) { f_capture_free(&messages); return -1; }
    if (messages.out_bytes) {
        *steering_bytes = messages.out_bytes;
        if (used + separator_size > AGENT_PROMPT_LIMIT || messages.out_bytes > AGENT_PROMPT_LIMIT - used - separator_size || memchr(messages.out, '\0', messages.out_bytes)) { f_capture_free(&messages); return -1; }
        char *joined = malloc(used + separator_size + messages.out_bytes + 1);
        if (!joined) { f_capture_free(&messages); return -1; }
        memcpy(joined, *prompt, used); memcpy(joined + used, separator, separator_size); memcpy(joined + used + separator_size, messages.out, messages.out_bytes + 1);
        free(*prompt); *prompt = joined;
    }
    f_capture_free(&messages);
    if (agent_stop(stream)) return -1;
    return 0;
}
static const char *end_state(int status, const struct f_capture *cap, const struct agent_stream *stream) {
    const char *end_state = status ? "failed" : "completed";
    if (cap->status == 130) end_state = "cancelled";
    if (cap->timeout) end_state = "timed_out";
    if (stream->permission) end_state = "permission_required";
    if (stream->observation_failed) end_state = "observation_unavailable";
    if (stream->malformed) end_state = "malformed_output";
    if (stream->stale) end_state = "stale_instance";
    return end_state;
}
/* Caller owns the returned observation object; stream and capture are borrowed. */
static json_object *observations(const struct agent_stream *stream, const struct f_capture *cap,
                             bool invoked, bool resumed, int status, size_t steering_bytes,
                             json_object *profile) {
    json_object *observed = json_object_new_object();
    json_object_object_add(observed, "headless", invoked ? json_object_new_boolean(true) : NULL);
    json_object_object_add(observed, "prompt", invoked && strcmp(f_string(profile, "prompt"), "none") ? json_object_new_boolean(true) : NULL);
    json_object_object_add(observed, "observations", json_object_array_length(stream->events) ? json_object_new_boolean(true) : NULL);
    json_object_object_add(observed, "resume", resumed && stream->session_seen && !status ? json_object_new_boolean(true) : NULL);
    json_object_object_add(observed, "cancel", invoked && !cap->stop_unknown && !cap->timeout && (cap->cancelled || f_stopped) && !stream->permission && !stream->stale && !stream->malformed && !stream->observation_failed ? json_object_new_boolean(true) : NULL);
    json_object_object_add(observed, "safe-point", invoked && steering_bytes ? json_object_new_boolean(true) : NULL);
    json_object_object_add(observed, "permission-requests", stream->permission ? json_object_new_boolean(true) : NULL);
    json_object_object_add(observed, "usage", f_field(stream->usage, "input_tokens") || f_field(stream->usage, "output_tokens") || f_field(stream->usage, "cost_usd") ? json_object_new_boolean(true) : NULL);
    return observed;
}
/* Borrow profile, argv storage, prompt, stream and cap. Cap buffers remain caller-owned. */
static bool invoke_agent(json_object *profile, json_object *args, const char *prompt,
                         unsigned seconds, struct agent_stream *stream, struct f_capture *cap) {
    struct f_control control = {0};
    char *command[AGENT_ARGS + 2];
    size_t i;
    for (i = 0; i < json_object_array_length(args); i++) command[i] = (char *)f_text(json_object_array_get_idx(args, i));
    command[i] = NULL;
    control.log_fd[0] = control.log_fd[1] = -1; control.context = stream; control.stop = agent_stop; control.observe = agent_observe; control.grace_seconds = 1;
    bool stdin_prompt = !strcmp(f_string(profile, "prompt"), "stdin");
    return !f_run_controlled(command, stdin_prompt ? prompt : NULL, stdin_prompt ? strlen(prompt) : 0, seconds ? seconds : 86400, cap, &control) && cap->status != 127;
}

/* Internal argv transport, invoked only by the shell exec supervisor. */
json_object *agent_run_cli(int argc, char **argv) {
    json_object *profile = NULL, *probe = NULL, *result = NULL, *record = NULL, *values = NULL, *args = NULL, *prior = NULL;
    char *prompt = NULL, *branch = NULL, root[F_PATH], current[F_PATH], path[F_PATH], worktree[F_PATH], hash[65], prompt_hash[65];
    struct agent_stream stream = {0}; struct f_capture cap = {0};
    struct run_request request;
    bool invoked = false; int status = 1; size_t steering_bytes = 0;
    if (run_request_parse(argc, argv, &request)) goto invalid;
    profile = agent_profile(request.profile);
    if (!profile || !(prompt = prompt_read(request.prompt_path)) || !requires(profile, request.required) || agent_profile_hash(profile, hash)) goto invalid;
    probe = agent_probe(profile);
    if (!json_object_get_boolean(f_field(f_field(probe, "probed"), "executable_available")) ||
        !json_object_get_boolean(f_field(f_field(probe, "probed"), "invocation_help"))) goto invalid;
    if (request.preflight) { result = f_success("agent-preflight", json_object_get(probe)); goto done; }
    if (!*request.seconds_text || strspn(request.seconds_text, "0123456789") != strlen(request.seconds_text) || strtoul(request.seconds_text, NULL, 10) > 86400 ||
        (strcmp(request.retain_text, "0") && strcmp(request.retain_text, "1"))) goto invalid;
    if (!getcwd(worktree, sizeof(worktree)) || !identity(request.project, "project_") || !identity(request.head, "head_") || !identity(request.instance, "instance_") ||
        snprintf(root, sizeof(root), "%s/state/v2/projects/%s/heads/%s", f_home, request.project, request.head) >= (int)sizeof(root) ||
        f_path(current, sizeof(current), root, "current-instance") || f_path(path, sizeof(path), root, "branch") || !(branch = f_read(path, 512))) goto invalid;
    branch[strcspn(branch, "\r\n")] = '\0';
    stream.adapter = f_string(profile, "adapter"); stream.branch = branch; stream.instance = request.instance; stream.current_path = current;
    stream.events = json_object_new_array();
    if (agent_stop(&stream)) goto invalid;
    if (resume_session(profile, &request, worktree, hash, &stream, &prior) ||
        apply_steering(profile, &prompt, &stream, &steering_bytes)) goto invalid;
    /* File transport gets an immutable private copy, removed after execution. */
    if (f_path(path, sizeof(path), request.directory, ".agent-prompt") || f_write(path, prompt, strlen(prompt), false) || f_hash(path, prompt_hash)) goto invalid;
    values = json_object_new_object(); f_string_add(values, "prompt", prompt); f_string_add(values, "task_file", path);
    f_string_add(values, "session_id", stream.session);
    f_string_add(values, "input_dir", getenv("HYDRA_WORKFLOW_INPUTS_DIR")); f_string_add(values, "output_dir", getenv("HYDRA_WORKFLOW_OUTPUTS_DIR"));
    args = agent_arguments(profile, *request.resume_run ? "resume_argv" : "argv", values); if (!args) { unlink(path); goto invalid; }
    record = json_object_new_object(); json_object_object_add(record, "schema_version", json_object_new_int(1));
    f_string_add(record, "profile", request.profile); f_string_add(record, "profile_sha256", hash); f_string_add(record, "instance_id", request.instance);
    f_string_add(record, "project_id", request.project); f_string_add(record, "head_id", request.head); f_string_add(record, "worktree", worktree);
    f_string_add(record, "prompt_sha256", prompt_hash); f_string_add(record, "state", "running");
    json_object_object_add(record, "probe", json_object_get(probe));
    json_object_object_add(record, "started_at", json_object_new_int64((int64_t)time(NULL)));
    if (*stream.session) f_string_add(record, "session_id", stream.session);
    if (task_write_json(request.directory, "agent.json", record, false)) { unlink(path); goto invalid; }
    invoked = invoke_agent(profile, args, prompt, (unsigned)strtoul(request.seconds_text, NULL, 10), &stream, &cap);

    unlink(path);
    /* The shell watchdog may reach the same deadline before this helper does.
     * Its durable marker distinguishes deadline signals from user cancellation. */
    {
        struct stat timed;
        if (!f_path(path, sizeof(path), request.directory, ".timed-out") && !lstat(path, &timed) && S_ISREG(timed.st_mode)) {
            cap.timeout = true; cap.status = 124;
        }
    }
    if (strcmp(stream.adapter, "none") && stream.received != stream.consumed && !cap.cancelled && !f_stopped && !cap.timeout) stream.malformed = true;
    if (stream.stale || stream.malformed || stream.observation_failed) status = 125;
    else if (stream.permission) status = 3;
    else if (stream.failed && !cap.status) status = 1;
    else status = cap.status;
    if (!status && *request.answer_path) {
        const char *answer = !strcmp(stream.adapter, "none") ? cap.out : stream.answer;
        size_t bytes = !strcmp(stream.adapter, "none") ? cap.out_bytes : (answer ? strlen(answer) : 0);
        if (!answer || bytes > AGENT_OUTPUT_LIMIT || f_write(request.answer_path, answer, bytes, false)) status = 125;
    }
    if (!strcmp(request.retain_text, "1")) {
        char parent[F_PATH], *slash;
        if (f_copy(parent, sizeof(parent), request.directory) || !(slash = strrchr(parent, '/'))) goto invalid;
        *slash = '\0'; slash = strrchr(parent, '/');
        bool retained = slash && !agent_retain(root, slash + 1, &cap);
        if (!retained) status = 125;
        json_object_object_add(record, "raw_retained", json_object_new_boolean(retained));
        json_object_object_add(record, "raw_truncated", json_object_new_boolean(cap.out_bytes > AGENT_OUTPUT_LIMIT || cap.err_bytes > AGENT_OUTPUT_LIMIT));
    }
    json_object_object_add(record, "exit_status", json_object_new_int(status));
    f_string_add(record, "state", end_state(status, &cap, &stream));
    json_object_object_add(record, "finished_at", json_object_new_int64((int64_t)time(NULL)));
    json_object_object_add(record, "events", json_object_get(stream.events));
    json_object_object_add(record, "usage", json_object_get(stream.usage));
    json_object_object_add(record, "observed", observations(&stream, &cap, invoked, *request.resume_run, status, steering_bytes, profile));

    if (*stream.session) f_string_add(record, "session_id", stream.session);
    json_object_object_add(record, "verification_passed", json_object_new_boolean(false));
    if (task_write_json(request.directory, "agent.json", record, true)) goto invalid;
    result = f_success("agent-run", json_object_get(record)); goto done;
invalid:
    result = f_error("agent-run", "capability_unavailable", "profile, prompt, required capability, current instance, exact resume evidence, or receipt storage is unavailable; inspect recorded execution before retrying");
done:
    free(prompt); free(branch); free(stream.answer); json_object_put(stream.events); json_object_put(stream.usage);
    json_object_put(profile); json_object_put(probe); json_object_put(record); json_object_put(values); json_object_put(args); json_object_put(prior); f_capture_free(&cap); return result;
}
