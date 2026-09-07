#include "agent.h"
#include <math.h>
#include <string.h>

static json_object *usage(json_object *source, const char *input, const char *output, const char *cached) {
    json_object *result = json_object_new_object();
    const char *names[] = {"input_tokens", "output_tokens", "cached_input_tokens"};
    const char *keys[] = {input, output, cached}; size_t i;
    for (i = 0; i < 3; i++) {
        json_object *value = keys[i] ? f_field(source, keys[i]) : NULL;
        if (value && (!json_object_is_type(value, json_type_int) || json_object_get_int64(value) < 0)) { json_object_put(result); return NULL; }
        json_object_object_add(result, names[i], json_object_get(value));
    }
    json_object_object_add(result, "cost_usd", NULL);
    return result;
}
static const char *content_text(json_object *content) {
    size_t i; const char *text = NULL;
    if (!json_object_is_type(content, json_type_array)) return NULL;
    /* A single final text block is exact. Multiple blocks need explicit joining
     * in a provider adapter; never silently choose one part of an answer. */
    for (i = 0; i < json_object_array_length(content); i++) {
        json_object *part = json_object_array_get_idx(content, i); const char *type = f_string(part, "type");
        if (type && !strcmp(type, "text")) { if (text) return NULL; text = f_string(part, "text"); }
    }
    return text;
}
static int canonical(json_object *input, struct agent_event *event) {
    const char *const keys[] = {"schema_version", "type", "status", "session_id", "text", "request_id", "usage", NULL};
    const char *type = f_string(input, "type");
    if (!task_keys(input, keys) || !f_number_is(input, "schema_version", 1) || !type) return -1;
    if (!strcmp(type, "session")) { event->kind = "session"; event->session = f_string(input, "session_id"); return event->session ? 1 : -1; }
    if (!strcmp(type, "observation")) {
        event->kind = "observation"; event->status = f_string(input, "status");
        return event->status && (!strcmp(event->status, "running") || !strcmp(event->status, "idle") || !strcmp(event->status, "failed")) ? 1 : -1;
    }
    if (!strcmp(type, "result")) { event->kind = "result"; event->text = f_string(input, "text"); return event->text ? 1 : -1; }
    if (!strcmp(type, "permission")) { event->kind = "permission"; event->permission = f_string(input, "request_id"); return f_name(event->permission) ? 1 : -1; }
    if (!strcmp(type, "usage")) {
        const char *const fields[] = {"input_tokens", "output_tokens", "cached_input_tokens", "cost_usd", NULL};
        json_object *source = f_field(input, "usage"), *cost = f_field(source, "cost_usd");
        if (!task_keys(source, fields)) return -1;
        event->kind = "usage"; event->usage = usage(source, "input_tokens", "output_tokens", "cached_input_tokens");
        if (!event->usage) return -1;
        if (cost) {
            double value = json_object_get_double(cost);
            if ((!json_object_is_type(cost, json_type_double) && !json_object_is_type(cost, json_type_int)) || !isfinite(value) || value < 0) { json_object_put(event->usage); event->usage = NULL; return -1; }
            json_object_object_add(event->usage, "cost_usd", json_object_get(cost));
        }
        return 1;
    }
    return -1;
}
static int agy(json_object *input, struct agent_event *event) {
    const char *type = f_string(input, "event");
    if (!type) return -1;
    if (!strcmp(type, "init")) {
        event->kind = "session"; event->status = "running";
        event->session = f_string(input, "conversation_id");
        return event->session && *event->session ? 1 : -1;
    }
    if (!strcmp(type, "result")) {
        json_object *result = f_field(input, "result");
        const char *status = f_string(result, "status"), *session = f_string(result, "conversation_id");
        if (!status || (strcmp(status, "SUCCESS") && strcmp(status, "ERROR") && strcmp(status, "CANCELED") &&
            strcmp(status, "INTERRUPTED") && strcmp(status, "INVALID") && strcmp(status, "WAITING") && strcmp(status, "RUNNING"))) return -1;
        event->kind = "result"; event->status = !strcmp(status, "SUCCESS") ? "idle" : "failed";
        event->text = f_string(result, "response"); event->session = session && *session ? session : NULL;
        if (!strcmp(status, "SUCCESS") && (!event->text || !event->session)) return -1;
        /* Only the terminal aggregate is counted; step deltas are not results. */
        event->usage = usage(f_field(result, "usage"), "input_tokens", "output_tokens", "cache_read_tokens");
        return event->usage ? 1 : -1;
    }
    return 0;
}
static int cursor(json_object *input, const char *type, struct agent_event *event) {
    if (!strcmp(type, "system")) {
        const char *subtype = f_string(input, "subtype");
        if (subtype && !strcmp(subtype, "init")) {
            event->kind = "session"; event->status = "running";
            event->session = f_string(input, "session_id");
            return event->session && *event->session ? 1 : -1;
        }
    }
    if (!strcmp(type, "result")) {
        json_object *error = f_field(input, "is_error");
        if (!json_object_is_type(error, json_type_boolean)) return -1;
        event->kind = "result"; event->status = json_object_get_boolean(error) ? "failed" : "idle";
        event->text = f_string(input, "result"); event->session = f_string(input, "session_id");
        if (event->session && !*event->session) event->session = NULL;
        if (!json_object_get_boolean(error) && (!event->text || !event->session || !*event->session)) return -1;
        /* Cursor's documented result has no usage counters. Keep them unknown. */
        return 1;
    }
    if (!strcmp(type, "error")) { event->kind = "observation"; event->status = "failed"; return 1; }
    return 0;
}
int agent_decode(const char *adapter, json_object *input, struct agent_event *event) {
    const char *type = f_string(input, "type");
    memset(event, 0, sizeof(*event));
    if (!json_object_is_type(input, json_type_object)) return -1;
    if (!strcmp(adapter, "agy-jsonl")) return agy(input, event);
    if (!type) return -1;
    if (!strcmp(adapter, "cursor-jsonl")) return cursor(input, type, event);
    if (!strcmp(adapter, "canonical-jsonl")) return canonical(input, event);
    if (!strcmp(adapter, "codex-jsonl")) {
        if (!strcmp(type, "thread.started")) { event->kind = "session"; event->session = f_string(input, "thread_id"); return event->session ? 1 : -1; }
        if (!strcmp(type, "turn.started")) { event->kind = "observation"; event->status = "running"; return 1; }
        if (!strcmp(type, "turn.completed")) {
            event->kind = "usage"; event->status = "idle"; event->usage = usage(f_field(input, "usage"), "input_tokens", "output_tokens", "cached_input_tokens"); return event->usage ? 1 : -1;
        }
        if (!strcmp(type, "turn.failed") || !strcmp(type, "error")) { event->kind = "observation"; event->status = "failed"; return 1; }
        if (!strcmp(type, "item.completed")) {
            json_object *item = f_field(input, "item"); const char *kind = f_string(item, "type");
            if (kind && !strcmp(kind, "agent_message")) { event->kind = "result"; event->text = f_string(item, "text"); return event->text ? 1 : -1; }
        }
        return 0;
    }
    if (!strcmp(adapter, "claude-jsonl")) {
        if (!strcmp(type, "system")) {
            const char *subtype = f_string(input, "subtype");
            if (subtype && !strcmp(subtype, "init")) { event->kind = "session"; event->session = f_string(input, "session_id"); return event->session ? 1 : -1; }
        }
        if (!strcmp(type, "result")) {
            json_object *error = f_field(input, "is_error");
            event->kind = "result"; event->text = f_string(input, "result");
            if (!json_object_is_type(error, json_type_boolean) ||
                (!json_object_get_boolean(error) && !event->text)) return -1;
            event->status = json_object_get_boolean(error) ? "failed" : "idle";
            event->usage = usage(f_field(input, "usage"), "input_tokens", "output_tokens", "cache_read_input_tokens");
            return event->usage ? 1 : -1;
        }
        return 0;
    }
    if (!strcmp(adapter, "pi-jsonl")) {
        if (!strcmp(type, "session")) { event->kind = "session"; event->session = f_string(input, "id"); return event->session ? 1 : -1; }
        if (!strcmp(type, "agent_start")) { event->kind = "observation"; event->status = "running"; return 1; }
        if (!strcmp(type, "agent_end")) { event->kind = "observation"; event->status = "idle"; return 1; }
        if (!strcmp(type, "message_end")) {
            json_object *message = f_field(input, "message"); const char *role = f_string(message, "role");
            if (role && !strcmp(role, "assistant")) {
                const char *reason = f_string(message, "stopReason");
                event->kind = "result"; event->text = content_text(f_field(message, "content"));
                event->status = reason && (!strcmp(reason, "error") || !strcmp(reason, "aborted")) ? "failed" : NULL;
                event->usage = usage(f_field(message, "usage"), "input", "output", "cacheRead"); return event->usage ? 1 : -1;
            }
        }
        return 0;
    }
    if (!strcmp(adapter, "opencode-jsonl")) {
        event->session = f_string(input, "sessionID");
        if (!strcmp(type, "step_start")) { event->kind = "observation"; event->status = "running"; return 1; }
        if (!strcmp(type, "text")) { event->kind = "result"; event->text = f_string(f_field(input, "part"), "text"); return event->text ? 1 : -1; }
        if (!strcmp(type, "step_finish")) {
            event->kind = "usage"; event->status = "idle"; event->usage = usage(f_field(f_field(input, "part"), "tokens"), "input", "output", NULL); return event->usage ? 1 : -1;
        }
        if (!strcmp(type, "error")) { event->kind = "observation"; event->status = "failed"; return 1; }
        return event->session ? 1 : 0;
    }
    return -1;
}
