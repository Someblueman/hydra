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
    static const char *const kinds[] = {"approval", "result", "permission", "unknown", "approval_expired"};
    const char *fields[] = {value(item, "source"), value(item, "kind"), value(item, "reason"), value(item, "project_id"), value(item, "host"), value(item, "task_id"), value(item, "run_id"), value(item, "step_id"), value(item, "attempt_id"), value(item, "head_id"), value(item, "current_instance"), value(item, "request_id"), item_binding(item)};
    json_object *route = f_field(item, "route");
    size_t i;
    for (i = 0U; i < 13U; i++) {
        size_t maximum = i == 2U ? 255U : i == 12U ? 64U : ATTENTION_MAX_FIELD;
        if (!bounded(fields[i], maximum)) return false;
    }
    if (!bounded(value(item, "revision"), F_LIMIT) || !bounded(value(item, "freshness"), 32U)) return false;
    return one_of(value(item, "freshness"), freshness, 4U) && one_of(value(item, "kind"), kinds, 5U) && valid_route(route);
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
static bool hash_line(char *line, const char *path, char output[ATTENTION_HASH])
{
    char *space = strchr(line, ' ');
    size_t i;
    if (!space || (size_t)(space - line) != 64U) return false;
    for (i = 0U; i < 64U; i++) if (!((line[i] >= '0' && line[i] <= '9') || (line[i] >= 'a' && line[i] <= 'f'))) return false;
    while (*++space == ' ') { }
    if (strcmp(space, path)) return false;
    memcpy(output, line, 64U); output[64] = '\0';
    return true;
}
static bool collect_hashes(char *output, char **paths, size_t path_count,
                           char revisions[][ATTENTION_HASH], char identities[][ATTENTION_HASH], size_t item_count)
{
    char *save = NULL;
    char *line = strtok_r(output, "\n", &save);
    size_t i;
    for (i = 0U; i < path_count && line; i++, line = strtok_r(NULL, "\n", &save)) {
        char hash[ATTENTION_HASH];
        if (!hash_line(line, paths[i], hash)) return false;
        if (i < item_count) memcpy(revisions[i], hash, ATTENTION_HASH);
        else memcpy(identities[i - item_count], hash, ATTENTION_HASH);
    }
    return i == path_count && line == NULL;
}
static int run_hash_command(char **paths, size_t path_count, struct f_capture *capture)
{
    char *argv[ATTENTION_LIMIT * 2U + 4U];
    size_t i, argc = 0U;
    argv[argc++] = (char *)"shasum"; argv[argc++] = (char *)"-a"; argv[argc++] = (char *)"256";
    for (i = 0U; i < path_count; i++) argv[argc++] = paths[i];
    argv[argc] = NULL;
    if (f_run(argv, NULL, 0U, 30U, capture) == 0 && capture->status == 0) return 0;
    if (capture->status != 127) return -1;
    f_capture_free(capture); argc = 0U; argv[argc++] = (char *)"sha256sum";
    for (i = 0U; i < path_count; i++) argv[argc++] = paths[i];
    argv[argc] = NULL;
    return f_run(argv, NULL, 0U, 30U, capture) == 0 && capture->status == 0 ? 0 : -1;
}
static void cleanup_hash_files(char **paths, size_t count)
{
    size_t i;
    for (i = 0U; i < count; i++) {
        if (paths[i]) { unlink(paths[i]); free(paths[i]); }
    }
}
static bool write_item_files(const char *dir, json_object *item, size_t index,
                             char **revision_path, char **identity_path)
{
    char path[4096], identity[4096];
    identity_text(item, identity, sizeof(identity));
    (void)snprintf(path, sizeof(path), "%s/r%04zu", dir, index);
    if (f_write(path, value(item, "revision"), strlen(value(item, "revision")), true)) return false;
    *revision_path = strdup(path);
    (void)snprintf(path, sizeof(path), "%s/i%04zu", dir, index);
    if (f_write(path, identity, strlen(identity), true)) return false;
    *identity_path = strdup(path);
    return *revision_path && *identity_path;
}
static int write_hash_files(const char *dir, json_object **items, size_t count,
                            char revisions[][ATTENTION_HASH], char identities[][ATTENTION_HASH])
{
    char **paths = calloc(count * 2U + 1U, sizeof(*paths));
    struct f_capture capture = {0};
    size_t i;
    int result = -1;
    if (!paths) return -1;
    for (i = 0U; i < count; i++) {
        if (!write_item_files(dir, items[i], i, &paths[i], &paths[count + i])) goto cleanup;
    }
    if (run_hash_command(paths, count * 2U, &capture) || !capture.out) goto cleanup;
    result = collect_hashes(capture.out, paths, count * 2U, revisions, identities, count) ? 0 : -1;
cleanup:
    f_capture_free(&capture); cleanup_hash_files(paths, count * 2U); free(paths);
    return result;
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
    char dir[] = "/tmp/hydra-attention-item-XXXXXX";
    char revisions[1][ATTENTION_HASH], identities[1][ATTENTION_HASH];
    json_object *items[] = {item};
    int status;
    if (!valid_item(item) || !mkdtemp(dir)) return 1;
    status = write_hash_files(dir, items, 1U, revisions, identities);
    rmdir(dir);
    if (status) return 1;
    memcpy(revision, revisions[0], ATTENTION_HASH);
    memcpy(identity, identities[0], ATTENTION_HASH);
    return 0;
}
static void output_item(json_object *item, const char *revision, const char *identity)
{
    json_object *route = f_field(item, "route");
    printf("ITEM\t"); output_field(value(item, "source")); printf("\t"); output_field(value(item, "kind")); printf("\t"); output_field(value(item, "reason")); printf("\t"); output_field(value(item, "project_id")); printf("\t"); output_field(value(item, "host")); printf("\t"); output_field(value(item, "task_id")); printf("\t"); output_field(value(item, "run_id")); printf("\t"); output_field(value(item, "step_id")); printf("\t"); output_field(value(item, "attempt_id")); printf("\t"); output_field(value(item, "head_id")); printf("\t"); output_field(value(item, "current_instance")); printf("\t"); output_field(value(item, "request_id")); printf("\t"); output_field(item_binding(item)); printf("\t%s\t%s\t", revision, identity); output_field(value(item, "freshness")); printf("\t"); output_field(value(route, "kind")); printf("\t%d\n", json_object_get_boolean(f_field(route, "navigable")) ? 1 : 0);
}
int f_attention_tui_data(json_object *aggregate)
{
    json_object *data = f_field(aggregate, "data");
    json_object *array = f_field(data, "items");
    json_object *partial_value = f_field(data, "partial");
    json_object *truncated_value = f_field(data, "truncated");
    json_object *items[ATTENTION_LIMIT];
    char revisions[ATTENTION_LIMIT][ATTENTION_HASH], identities[ATTENTION_LIMIT][ATTENTION_HASH];
    char dir[] = "/tmp/hydra-attention-batch-XXXXXX";
    size_t count = 0U, i;
    bool partial, truncated;
    if (!json_object_is_type(array, json_type_array) ||
        !json_object_is_type(partial_value, json_type_boolean) ||
        !json_object_is_type(truncated_value, json_type_boolean) ||
        !valid_items(array, items, &count)) return 1;
    truncated = json_object_get_boolean(truncated_value);
    partial = json_object_get_boolean(partial_value) || truncated;
    if (count && !mkdtemp(dir)) return 1;
    if (count && write_hash_files(dir, items, count, revisions, identities)) { rmdir(dir); return 1; }
    puts("HYDRA_ATTENTION\t1");
    for (i = 0U; i < count; i++) output_item(items[i], revisions[i], identities[i]);
    printf("END\t%zu\t%d\t%d\n", count, partial ? 1 : 0, truncated ? 1 : 0);
    if (count) rmdir(dir);
    return ferror(stdout) ? 1 : 0;
}
