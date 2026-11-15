#define _POSIX_C_SOURCE 200809L
#ifdef __APPLE__
#define _DARWIN_C_SOURCE
#endif
#include "fleet/attention_tui.h"
#include "fleet/support/files.h"
#include "fleet/support/json.h"
#include "fleet/support/process.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#define ATTENTION_LIMIT 512U
#define ATTENTION_MAX_FIELD 127U
#define ATTENTION_HASH 65U
#define ATTENTION_DETAIL 256U

static const char *value(json_object *object, const char *key)
{
    const char *text = f_string(object, key);
    return text && *text ? text : "-";
}
static const char *item_binding(json_object *item)
{
    const char *binding = f_string(item, "binding");
    if (!binding || !*binding) binding = f_string(item, "spec_sha256");
    return binding && *binding ? binding : "-";
}
static bool bounded(const char *text, size_t maximum)
{
    const unsigned char *cursor = (const unsigned char *)(text ? text : "");
    size_t length = strlen((const char *)cursor);
    if (!length || length > maximum || strchr((const char *)cursor, '\t')) return false;
    for (; *cursor; cursor++) if (*cursor < 0x20U || *cursor == 0x7fU) return false;
    return true;
}
static bool one_of(const char *text, const char *const choices[], size_t count)
{
    size_t i;
    for (i = 0U; i < count; i++) if (!strcmp(text, choices[i])) return true;
    return false;
}
static bool valid_route(json_object *route)
{
    static const char *const routes[] = {"workflow-request", "workflow-evidence", "task-observe", "task-result", "agent-record", "unavailable"};
    return json_object_is_type(route, json_type_object) &&
        bounded(value(route, "kind"), ATTENTION_MAX_FIELD) &&
        one_of(value(route, "kind"), routes, 6U) &&
        json_object_is_type(f_field(route, "navigable"), json_type_boolean);
}
static bool valid_item(json_object *item)
{
    static const char *const freshness[] = {"fresh", "stale", "unknown", "expired"};
    static const char *const kinds[] = {"approval", "result", "permission", "unknown", "approval_expired", "failure"};
    const char *fields[] = {value(item, "source"), value(item, "kind"), value(item, "reason"), value(item, "project_id"), value(item, "host"), value(item, "task_id"), value(item, "run_id"), value(item, "step_id"), value(item, "attempt_id"), value(item, "head_id"), value(item, "current_instance"), value(item, "request_id"), item_binding(item)};
    json_object *route = f_field(item, "route");
    size_t i;
    for (i = 0U; i < 13U; i++) {
        size_t maximum = i == 2U ? 255U : i == 12U ? 64U : ATTENTION_MAX_FIELD;
        if (!bounded(fields[i], maximum)) return false;
    }
    if (!bounded(value(item, "revision"), F_LIMIT) || !bounded(value(item, "freshness"), 32U)) return false;
    return one_of(value(item, "freshness"), freshness, 4U) && one_of(value(item, "kind"), kinds, 6U) && valid_route(route);
}
static void output_field(const char *text)
{
    const unsigned char *cursor = (const unsigned char *)(text ? text : "-");
    for (; *cursor; cursor++) putchar((int)*cursor);
}
static void identity_text(json_object *item, char *output, size_t capacity)
{
    (void)snprintf(output, capacity, "%s\t%s\t%s\t%s\t%s\t%s\t%s\t%s\t%s\t%s\t%s\t%s", value(item, "source"), value(item, "kind"), value(item, "project_id"), value(item, "host"), value(item, "task_id"), value(item, "run_id"), value(item, "step_id"), value(item, "attempt_id"), value(item, "head_id"), value(item, "current_instance"), value(item, "request_id"), item_binding(item));
}
/* The revision and identity hashes are SHA-256 digests of the revision text
 * and of the tab-separated identity fields. */
