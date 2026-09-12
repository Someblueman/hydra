#include "fleet/review_projection.h"
#include "fleet/attention_tui.h"
#include "fleet/support/json.h"
#include "fleet/support/files.h"
#include "fleet/task/task.h"
#include <fcntl.h>
#include <stdarg.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#define REVIEW_LIMIT (1024U * 1024U)
#define REVIEW_LINE 8192U
#define REVIEW_CHUNK 4096U
struct projection { char *text; size_t used, lines, refs, previews; bool failed; };

static const char *value(json_object *object, const char *key)
{
    const char *text = f_string(object, key);
    return text && *text ? text : "-";
}
static bool safe_field(const char *text, size_t limit)
{
    const unsigned char *p = (const unsigned char *)text;
    if (!text || !*text || strlen(text) > limit) return false;
    for (; *p; p++) if (*p < 32U || *p == 127U) return false;
    return true;
}
static bool selection_valid(json_object *selection)
{
    char revision[65], identity[65];
    return task_hex(f_string(selection, "revision_sha256"), 64U) &&
        task_hex(f_string(selection, "identity_sha256"), 64U) &&
        !f_attention_item_hashes(selection, revision, identity) &&
        !strcmp(identity, f_string(selection, "identity_sha256"));
}
json_object *review_selection_parse(const char *source, int argc, char **argv)
{
    static const char *const fields[] = {"kind", "project_id", "host", "task_id", "run_id", "step_id", "attempt_id", "head_id", "current_instance", "request_id", "binding", "revision_sha256", "identity_sha256"};
    json_object *selection = NULL, *route; size_t i;
    if (argc != 13) return NULL;
    selection = json_object_new_object(); f_string_add(selection, "source", source);
    for (i = 0; i < sizeof(fields) / sizeof(fields[0]); i++) {
        if (!safe_field(argv[i], i < 10U ? 127U : 64U) || (strcmp(argv[i], "-") && !f_name(argv[i]))) goto invalid;
        f_string_add(selection, fields[i], argv[i]);
    }
    if (strcmp(argv[10], "-") && !task_hex(argv[10], 40U) && !task_hex(argv[10], 64U)) goto invalid;
    f_string_add(selection, "reason", "selected"); f_string_add(selection, "revision", "requested"); f_string_add(selection, "freshness", "unknown");
    route = json_object_new_object(); f_string_add(route, "kind", "unavailable");
    json_object_object_add(route, "navigable", json_object_new_boolean(false)); json_object_object_add(selection, "route", route);
    if (selection_valid(selection)) return selection;
invalid:
    json_object_put(selection); return NULL;
}
static void append(struct projection *out, const char *format, ...)
{
    va_list args; int count;
    if (out->failed) return;
    va_start(args, format);
    count = vsnprintf(out->text + out->used, REVIEW_LIMIT - out->used, format, args);
    va_end(args);
    if (count < 0 || (size_t)count >= REVIEW_LIMIT - out->used) out->failed = true;
    else out->used += (size_t)count;
}
static bool header(struct projection *out, json_object *selection)
{
    static const char *const fields[] = {"source", "project_id", "host", "task_id", "run_id", "step_id", "attempt_id", "head_id", "current_instance", "request_id"};
    const char *binding = f_string(selection, "binding"); size_t i;
    if (!binding || !*binding) binding = value(selection, "spec_sha256");
    if (!safe_field(binding, 64U) || !task_hex(f_string(selection, "revision_sha256"), 64U) ||
        !task_hex(f_string(selection, "identity_sha256"), 64U)) return false;
    append(out, "HYDRA_REVIEW\t1");
    for (i = 0; i < sizeof(fields) / sizeof(fields[0]); i++) {
        const char *text = value(selection, fields[i]);
        if (!safe_field(text, 127U)) return false;
        append(out, "\t%s", text);
    }
    append(out, "\t%s\t%s\t%s\n", binding, value(selection, "revision_sha256"), value(selection, "identity_sha256"));
    return true;
}
static void human_line(struct projection *out, const char *tag, const char *prefix, const char *line)
{
    append(out, "%s\t%s%s\n", tag, prefix, line);
    if (!strcmp(tag, "TEXT")) out->lines++; else out->previews++;
}
static void human_lines(struct projection *out, const char *tag, const char *prefix, const char *text)
{
    char line[REVIEW_CHUNK + 1U]; size_t n = 0U;
    const unsigned char *p = (const unsigned char *)text;
    do {
        unsigned char c = *p;
        if (!c || c == '\n' || n == REVIEW_CHUNK) {
            line[n] = '\0'; human_line(out, tag, prefix, line);
            n = 0U;
            if (!c) break;
            if (c == '\n') { p++; continue; }
        }
        line[n++] = (c < 32U || c == 127U) ? '?' : (char)c;
        p++;
    } while (!out->failed);
}
static void human_value(struct projection *out, const char *prefix, json_object *object, unsigned depth);
static void human_object(struct projection *out, const char *prefix, json_object *object, unsigned depth)
{
    json_object_object_foreach(object, key, child) {
        char label[1024];
        if (!safe_field(key, 255U) || snprintf(label, sizeof(label), "%s%s: ", prefix, key) >= (int)sizeof(label)) { out->failed = true; return; }
        human_value(out, label, child, depth + 1U);
    }
}
static void human_value(struct projection *out, const char *prefix, json_object *object, unsigned depth)
{
    size_t i;
    if (depth > 12U) { out->failed = true; return; }
    if (json_object_is_type(object, json_type_object)) { human_object(out, prefix, object, depth); return; }
    if (json_object_is_type(object, json_type_array)) {
        for (i = 0; i < json_object_array_length(object); i++) {
            char label[1024];
            if (snprintf(label, sizeof(label), "%s[%zu] ", prefix, i) >= (int)sizeof(label)) { out->failed = true; return; }
            human_value(out, label, json_object_array_get_idx(object, i), depth + 1U);
        }
        return;
    }
    const char *text = json_object_is_type(object, json_type_string) ? f_text(object) : json_object_to_json_string_ext(object, JSON_C_TO_STRING_PLAIN);
    if (!text) { out->failed = true; return; }
    human_lines(out, "TEXT", prefix, text);
}
static bool reference(struct projection *out, json_object *row, size_t index)
{
    const char *kind = value(row, "kind"), *state = value(row, "state"), *locator = value(row, "locator");
    if ((strcmp(kind, "transcript") && strcmp(kind, "log") && strcmp(kind, "pr")) ||
        (strcmp(state, "available") && strcmp(state, "inaccessible") && strcmp(state, "supplied-unopened")) || !safe_field(locator, 4095U)) return false;
    append(out, "REF\t%s\t%s\t%s\n", kind, state, locator); out->refs++;
    if (f_field(row, "preview")) {
        char prefix[32]; const char *preview = f_string(row, "preview");
        if (!preview || strlen(preview) > REVIEW_CHUNK) return false;
        (void)snprintf(prefix, sizeof(prefix), "%zu\t", index);
        human_lines(out, "PREVIEW", prefix, preview);
    }
    return true;
}
static bool references(struct projection *out, json_object *rows)
{
    size_t i;
    if (!rows) return true;
    if (!json_object_is_type(rows, json_type_array) || json_object_array_length(rows) > 16U) return false;
    for (i = 0; i < json_object_array_length(rows); i++) {
        if (!reference(out, json_object_array_get_idx(rows, i), i)) return false;
    }
    return true;
}
static bool status_field(const char *parent, const char *key)
{
    if (!*parent) return !strcmp(key, "readiness") || !strcmp(key, "accepted");
    if (!strcmp(parent, "checks")) return !strcmp(key, "state");
    return !strcmp(parent, "request") && (!strcmp(key, "state") || !strcmp(key, "message"));
}
static void status_line(struct projection *out, json_object *data, const char *parent, const char *key)
{
    json_object *object = *parent ? f_field(data, parent) : data, *field;
    char label[64];
    if (!json_object_object_get_ex(object, key, &field)) return;
    (void)snprintf(label, sizeof(label), "%s%s%s: ", parent, *parent ? ": " : "", key);
    human_value(out, label, field, *parent ? 1U : 0U);
}
static void body_object(struct projection *out, const char *key, json_object *field)
{
    char label[1024];
    json_object_object_foreach(field, child_key, child) {
        if (status_field(key, child_key)) continue;
        if (!safe_field(child_key, 255U)) { out->failed = true; return; }
        (void)snprintf(label, sizeof(label), "%s: %s: ", key, child_key); human_value(out, label, child, 1U);
    }
}
static void body_field(struct projection *out, const char *key, json_object *field)
{
    if ((!strcmp(key, "checks") || !strcmp(key, "request")) && json_object_is_type(field, json_type_object)) {
        body_object(out, key, field);
    } else {
        char label[260];
        (void)snprintf(label, sizeof(label), "%s: ", key); human_value(out, label, field, 0U);
    }
}
static void body(struct projection *out, json_object *data)
{
    if (!json_object_is_type(data, json_type_object)) { out->failed = true; return; }
    /* Present existing computed state before long identities and evidence.
     * Each field is emitted once; the formatter never derives readiness. */
    status_line(out, data, "", "readiness"); status_line(out, data, "", "accepted");
    status_line(out, data, "checks", "state"); status_line(out, data, "request", "state");
    status_line(out, data, "request", "message");
    json_object_object_foreach(data, key, child) {
        if (!strcmp(key, "references") || !strcmp(key, "attention") || !strcmp(key, "selection")) continue;
        if (status_field("", key)) continue;
        if (!safe_field(key, 255U)) { out->failed = true; return; }
        body_field(out, key, child);
    }
}
int review_projection(json_object *envelope, json_object *selection)
{
    struct projection out = {0}; json_object *data = f_field(envelope, "data"); int status = 1;
    if (!f_number_is(envelope, "schema_version", 1) || !json_object_is_type(f_field(envelope, "ok"), json_type_boolean)) return 1;
    if (!json_object_get_boolean(f_field(envelope, "ok")) && !json_object_is_type(f_field(envelope, "error"), json_type_object)) return 1;
    if (json_object_get_boolean(f_field(envelope, "ok")) &&
        (!json_object_is_type(f_field(data, "accepted"), json_type_boolean) || json_object_get_boolean(f_field(data, "accepted")))) return 1;
    out.text = malloc(REVIEW_LIMIT);
    if (!out.text || !header(&out, selection)) goto cleanup;
    if (!json_object_get_boolean(f_field(envelope, "ok"))) human_value(&out, "error: ", f_field(envelope, "error"), 0U);
    else body(&out, data);
    if (!references(&out, f_field(data, "references"))) goto cleanup;
    append(&out, "END\t%zu\t%zu\t%zu\n", out.lines, out.refs, out.previews);
    if (!out.failed && fwrite(out.text, 1U, out.used, stdout) == out.used && !ferror(stdout)) status = 0;
cleanup:
    free(out.text); return status;
}
static void local_preview(json_object *row, const char *path)
{
    int fd = open(path, O_RDONLY | O_NONBLOCK | O_NOFOLLOW); struct stat st;
    char bytes[REVIEW_CHUNK + 1U]; ssize_t n; size_t i;
    f_string_add(row, "state", "inaccessible");
    if (fd < 0) return;
    if (fstat(fd, &st) || !S_ISREG(st.st_mode)) goto cleanup;
    n = read(fd, bytes, REVIEW_CHUNK);
    if (n < 0) goto cleanup;
    for (i = 0; i < (size_t)n; i++) {
        unsigned char c = (unsigned char)bytes[i];
        if ((c < 32U && c != '\n' && c != '\t') || c >= 127U) bytes[i] = '?';
    }
    bytes[n] = '\0'; f_string_add(row, "state", "available"); f_string_add(row, "preview", bytes);
    f_string_add(row, "preview_encoding", "ASCII preview; other bytes replaced");
    json_object_object_add(row, "truncated", json_object_new_boolean(st.st_size > n));
cleanup:
    close(fd);
}
json_object *review_references(json_object *input)
{
    json_object *rows = json_object_new_array(); size_t i;
    if (!input) return rows;
    if (!json_object_is_type(input, json_type_array) || json_object_array_length(input) > 16U) goto invalid;
    for (i = 0; i < json_object_array_length(input); i++) {
        json_object *item = json_object_array_get_idx(input, i), *row;
        const char *kind = f_string(item, "kind"), *locator = f_string(item, "locator");
        if (!kind || (strcmp(kind, "transcript") && strcmp(kind, "log") && strcmp(kind, "pr")) || !safe_field(locator, 4095U)) goto invalid;
        if (locator[0] != '/' && strncmp(locator, "https://", 8U) && strncmp(locator, "http://", 7U)) goto invalid;
        row = json_object_new_object(); f_string_add(row, "kind", kind); f_string_add(row, "locator", locator);
        if (locator[0] == '/') local_preview(row, locator); else f_string_add(row, "state", "supplied-unopened");
        json_object_array_add(rows, row);
    }
    return rows;
invalid:
    json_object_put(rows); return NULL;
}
