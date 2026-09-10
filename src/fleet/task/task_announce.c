#include "fleet/support/files.h"
#include "fleet/support/json.h"
#include "fleet/task/task.h"
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

struct text_buffer { char *value; size_t length, capacity; };
static bool append_number(struct text_buffer *buffer, uint64_t value);

static bool append(struct text_buffer *buffer, const char *text) {
    size_t length = strlen(text), needed = buffer->length + length + 1;
    char *grown;
    if (needed > 262144U) return false;
    if (needed > buffer->capacity) {
        size_t capacity = buffer->capacity ? buffer->capacity : 1024U;
        while (capacity < needed) capacity *= 2U;
        grown = realloc(buffer->value, capacity);
        if (!grown) return false;
        buffer->value = grown; buffer->capacity = capacity;
    }
    memcpy(buffer->value + buffer->length, text, length + 1); buffer->length += length;
    return true;
}

static bool append_field(struct text_buffer *buffer, json_object *object, const char *key, const char *fallback) {
    json_object *value = f_field(object, key); const char *text = f_text(value);
    const unsigned char *p;
    if (!text) text = fallback;
    for (p = (const unsigned char *)text; *p; p++) if (*p < 32 || *p == 127) return false;
    return append(buffer, text);
}

static bool append_sanitized_field(struct text_buffer *buffer, json_object *object, const char *key, const char *fallback) {
    json_object *value = f_field(object, key); const char *text = f_text(value); const unsigned char *p;
    if (!text) text = fallback;
    for (p = (const unsigned char *)text; *p; p++) {
        char replacement[2] = {(char)((*p < 32 || *p > 126) ? '?' : *p), '\0'};
        if (!append(buffer, replacement)) return false;
    }
    return true;
}

static bool append_json_value(struct text_buffer *buffer, json_object *object, const char *key, const char *fallback) {
    json_object *value = f_field(object, key);
    if (json_object_is_type(value, json_type_int)) return append_number(buffer, (uint64_t)json_object_get_int64(value));
    return append_sanitized_field(buffer, object, key, fallback);
}

static bool append_number(struct text_buffer *buffer, uint64_t value) {
    char number[32];
    (void)snprintf(number, sizeof(number), "%llu", (unsigned long long)value);
    return append(buffer, number);
}

static bool bounded_int(json_object *object, const char *key, uint64_t *value) {
    json_object *field = f_field(object, key);
    if (!json_object_is_type(field, json_type_int) || json_object_get_int64(field) < 0 ||
        (uint64_t)json_object_get_int64(field) > UINT32_MAX) return false;
    *value = (uint64_t)json_object_get_int64(field); return true;
}

static bool boolean_field(json_object *object, const char *key, bool *value) {
    json_object *field = f_field(object, key);
    if (!json_object_is_type(field, json_type_boolean)) return false;
    *value = json_object_get_boolean(field) != 0; return true;
}

static json_object *invalid(const char *message) {
    return f_error("fleet-task-announce", "invalid_input", message);
}

