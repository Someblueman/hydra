#include "fleet/support/json.h"
#include "fleet/support/files.h"
#include "fleet/support/process.h"
#include "fleet/agent/agent.h"
#include <stdlib.h>
#include <string.h>
#include <time.h>

/* A failed status observation is retried no sooner than this, so a busy or
 * broken state store costs at most one bounded ingest per interval. */
#define AGENT_OBSERVATION_RETRY_MS 1000LL

bool agent_stop(void *context) {
    struct agent_stream *stream = context; char *current = f_read(stream->current_path, 128);
    if (current) current[strcspn(current, "\r\n")] = '\0';
    if (!current || strcmp(current, stream->instance)) stream->stale = true;
    free(current); return stream->malformed || stream->stale || stream->permission;
}
static int observation(struct agent_stream *stream, const char *status) {
    struct f_capture cap = {0}; char event[512];
    char *argv[] = {(char *)f_hydra, "adapter", "ingest", (char *)stream->branch, NULL};
    int length = snprintf(event, sizeof(event), "{\"schema_version\":1,\"instance_id\":\"%s\",\"kind\":\"observed\",\"status\":\"%s\"}", stream->instance, status);
    int result = length < 0 || length >= (int)sizeof(event) || f_run(argv, event, (size_t)length, 5, &cap) || cap.status;
    if (result) {
        /* Keep why the observation was lost: exit status, deadline, stderr. */
        stream->observation_failures++; stream->observation_exit = cap.status; stream->observation_timed_out = cap.timeout;
        json_object_put(stream->observation_error); stream->observation_error = agent_excerpt(cap.err, cap.err_bytes);
    }
    f_capture_free(&cap); return result ? -1 : 0;
}
static long long now_ms(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (long long)ts.tv_sec * 1000LL + ts.tv_nsec / 1000000L;
}
/* Records the pending status unless the shell already accepted it in this run.
 * Status observations are heuristic, so a failure only delays the next attempt
 * and is kept for the receipt; while the provider runs, a changed current
 * instance still stops it. The final attempt ignores the retry interval. */
static void observation_flush(struct agent_stream *stream, bool final) {
    if (!stream->pending_status[0] || !strcmp(stream->pending_status, stream->recorded_status) || f_stopped) return;
    if (!final && now_ms() < stream->retry_at) return;
    if (!observation(stream, stream->pending_status)) {
        memcpy(stream->recorded_status, stream->pending_status, sizeof(stream->recorded_status));
        return;
    }
    stream->retry_at = now_ms() + AGENT_OBSERVATION_RETRY_MS;
    if (!final && !f_stopped) (void)agent_stop(stream);
}
static void observe_status(struct agent_stream *stream, const char *status) {
    /* Decoded statuses are short literals; anything else is not recordable. */
    if (strlen(status) >= sizeof(stream->pending_status)) return;
    memcpy(stream->pending_status, status, strlen(status) + 1);
    observation_flush(stream, false);
}
void agent_observe_final(struct agent_stream *stream) {
    if (!stream->malformed && !stream->stale) observation_flush(stream, true);
}
static void observed_text(char *out, size_t size, const char *value) {
    size_t i;
    if (!value || out[0] || !*value || strlen(value) >= size) return;
    for (i = 0; value[i]; i++) if ((unsigned char)value[i] < 32 || value[i] == 127) return;
    memcpy(out, value, strlen(value) + 1);
}
/* Records the first model and effort a provider event names; never inferred. */
static void observe_model(struct agent_stream *stream, json_object *input) {
    observed_text(stream->observed_model, sizeof(stream->observed_model), f_string(input, "model"));
    observed_text(stream->observed_model, sizeof(stream->observed_model), f_string(f_field(input, "message"), "model"));
    observed_text(stream->observed_effort, sizeof(stream->observed_effort), f_string(input, "reasoning_effort"));
}
void agent_observe(void *context, const char *text, size_t size) {
    struct agent_stream *stream = context;
    if (stream->malformed || stream->stale || stream->permission || f_stopped) return;
    stream->received = size;
    if (!strcmp(stream->adapter, "none")) return;
    while (stream->consumed < size) {
        stream->line_start = stream->consumed;
        const char *start = text + stream->consumed, *end = memchr(start, '\n', size - stream->consumed);
        size_t length = end ? (size_t)(end - start) : size - stream->consumed;
        if (length > AGENT_EVENT_LIMIT || memchr(start, '\0', length)) { stream->malformed = true; return; }
        if (!end) return;
        stream->consumed += length + 1;
        char *line = malloc(length + 1); json_object *input, *record; struct agent_event event;
        if (!line) { stream->malformed = true; return; }
        memcpy(line, start, length); line[length] = '\0'; input = f_parse(line); free(line);
        int decoded = agent_decode(stream->adapter, input, &event);
        observe_model(stream, input);
        if (decoded < 0 || json_object_array_length(stream->events) >= 1024) { stream->malformed = true; json_object_put(input); return; }
        if (!decoded) { json_object_put(input); continue; }
        if (event.session) {
            stream->session_seen = true;
            if (!f_name(event.session) || strlen(event.session) > 128 || (*stream->session && strcmp(stream->session, event.session))) stream->malformed = true;
            else (void)f_copy(stream->session, sizeof(stream->session), event.session);
        }
        if (event.status && !strcmp(event.status, "failed")) stream->failed = true;
        if (event.permission) stream->permission = true;
        if (event.text && strlen(event.text) <= AGENT_OUTPUT_LIMIT) {
            char *answer = strdup(event.text);
            if (!answer) stream->malformed = true;
            else { free(stream->answer); stream->answer = answer; }
        }
        record = json_object_new_object(); f_string_add(record, "kind", event.kind ? event.kind : "session");
        if (event.status) f_string_add(record, "status", event.status);
        if (event.session) f_string_add(record, "session_id", event.session);
        if (event.permission) f_string_add(record, "request_id", event.permission);
        if (event.usage) {
            json_object_put(stream->usage); stream->usage = event.usage;
            json_object_object_add(record, "usage", json_object_get(event.usage));
        }
        json_object_array_add(stream->events, record);
        if (!stream->malformed && event.status && !f_stopped) observe_status(stream, event.status);
        json_object_put(input);
        if (stream->malformed || stream->stale || stream->permission) return;
    }
}