static void item_hashes(json_object *item, char revision[ATTENTION_HASH], char identity[ATTENTION_HASH])
{
    char text[4096];
    const char *recorded = value(item, "revision");
    identity_text(item, text, sizeof(text));
    f_sha256_text(recorded, strlen(recorded), revision);
    f_sha256_text(text, strlen(text), identity);
}
static bool valid_items(json_object *array, json_object **items, size_t *count)
{
    size_t i, length = json_object_array_length(array);
    if (length > ATTENTION_LIMIT) return false;
    for (i = 0U; i < length; i++) {
        items[*count] = json_object_array_get_idx(array, i);
        if (!json_object_is_type(items[*count], json_type_object) || !valid_item(items[*count])) return false;
        (*count)++;
    }
    return true;
}
int f_attention_item_hashes(json_object *item, char revision[65], char identity[65])
{
    if (!valid_item(item)) return 1;
    item_hashes(item, revision, identity);
    return 0;
}
/* Version 2 appended one presentation label (the workflow or task name). It is
 * never part of the identity or revision; an unusable label is sent as "-". */
static const char *item_label(json_object *item)
{
    const char *label = f_string(item, "label");
    return label && bounded(label, ATTENTION_MAX_FIELD) ? label : "-";
}
/* Version 3 appends one presentation detail: the requirement IDs a failed
 * check decides, comma separated and bounded, or "-". Like the label it is
 * outside the identity and the revision hash. */
static bool detail_append(char detail[ATTENTION_DETAIL], size_t *used, const char *id)
{
    size_t length = id ? strlen(id) : 0U;
    if (!length || !bounded(id, ATTENTION_MAX_FIELD)) return true;
    if (*used + length + 2U >= ATTENTION_DETAIL) return false;
    *used += (size_t)snprintf(detail + *used, ATTENTION_DETAIL - *used, "%s%s", *used ? ", " : "", id);
    return true;
}
static void item_detail(json_object *item, char detail[ATTENTION_DETAIL])
{
    json_object *requirements = f_field(item, "requirements");
    size_t i, used = 0U, count = json_object_is_type(requirements, json_type_array) ? json_object_array_length(requirements) : 0U;
    detail[0] = '\0';
    for (i = 0U; i < count; i++)
        if (!detail_append(detail, &used, json_object_get_string(json_object_array_get_idx(requirements, i)))) break;
    if (!used) (void)snprintf(detail, ATTENTION_DETAIL, "-");
}
static void output_item(json_object *item, const char *revision, const char *identity)
{
    char detail[ATTENTION_DETAIL];
    json_object *route = f_field(item, "route");
    printf("ITEM\t"); output_field(value(item, "source")); printf("\t"); output_field(value(item, "kind")); printf("\t"); output_field(value(item, "reason")); printf("\t"); output_field(value(item, "project_id")); printf("\t"); output_field(value(item, "host")); printf("\t"); output_field(value(item, "task_id")); printf("\t"); output_field(value(item, "run_id")); printf("\t"); output_field(value(item, "step_id")); printf("\t"); output_field(value(item, "attempt_id")); printf("\t"); output_field(value(item, "head_id")); printf("\t"); output_field(value(item, "current_instance")); printf("\t"); output_field(value(item, "request_id")); printf("\t"); output_field(item_binding(item)); printf("\t%s\t%s\t", revision, identity); output_field(value(item, "freshness")); printf("\t"); output_field(value(route, "kind")); printf("\t%d\t", json_object_get_boolean(f_field(route, "navigable")) ? 1 : 0); output_field(item_label(item)); item_detail(item, detail); printf("\t%s\n", detail);
}
int f_attention_tui_data(json_object *aggregate)
{
    json_object *data = f_field(aggregate, "data");
    json_object *array = f_field(data, "items");
    json_object *partial_value = f_field(data, "partial");
    json_object *truncated_value = f_field(data, "truncated");
    json_object *items[ATTENTION_LIMIT];
    char revision[ATTENTION_HASH], identity[ATTENTION_HASH];
    size_t count = 0U, i;
    bool partial, truncated;
    if (!json_object_is_type(array, json_type_array) ||
        !json_object_is_type(partial_value, json_type_boolean) ||
        !json_object_is_type(truncated_value, json_type_boolean) ||
        !valid_items(array, items, &count)) return 1;
    truncated = json_object_get_boolean(truncated_value);
    partial = json_object_get_boolean(partial_value) || truncated;
    puts("HYDRA_ATTENTION\t3");
    for (i = 0U; i < count; i++) {
        item_hashes(items[i], revision, identity);
        output_item(items[i], revision, identity);
    }
    printf("END\t%zu\t%d\t%d\n", count, partial ? 1 : 0, truncated ? 1 : 0);
    return ferror(stdout) ? 1 : 0;
}
