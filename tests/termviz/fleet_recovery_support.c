#define _XOPEN_SOURCE 700
#include "fleet_recovery_support.h"
#include <errno.h>
#include <glob.h>
#include <json-c/json.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>
char root[4096], build[4096], base[4096], source[4096], bin[4096], tui[4096], client[4096],
    output[1048576];
bool succeeded, cleaned, cleanup_failed;
json_object *field(json_object *o, const char *key) {
    json_object *v = NULL;
    CHECK(o && json_object_object_get_ex(o, key, &v), key);
    return v;
}
json_object *optional(json_object *o, const char *key) {
    json_object *v = NULL;
    if (o)
        json_object_object_get_ex(o, key, &v);
    return v;
}
const char *string(json_object *o) {
    const char *p = json_object_get_string(o);
    return p ? p : "";
}
void addstr(json_object *o, const char *key, const char *value) {
    json_object_object_add(o, key, json_object_new_string(value));
}
json_object *parse(const char *text) {
    json_object *o = json_tokener_parse(text);
    CHECK(o, "response JSON");
    return o;
}
void save_json(const char *name, json_object *value) {
    char path[4096];
    tv_format(path, sizeof(path), "%s/%s", base, name);
    CHECK(!json_object_to_file_ext(path, value, JSON_C_TO_STRING_PRETTY), "save JSON evidence");
}
void trim(char *value) {
    size_t n = strlen(value);
    while (n && (value[n - 1] == '\n' || value[n - 1] == '\r' || value[n - 1] == ' '))
        value[--n] = 0;
}
const char *run_at(const char *cwd, const char *const argv[]) {
    char path[4096];
    int status = tv_command(cwd, NULL, output, sizeof(output), 60, argv);
    json_object *record = json_object_new_object(), *args = json_object_new_array();
    size_t i;
    for (i = 0; argv[i]; i++)
        json_object_array_add(args, json_object_new_string(argv[i]));
    json_object_object_add(record, "argv", args);
    addstr(record, "cwd", cwd);
    json_object_object_add(record, "exit", json_object_new_int(status));
    addstr(record, "output", output);
    tv_format(path, sizeof(path), "%s/commands.jsonl", base);
    tv_append(path, json_object_to_json_string_ext(record, JSON_C_TO_STRING_PLAIN));
    tv_append(path, "\n");
    json_object_put(record);
    if (status)
        fprintf(stderr, "V4 command %s returned %d:\n%s\nEvidence: %s\n", argv[0], status, output,
                base);
    CHECK(!status, "V4 public command success");
    return output;
}
#define RUN(...) run_at(source, (const char *[]){__VA_ARGS__, NULL})
#define H(...) RUN(bin, __VA_ARGS__)
json_object *status(const char *host, const char *task) {
    json_object *doc = parse(H("fleet", "task", "status", host, "--id", task)),
                *data = json_object_get(field(doc, "data"));
    json_object_put(doc);
    return data;
}
bool runtime_is(json_object *data, const char *key, const char *expected) {
    return !strcmp(string(optional(optional(data, "runtime"), key)), expected);
}
json_object *wait_runtime(const char *host, const char *task, const char *key, const char *expected,
                          double timeout) {
    double end = tv_now() + timeout;
    json_object *data;
    do {
        data = status(host, task);
        if (expected ? runtime_is(data, key, expected)
                     : *string(optional(optional(data, "runtime"), key)))
            return data;
        json_object_put(data);
        tv_sleep(.15);
    } while (tv_now() < end);
    fprintf(stderr, "Timed out waiting %s %s %s\n", task, key, expected ? expected : "assigned");
    CHECK(false, "receiver runtime deadline");
    return NULL;
}
void check_runtime(const char *host, const char *task, const char *key, const char *expected) {
    json_object *data = status(host, task);
    CHECK(runtime_is(data, key, expected), "receiver runtime identity/state");
    json_object_put(data);
}
void cleanup(void) {
    char path[4096], home[4096], result[16384];
    const char *keep = getenv("HYDRA_TEST_KEEP_FIXTURE");
    int i;
    if (cleaned)
        return;
    cleaned = true;
    for (i = 0; i < 2; i++) {
        tv_format(home, sizeof(home), "%s/host-%c", base, 'a' + i);
        if (!tv_exists(home))
            continue;
        tv_format(path, sizeof(path), "%s/release", home);
        tv_write(path, "");
        tv_format(path, sizeof(path), "%s/release-workflow", home);
        tv_write(path, "");
        {
            const char *args[] = {
                "sh",
                "-c",
                "root=\"$1\"; fixture=\"$2\"; HYDRA_HOME=\"$3\"; export HYDRA_HOME; . "
                "\"$root/tests/workflow_task_cleanup.sh\"; workflow_task_fixture_quiesce "
                "\"$HYDRA_HOME\" && test_tmux_fixture_cleanup \"$fixture\"",
                "fixture-cleanup",
                root,
                base,
                home,
                NULL};
            int rc = tv_command(NULL, NULL, result, sizeof(result), 45, args);
            if (rc) {
                cleanup_failed = true;
                fprintf(stderr, "V4 cleanup: %s\n", result);
            }
        }
    }
    if (succeeded && !cleanup_failed && (!keep || strcmp(keep, "1"))) {
        const char *args[] = {"rm", "-rf", base, NULL};
        tv_command_ok(NULL, args);
    } else
        printf("V4 evidence: %s\n", base);
}
void open_observer(struct tv_session *s) {
    const char *args[] = {tui, "--fleet", "--view", "overview", NULL};
    tv_open(s, args, 140, 40, source);
}
void select_task(struct tv_session *s, const char *task) {
    char selected[256];
    int i;
    tv_until(s, task, 15);
    tv_format(selected, sizeof(selected), "> %s", task);
    for (i = 0; i < 8; i++) {
        if (tv_contains(s, selected))
            return;
        tv_send(s, "j");
        tv_pump(s, .15);
    }
    CHECK(false, "select exact receiver task");
}
size_t acceptance_count(const char *name) {
    glob_t g;
    char pattern[4096];
    int rc;
    size_t count;
    tv_format(pattern, sizeof(pattern), "%s/host-%s/fleet/tasks/task_*/acceptance.json", base,
              name);
    memset(&g, 0, sizeof(g));
    rc = glob(pattern, 0, NULL, &g);
    CHECK(rc == 0 || rc == GLOB_NOMATCH, "acceptance glob");
    count = g.gl_pathc;
    globfree(&g);
    return count;
}
json_object *workflow_logs(const char *task, long long offset, const char *kind, int limit) {
    char off[64], lim[64];
    json_object *doc, *log;
    tv_format(off, sizeof(off), "%lld", offset);
    tv_format(lim, sizeof(lim), "%d", limit);
    if (!strcmp(kind, "owner"))
        doc = parse(H("fleet", "task", "logs", "host-b", "--id", task, "--source", kind, "--offset",
                      off, "--limit", lim));
    else
        doc = parse(H("fleet", "task", "logs", "host-b", "--id", task, "--source", kind, "--step",
                      "work", "--attempt", "1", "--offset", off, "--limit", lim));
    log = json_object_get(field(field(doc, "data"), "log"));
    json_object_put(doc);
    return log;
}
void attempt_exact(json_object *rows, const char *state) {
    size_t i, n = 0;
    for (i = 0; i < json_object_array_length(rows); i++) {
        json_object *row = json_object_array_get_idx(rows, i);
        if (!strcmp(string(optional(row, "step_id")), "work")) {
            n++;
            CHECK(!strcmp(string(field(row, "attempt_id")), "attempt-1") &&
                      !strcmp(string(field(row, "state")), state),
                  "original work attempt preserved");
        }
    }
    CHECK(n == 1, "one work attempt");
}
bool work_running(json_object *document) {
    json_object *rows = field(field(field(document, "data"), "task"), "steps");
    size_t i;
    for (i = 0; i < json_object_array_length(rows); i++) {
        json_object *row = json_object_array_get_idx(rows, i);
        if (!strcmp(string(optional(row, "step_id")), "work") &&
            !strcmp(string(optional(row, "state")), "running"))
            return true;
    }
    return false;
}
void sequences(json_object *events, json_object *seen) {
    size_t i;
    for (i = 0; i < json_object_array_length(events); i++) {
        json_object *event = json_object_array_get_idx(events, i);
        long long sequence = json_object_get_int64(field(event, "sequence"));
        CHECK(sequence == (long long)json_object_array_length(seen) + 1,
              "complete ordered event sequence");
        json_object_array_add(seen, json_object_new_int64(sequence));
    }
}
void unhex(const char *hex, char *out, size_t cap) {
    size_t i, n = strlen(hex);
    CHECK(!(n % 2) && n / 2 < cap, "hex bounds");
    for (i = 0; i < n; i += 2) {
        unsigned byte;
        char pair[3] = {hex[i], hex[i + 1], 0};
        CHECK(strspn(pair, "0123456789abcdefABCDEF") == 2, "strict hex digits");
        CHECK(sscanf(pair, "%2x", &byte) == 1, "hex bytes");
        out[i / 2] = (char)byte;
    }
    out[n / 2] = 0;
}
const char workflow[] =
    "version: 1\nid: v4-events\nparallelism: 1\nresources:\n  disk_mb: 1\n  max_heads: 2\nsteps:\n "
    " - id: create\n    kind: spawn\n    needs: []\n    retry: 0\n    idempotent: false\n    "
    "args:\n      branch: v4-worker\n      terminal_mode: headless\n  - id: work\n    kind: exec\n "
    "   needs: [create]\n    retry: 0\n    idempotent: true\n    args:\n      head: v4-worker\n    "
    "  argv: [sh, workflow-work.sh]\n  - id: verify\n    kind: gate\n    needs: [work]\n    retry: "
    "0\n    idempotent: true\n    args:\n      head: v4-worker\n      name: result\n      argv: "
    "[test, -s, result.txt]\n";
