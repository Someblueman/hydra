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

static bool append_sanitized_field(struct text_buffer *buffer, json_object *object, const char *key, const char *fallback) {
    json_object *value = f_field(object, key); const char *text = f_text(value); size_t i, length;
    if (!text) text = fallback;
    length = strlen(text);
    for (i = 0; i < length; i++) {
        unsigned char byte = (unsigned char)text[i];
        char replacement[2] = {(char)((byte < 32 || byte > 126) ? '?' : byte), '\0'};
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

struct announce_page { json_object *data, *task, *stream, *events; uint64_t cursor, offset, oldest, head; bool available, gap, reset, truncated; const char *stream_id; };

static bool page_envelope(json_object *parsed, struct announce_page *page) {
    const char *command = f_string(parsed, "command");
    if (!f_number_is(parsed, "schema_version", 1) || !json_object_is_type(f_field(parsed, "ok"), json_type_boolean) ||
        !json_object_get_boolean(f_field(parsed, "ok")) || !command || strcmp(command, "fleet-observation")) return false;
    page->data = f_field(parsed, "data"); page->task = f_field(page->data, "task");
    if (!json_object_is_type(page->data, json_type_object) || !json_object_is_type(page->task, json_type_object)) return false;
    if (!f_string(page->task, "task_id") || strncmp(f_string(page->task, "task_id"), "task_", 5) ||
        !task_hex(f_string(page->task, "task_id") + 5, 64)) return false;
    return task_observation_response_valid(page->data, f_string(page->task, "task_id"));
}

static bool page_stream(struct announce_page *page) {
    const unsigned char *p;
    page->stream = f_field(page->data, "event_observation"); page->events = f_field(page->stream, "events");
    if (!json_object_is_type(page->stream, json_type_object) || !f_number_is(page->stream, "schema_version", 1) ||
        !json_object_is_type(page->events, json_type_array) || json_object_array_length(page->events) > 128U ||
        !boolean_field(page->stream, "available", &page->available) || !boolean_field(page->stream, "retention_gap", &page->gap) ||
        !boolean_field(page->stream, "stream_reset", &page->reset) || !bounded_int(page->stream, "next_cursor", &page->cursor) ||
        !bounded_int(page->stream, "next_byte_offset", &page->offset) || !bounded_int(page->stream, "oldest_cursor", &page->oldest) ||
        !bounded_int(page->stream, "head_cursor", &page->head) || !f_text(f_field(page->stream, "duplicate_policy")) ||
        strcmp(f_text(f_field(page->stream, "duplicate_policy")), "sequence-cursor") || page->head < page->oldest || page->cursor > page->head) return false;
    page->truncated = false;
    if (f_field(page->stream, "scan_truncated") && !boolean_field(page->stream, "scan_truncated", &page->truncated)) return false;
    page->stream_id = f_text(f_field(page->stream, "stream_id"));
    if (!page->available && json_object_array_length(page->events)) return false;
    if (page->available && !page->stream_id) return false;
    if (page->stream_id && strlen(page->stream_id) >= 128) return false;
    if (page->stream_id) for (p = (const unsigned char *)page->stream_id; *p; p++) if (*p < 32 || *p > 126) return false;
    return true;
}

static bool event_binding_valid(json_object *event, const char *task_run) {
    const char *event_run = f_string(event, "run_id");
    json_object *step = f_field(event, "step_id");
    /* The current step is not a filter over earlier or sibling events. */
    return task_run && event_run && !strcmp(task_run, event_run) &&
        (!step || json_object_is_type(step, json_type_null) || f_text(step));
}

static bool render_events(struct text_buffer *text, const struct announce_page *page) {
    size_t i; uint64_t previous = 0, sequence; const char *task_run = f_string(page->task, "run_id");
    for (i = 0; i < json_object_array_length(page->events); i++) {
        json_object *event = json_object_array_get_idx(page->events, i);
        if (!json_object_is_type(event, json_type_object) || !f_number_is(event, "schema_version", 1) ||
            !f_text(f_field(event, "occurred_at")) || !bounded_int(event, "sequence", &sequence) || !sequence ||
            (!previous && sequence <= page->oldest) || (previous && sequence != previous + 1U) || !f_text(f_field(event, "type")) || !f_text(f_field(event, "detail"))) return false;
        if (!event_binding_valid(event, task_run)) return false;
        if (!append(text, "event time=") || !append_sanitized_field(text, event, "occurred_at", "unknown") || !append(text, " run=") ||
            !append_sanitized_field(text, event, "run_id", "-") || !append(text, " step=") || !append_sanitized_field(text, event, "step_id", "-") ||
            !append(text, " sequence=") || !append_number(text, sequence) || !append(text, " type=") || !append_sanitized_field(text, event, "type", "unknown") ||
            !append(text, " detail=") || !append_sanitized_field(text, event, "detail", "") || !append(text, "\n")) return false;
        previous = sequence;
    }
    return !previous || previous == page->cursor;
}

static bool render_page(struct text_buffer *text, const struct announce_page *page) {
    if (!append(text, "task id=") || !append_sanitized_field(text, page->task, "task_id", "unknown") || !append(text, " state=") ||
        !append_sanitized_field(text, page->task, "execution_state", "unknown") || !append(text, " receiver_observed_at=") ||
        !append_json_value(text, page->data, "receiver_observed_at", "unavailable") || !append(text, " evidence=saved_snapshot\n")) return false;
    if (!page->available && (!append(text, "history unavailable reason=") || !append_sanitized_field(text, page->stream, "unavailable_reason", "unavailable") || !append(text, "\n"))) return false;
    if (page->gap && !append(text, "history retention_gap=true\n")) return false;
    if (page->reset && !append(text, "history stream_reset=true\n")) return false;
    if (page->truncated && !append(text, "history truncated=true\n")) return false;
    if (!render_events(text, page) || !append(text, "resume cursor=") || !append_number(text, page->cursor) || !append(text, " byte_offset=") ||
        !append_number(text, page->offset) || !append(text, " stream_id=") || !(page->stream_id ? append_sanitized_field(text, page->stream, "stream_id", "unavailable") : append(text, "unavailable")) || !append(text, "\n")) return false;
    return true;
}

static bool announce_options(int argc, char **argv, const char **input, bool *plain) {
    const char *format = NULL;
    for (int i = 1; i < argc; i++) {
        const char **destination;
        if (!strcmp(argv[i], "--input")) destination = input;
        else if (!strcmp(argv[i], "--format")) destination = &format;
        else return false;
        if (*destination || ++i == argc || !argv[i][0]) return false;
        *destination = argv[i];
    }
    if (!*input || (format && strcmp(format, "json") && strcmp(format, "text"))) return false;
    *plain = format && !strcmp(format, "text");
    return true;
}

json_object *task_announce_cli(int argc, char **argv) {
    const char *input = NULL; bool plain = false;
    json_object *parsed = NULL; struct announce_page page = {0}; struct text_buffer text = {0};
    if (!announce_options(argc, argv, &input, &plain)) return invalid("announce requires --input FILE [--format json|text]");
    parsed = f_read_json(input, 262144U);
    if (!parsed) return invalid("saved observation is missing, malformed, or oversized");
    if (!page_envelope(parsed, &page) || !page_stream(&page) || !render_page(&text, &page)) { free(text.value); json_object_put(parsed); return invalid("saved observation is invalid or inconsistent"); }
    if (plain) {
        bool written = fwrite(text.value, 1, text.length, stdout) == text.length && fflush(stdout) == 0;
        free(text.value); json_object_put(parsed);
        return written ? NULL : f_error("fleet-task-announce", "io_failed", "cannot write announcement");
    }
    { json_object *result = json_object_new_object(); f_string_add(result, "task_id", f_string(page.task, "task_id")); f_string_add(result, "announcement", text.value ? text.value : ""); free(text.value); json_object_put(parsed); return f_success("fleet-task-announce", result); }
}
