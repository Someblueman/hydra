#include "fixture.h"
#include <glob.h>

static long long integer(json_object *object, const char *field) {
    json_object *value = fx_field(object, field);
    fx_require(json_object_is_type(value, json_type_int), field);
    return json_object_get_int64(value);
}
static int equal(json_object *value, const char *literal) {
    json_object *expected = f_parse_value(literal);
    fx_require(expected != NULL, "expected literal");
    int matches = json_object_equal(value, expected);
    json_object_put(expected);
    return matches;
}
static void observed_task(const char *path) {
    json_object *document = fx_read(path), *task = fx_field(fx_field(document, "data"), "task");
    json_object *attempts = fx_field(task, "attempt_history");
    fx_require(json_object_array_length(attempts) > 0, "attempt history");
    size_t work = 0;
    for (size_t i = 0; i < json_object_array_length(attempts); i++) {
        json_object *attempt = json_object_array_get_idx(attempts, i);
        fx_require(equal(fx_field(attempt, "retention"), "\"retained\"") &&
                       f_field(attempt, "process_exit"),
                   "retained exit");
        if (!equal(fx_field(attempt, "step_id"), "\"work\""))
            continue;
        fx_require(work < 2, "work attempt count");
        fx_require(
            equal(fx_field(attempt, "attempt_id"), work ? "\"attempt-2\"" : "\"attempt-1\"") &&
                equal(fx_field(attempt, "state"), work ? "\"succeeded\"" : "\"failed\"") &&
                equal(fx_field(attempt, "process_exit"), work ? "\"0\"" : "\"7\""),
            "ordered retry attempts");
        const char *completed = f_string(attempt, "completed_at");
        fx_require(completed && *completed, "completion time");
        work++;
    }
    fx_require(work == 2, "both work attempts");
    fx_require(equal(fx_field(task, "process_exit"), "{\"state\":\"succeeded\",\"exit_status\":0}"),
               "process result");
    fx_require(equal(fx_field(fx_field(task, "result_collection"), "state"), "\"ready\""),
               "collection ready");
    fx_require(json_object_array_length(fx_field(task, "artifact_inventory")) > 0, "artifacts");
    fx_require(
        equal(fx_field(task, "verification"),
              "{\"kind\":\"integrity\",\"state\":\"recorded\",\"recheck\":\"not_rechecked\"}"),
        "verification distinction");
    json_object_put(document);
}
static long long sequence(json_object *page, long long cursor) {
    json_object *events = fx_field(page, "events");
    fx_require(json_object_is_type(events, json_type_array), "event array");
    for (size_t i = 0; i < json_object_array_length(events); i++)
        fx_require(integer(json_object_array_get_idx(events, i), "sequence") == ++cursor,
                   "contiguous event sequence");
    return cursor;
}
static void pages(const char *first, const char *second) {
    const char *paths[] = {first, second};
    long long cursor = 0;
    for (size_t i = 0; i < 2; i++) {
        json_object *document = fx_read(paths[i]);
        json_object *page = fx_field(fx_field(document, "data"), "event_observation");
        cursor = sequence(page, cursor);
        if (i == 1)
            fx_require(cursor == integer(page, "head_cursor"), "complete event pages");
        json_object_put(document);
    }
}
static void bulk_events(const char *path) {
    FILE *stream = fopen(path, "w");
    fx_require(stream != NULL, path);
    char payload[3201];
    memset(payload, 'x', 3200);
    payload[3200] = 0;
    for (int n = 1; n <= 200; n++)
        fx_require(
            fprintf(stream,
                    "{\"schema_version\":1,\"sequence\":%d,\"type\":\"bulk\",\"payload\":\"%s\"}\n",
                    n, payload) > 0,
            "bulk write");
    fx_require(fclose(stream) == 0, "bulk close");
}
static void transport(const char *run, const char *lost) {
    char pattern[4096];
    fx_path(pattern, run, "steps/*/attempt-*/remote/transport-metrics.json");
    glob_t paths = {0};
    fx_require(glob(pattern, 0, NULL, &paths) == 0 && paths.gl_pathc > 0, "transport records");
    for (size_t i = 0; i < paths.gl_pathc; i++) {
        json_object *record = fx_read(paths.gl_pathv[i]);
        fx_require(integer(record, "calls") > 0 && integer(record, "request_bytes") > 0 &&
                       integer(record, "response_bytes") > 0 &&
                       equal(fx_field(record, "complete"), "true"),
                   "complete positive transport metrics");
        json_object_put(record);
    }
    globfree(&paths);
    if (!strcmp(lost, "1")) {
        fx_path(pattern, run, "steps/produce/attempt-1/remote/transport-metrics.json");
        json_object *record = fx_read(pattern);
        fx_require(integer(record, "calls") >= 2, "lost acknowledgment calls");
        json_object_put(record);
    }
}
int fx_events(int argc, char **argv) {
    if (argc == 3 && !strcmp(argv[1], "observe-check")) {
        observed_task(argv[2]);
        return 0;
    }
    if (argc == 4 && !strcmp(argv[1], "observe-pages")) {
        pages(argv[2], argv[3]);
        return 0;
    }
    if (argc == 3 && !strcmp(argv[1], "bulk-events")) {
        bulk_events(argv[2]);
        return 0;
    }
    if (argc == 4 && !strcmp(argv[1], "transport-metrics")) {
        transport(argv[2], argv[3]);
        return 0;
    }
    if ((argc == 4 && !strcmp(argv[1], "bulk-page")) || (argc == 3 && !strcmp(argv[1], "offset"))) {
        json_object *document = fx_read(argv[2]),
                    *page = fx_field(fx_field(document, "data"), "event_observation");
        if (argc == 3)
            printf("%lld\n", integer(page, "next_byte_offset"));
        else {
            char *end = NULL;
            long long cursor = strtoll(argv[3], &end, 10);
            fx_require(*argv[3] && end && !*end && cursor >= 0, "cursor");
            sequence(page, cursor);
            const char *stream = f_string(page, "stream_id");
            fx_require(stream != NULL, "stream identity");
            fx_require(json_object_is_type(fx_field(page, "scan_truncated"), json_type_boolean),
                       "truncation flag");
            printf("%lld %lld %s %zu %s\n", integer(page, "next_cursor"),
                   integer(page, "next_byte_offset"), stream,
                   json_object_array_length(fx_field(page, "events")),
                   json_object_get_boolean(f_field(page, "scan_truncated")) ? "true" : "false");
        }
        json_object_put(document);
        return 0;
    }
    return 1;
}