json_object *task_announce_cli(int argc, char **argv) {
    const char *input = NULL; json_object *parsed = NULL, *data, *task, *stream, *events;
    struct text_buffer text = {0}; size_t i; uint64_t previous = 0, sequence, cursor, offset, oldest, head;
    bool available, gap, reset, truncated = false; const char *stream_id;
    if (argc != 3 || strcmp(argv[1], "--input") || !argv[2][0]) return invalid("announce requires --input FILE");
    input = argv[2]; parsed = f_read_json(input, 262144U);
    if (!parsed) return invalid("saved observation is missing, malformed, or oversized");
    if (!f_number_is(parsed, "schema_version", 1) || !json_object_is_type(f_field(parsed, "ok"), json_type_boolean) ||
        !json_object_get_boolean(f_field(parsed, "ok")) || strcmp(f_string(parsed, "command"), "fleet-observation")) {
        json_object_put(parsed); return invalid("saved observation envelope is invalid");
    }
    data = f_field(parsed, "data");
    if (!json_object_is_type(data, json_type_object)) { json_object_put(parsed); return invalid("saved observation has no data object"); }
    task = f_field(data, "task");
    if (!json_object_is_type(task, json_type_object) || !f_string(task, "task_id") ||
        strncmp(f_string(task, "task_id"), "task_", 5) || !task_hex(f_string(task, "task_id") + 5, 64) ||
        !task_observation_response_valid(data, f_string(task, "task_id"))) {
        json_object_put(parsed); return invalid("saved observation failed normalized validation");
    }
    stream = f_field(data, "event_observation"); events = f_field(stream, "events");
    if (!json_object_is_type(stream, json_type_object) || !f_number_is(stream, "schema_version", 1) ||
        !json_object_is_type(events, json_type_array) || json_object_array_length(events) > 128U ||
        !boolean_field(stream, "available", &available) || !boolean_field(stream, "retention_gap", &gap) ||
        !boolean_field(stream, "stream_reset", &reset) ||
        !bounded_int(stream, "next_cursor", &cursor) || !bounded_int(stream, "next_byte_offset", &offset) ||
        !bounded_int(stream, "oldest_cursor", &oldest) || !bounded_int(stream, "head_cursor", &head) ||
        !f_text(f_field(stream, "duplicate_policy")) || strcmp(f_text(f_field(stream, "duplicate_policy")), "sequence-cursor") ||
        head < oldest || cursor > head) {
        json_object_put(parsed); return invalid("event observation page is malformed or exceeds its bound");
    }
    if (f_field(stream, "scan_truncated") && !boolean_field(stream, "scan_truncated", &truncated)) goto bad;
    stream_id = f_text(f_field(stream, "stream_id"));
    if (available && !stream_id) { json_object_put(parsed); return invalid("available event history has no stream identity"); }
    if (stream_id) {
        const unsigned char *p;
        if (strlen(stream_id) >= 128) { json_object_put(parsed); return invalid("stream identity is too long"); }
        for (p = (const unsigned char *)stream_id; *p; p++) if (*p < 32 || *p > 126) { json_object_put(parsed); return invalid("stream identity contains non-ASCII characters"); }
    }
    if (!available && json_object_array_length(events)) goto bad;
    if (!append(&text, "task id=") || !append_sanitized_field(&text, task, "task_id", "unknown") ||
        !append(&text, " state=") || !append_sanitized_field(&text, task, "execution_state", "unknown") ||
        !append(&text, " receiver_observed_at=") || !append_json_value(&text, data, "receiver_observed_at", "unavailable") ||
        !append(&text, " evidence=saved_snapshot\n")) goto bad;
    if (!available) {
        if (!append(&text, "history unavailable reason=") || !append_field(&text, stream, "unavailable_reason", "unavailable") || !append(&text, "\n")) goto bad;
    } else if (gap && (!append(&text, "history retention_gap=true\n"))) goto bad;
    if (reset && !append(&text, "history stream_reset=true\n")) goto bad;
    if (truncated && !append(&text, "history truncated=true\n")) goto bad;
    for (i = 0; i < json_object_array_length(events); i++) {
        json_object *event = json_object_array_get_idx(events, i);
        const char *task_run = f_string(task, "run_id"), *task_step = f_string(task, "step_id");
        const char *event_run, *event_step;
        if (!json_object_is_type(event, json_type_object) || !f_number_is(event, "schema_version", 1) ||
            !bounded_int(event, "sequence", &sequence) || !sequence || (previous && sequence != previous + 1U) ||
            !f_text(f_field(event, "type")) || !f_text(f_field(event, "detail"))) { fprintf(stderr, "DEBUG event fields\\n"); goto bad; }
        event_run = f_text(f_field(event, "run_id")); event_step = f_text(f_field(event, "step_id"));
        if ((task_run && event_run && strcmp(task_run, event_run)) || (task_step && event_step && strcmp(task_step, event_step))) { fprintf(stderr, "DEBUG identity\\n"); goto bad; }
        if (!append(&text, "event time=") || !append_sanitized_field(&text, event, "occurred_at", "unknown") ||
            !append(&text, " run=") || !append_sanitized_field(&text, event, "run_id", "-") ||
            !append(&text, " step=") || !append_sanitized_field(&text, event, "step_id", "-") ||
            !append(&text, " sequence=") || !append_number(&text, sequence) ||
            !append(&text, " type=")) goto bad;
        if (!append_sanitized_field(&text, event, "type", "unknown") || !append(&text, " detail=") || !append_sanitized_field(&text, event, "detail", "") || !append(&text, "\n")) goto bad;
        previous = sequence;
    }
    if (previous && previous != cursor) goto bad;
    if (!append(&text, "resume cursor=") || !append_number(&text, cursor) || !append(&text, " byte_offset=") || !append_number(&text, offset)) goto bad;
    if (!append(&text, " stream_id=") || !(stream_id ? append_sanitized_field(&text, stream, "stream_id", "unavailable") : append(&text, "unavailable")) || !append(&text, "\n")) goto bad;
    { json_object *result = json_object_new_object(); f_string_add(result, "task_id", f_string(task, "task_id")); f_string_add(result, "announcement", text.value ? text.value : ""); free(text.value); json_object_put(parsed); return f_success("fleet-task-announce", result); }
bad:
    free(text.value); json_object_put(parsed); return invalid("saved observation contains unsafe or inconsistent event data");
}
