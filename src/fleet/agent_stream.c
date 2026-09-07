#include "agent.h"
#include <stdlib.h>
#include <string.h>

bool agent_stop(void *context) {
    struct agent_stream *stream = context; char *current = f_read(stream->current_path, 128);
    if (current) current[strcspn(current, "\r\n")] = '\0';
    if (!current || strcmp(current, stream->instance)) stream->stale = true;
    free(current); return stream->malformed || stream->stale || stream->permission || stream->observation_failed;
}
static int observation(struct agent_stream *stream, const char *status) {
    struct f_capture cap = {0}; char event[512];
    char *argv[] = {(char *)f_hydra, "adapter", "ingest", (char *)stream->branch, NULL};
    int length = snprintf(event, sizeof(event), "{\"schema_version\":1,\"instance_id\":\"%s\",\"kind\":\"observed\",\"status\":\"%s\"}", stream->instance, status);
    int result = length < 0 || length >= (int)sizeof(event) || f_run(argv, event, (size_t)length, 5, &cap) || cap.status;
    f_capture_free(&cap); return result ? -1 : 0;
}
void agent_observe(void *context, const char *text, size_t size) {
    struct agent_stream *stream = context;
    if (stream->malformed || stream->stale || stream->permission || stream->observation_failed || f_stopped) return;
    stream->received = size;
    if (!strcmp(stream->adapter, "none")) return;
    while (stream->consumed < size) {
        const char *start = text + stream->consumed, *end = memchr(start, '\n', size - stream->consumed);
        size_t length = end ? (size_t)(end - start) : size - stream->consumed;
        if (length > AGENT_EVENT_LIMIT || memchr(start, '\0', length)) { stream->malformed = true; return; }
        if (!end) return;
        stream->consumed += length + 1;
        char *line = malloc(length + 1); json_object *input, *record; struct agent_event event;
        if (!line) { stream->malformed = true; return; }
        memcpy(line, start, length); line[length] = '\0'; input = f_parse(line); free(line);
        int decoded = agent_decode(stream->adapter, input, &event);
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
        if (!stream->malformed && event.status && !f_stopped && observation(stream, event.status) && !f_stopped) {
            (void)agent_stop(stream);
            if (!stream->stale) stream->observation_failed = true;
        }
        json_object_put(input);
        if (stream->malformed || stream->stale || stream->permission || stream->observation_failed) return;
    }
}
