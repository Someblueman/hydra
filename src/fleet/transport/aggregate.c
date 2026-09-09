#include "fleet/support/json.h"
#include "fleet/support/files.h"
#include "fleet/transport/remote.h"
#include "fleet/support/process.h"
#include "fleet/fleet.h"
#include <errno.h>
#include <stdlib.h>
#include <string.h>
#include <sys/wait.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>

#define OBSERVATION_CACHE_LIMIT (2U * 1024U * 1024U)
#define OBSERVATION_TASK_LIMIT 512U
#define OBSERVATION_STEP_LIMIT 512U
#define OBSERVATION_REQUEST_LIMIT 32U

static int host_order(const void *left, const void *right) {
    const char *a = f_string(*(json_object *const *)left, "host"), *b = f_string(*(json_object *const *)right, "host");
    return strcmp(a ? a : "", b ? b : "");
}
json_object *f_aggregate(const char *action, unsigned seconds, unsigned jobs) {
    json_object *names = f_remotes(), *hosts = json_object_new_array(), *result, *data = json_object_new_object();
    size_t count = json_object_array_length(names), next = 0, finished = 0;
    struct worker { pid_t pid; size_t index; char path[F_PATH]; } workers[16] = {{0}};
    char dir[] = "/tmp/hydra-fleet.XXXXXX"; unsigned active = 0, i; bool failed = false, succeeded = false;
    if (count > 16 || !mkdtemp(dir)) { json_object_put(names); json_object_put(hosts); json_object_put(data); return f_error("fleet", "limit", "fleet supports at most 16 hosts"); }
    while (finished < count && !f_stopped) {
        for (i = 0; i < jobs && next < count; i++) {
            if (workers[i].pid) continue;
            workers[i].index = next++;
            snprintf(workers[i].path, sizeof(workers[i].path), "%s/%zu", dir, workers[i].index);
            workers[i].pid = fork();
            if (workers[i].pid == 0) {
                struct f_remote remote; json_object *row; const char *text;
                const char *name = json_object_get_string(json_object_array_get_idx(names, workers[i].index));
                row = f_remote_load(name, &remote) ? f_error("fleet", "invalid_alias", name) : f_observe(&remote, action, seconds);
                f_string_add(row, "host", name);
                text = json_object_to_json_string_ext(row, JSON_C_TO_STRING_PLAIN);
                _exit(f_write(workers[i].path, text, strlen(text), false) ? 1 : 0);
            }
            if (workers[i].pid < 0) { workers[i].pid = 0; f_stopped = SIGTERM; break; }
            active++;
        }
        for (i = 0; i < jobs; i++) {
            int status; char *text; json_object *row;
            if (!workers[i].pid || waitpid(workers[i].pid, &status, WNOHANG) != workers[i].pid) continue;
            workers[i].pid = 0; active--; finished++;
            text = f_read(workers[i].path, F_LIMIT); row = text ? f_parse(text) : NULL; free(text);
            if (!row) row = f_error("fleet", "worker_failed", "host worker did not return a result");
            if (json_object_get_boolean(f_field(row, "ok"))) succeeded = true; else failed = true;
            json_object_array_add(hosts, row); unlink(workers[i].path);
        }
        if (active) { struct timespec pause = {0, 20000000}; nanosleep(&pause, NULL); }
    }
    for (i = 0; i < jobs; i++) if (workers[i].pid) {
        int status; kill(workers[i].pid, SIGTERM);
        while (waitpid(workers[i].pid, &status, 0) < 0 && errno == EINTR) { }
        unlink(workers[i].path);
    }
    rmdir(dir); json_object_put(names);
    json_object_array_sort(hosts, host_order);
    json_object_object_add(data, "hosts", hosts);
    json_object_object_add(data, "partial", json_object_new_boolean(failed && succeeded));
    result = (failed || f_stopped) ? f_error("fleet", f_stopped ? "cancelled" : "partial_failure", "some hosts could not be observed") : f_success("fleet", NULL);
    json_object_object_add(result, "data", data); return result;
}

static void add_nullable(json_object *object, const char *key, const char *value) {
    if (value) f_string_add(object, key, value);
    else json_object_object_add(object, key, json_object_new_null());
}

static bool bounded_text(json_object *object, const char *key, size_t limit, bool nullable_value) {
    json_object *value = f_field(object, key); const char *text = f_text(value);
    const unsigned char *p;
    if (nullable_value && json_object_is_type(value, json_type_null)) return true;
    if (!text || strlen(text) >= limit) return false;
    for (p = (const unsigned char *)text; *p; p++) if (*p < 32 || *p == 127) return false;
    return true;
}

static bool observation_time(json_object *object, const char *key) {
    json_object *value = f_field(object, key);
    return json_object_is_type(value, json_type_null) ||
        (json_object_is_type(value, json_type_int) && json_object_get_int64(value) >= 0);
}

static bool bounded_string_array(json_object *array, size_t limit) {
    size_t i;
    if (!json_object_is_type(array, json_type_array) || json_object_array_length(array) > limit) return false;
    for (i = 0; i < json_object_array_length(array); i++) {
        const unsigned char *p; const char *value = f_text(json_object_array_get_idx(array, i));
        if (!value || strlen(value) >= 1024) return false;
        for (p = (const unsigned char *)value; *p; p++) if (*p < 32 || *p == 127) return false;
    }
    return true;
}

static bool pending_requests_valid(json_object *pending) {
    size_t i;
    if (!json_object_is_type(pending, json_type_array) || json_object_array_length(pending) > OBSERVATION_REQUEST_LIMIT) return false;
    for (i = 0; i < json_object_array_length(pending); i++) {
        json_object *request = json_object_array_get_idx(pending, i);
        if (!json_object_is_type(request, json_type_object) || !bounded_text(request, "request_id", 128, false) ||
            !bounded_text(request, "step_id", 128, false) || !bounded_text(request, "state", 64, false) ||
            !bounded_text(request, "message", 1024, true) || !bounded_text(request, "expires_at", 128, true)) return false;
    }
    return true;
}

static bool observation_snapshot_valid(json_object *snapshot) {
    json_object *tasks, *times, *waiting, *owner, *steps, *pending; size_t i, j;
    if (!json_object_is_type(snapshot, json_type_object) || !f_number_is(snapshot, "snapshot_schema_version", 1) ||
        !observation_time(snapshot, "receiver_observed_at") ||
        !(tasks = f_field(snapshot, "tasks")) || !json_object_is_type(tasks, json_type_array) ||
        json_object_array_length(tasks) > OBSERVATION_TASK_LIMIT) return false;
    for (i = 0; i < json_object_array_length(tasks); i++) {
        json_object *task = json_object_array_get_idx(tasks, i); const char *reason;
        if (!json_object_is_type(task, json_type_object) || !bounded_text(task, "task_id", 128, false) ||
            !bounded_text(task, "run_id", 128, true) ||
            !bounded_text(task, "step_id", 128, true) || !bounded_text(task, "attempt_id", 128, true) ||
            !bounded_text(task, "execution_state", 64, false) || !bounded_text(task, "assigned_host", 256, true) ||
            !bounded_text(task, "workspace", F_PATH, true) || !bounded_text(task, "agent_profile", 128, false) ||
            !(owner = f_field(task, "execution_owner")) || !json_object_is_type(owner, json_type_object) ||
            !bounded_text(owner, "kind", 64, false) || !bounded_text(owner, "state", 64, false) ||
            !bounded_text(owner, "recorded_state", 64, true) || !bounded_text(owner, "failure", 1024, true) ||
            !(waiting = f_field(task, "waiting")) || !json_object_is_type(waiting, json_type_object) ||
            !bounded_text(waiting, "reason", 32, false) || !bounded_text(waiting, "detail", 1024, false) ||
            !bounded_text(waiting, "next_action", 1024, false) || !(reason = f_string(waiting, "reason")) ||
            (strcmp(reason, "admission") && strcmp(reason, "dependency") && strcmp(reason, "authentication") &&
             strcmp(reason, "approval") && strcmp(reason, "reconciliation") && strcmp(reason, "none")) ||
            !(times = f_field(task, "observation_timestamps")) || !json_object_is_type(times, json_type_object) ||
            !observation_time(times, "accepted_at") || !observation_time(times, "started_at") ||
            !observation_time(times, "resumed_at") || !observation_time(times, "finished_at") ||
            !observation_time(times, "receiver_observed_at") ||
            !json_object_is_type(f_field(task, "effective_configuration"), json_type_object) ||
            strlen(json_object_to_json_string_ext(f_field(task, "effective_configuration"), JSON_C_TO_STRING_PLAIN)) >= 131072 ||
            !(json_object_is_type(f_field(task, "contract"), json_type_object)) ||
            !bounded_text(f_field(task, "contract"), "availability", 32, false) ||
            !bounded_text(f_field(task, "contract"), "reason", 1024, false) ||
            !bounded_string_array(f_field(f_field(task, "contract"), "missing_evidence"), OBSERVATION_REQUEST_LIMIT) ||
            !(pending = f_field(task, "pending_requests")) || !pending_requests_valid(pending) ||
            !(steps = f_field(task, "steps")) || !json_object_is_type(steps, json_type_array) ||
            json_object_array_length(steps) > OBSERVATION_STEP_LIMIT) return false;
        for (j = 0; j < i; j++) if (!strcmp(f_string(json_object_array_get_idx(tasks, j), "task_id"), f_string(task, "task_id"))) return false;
        for (j = 0; j < json_object_array_length(steps); j++) {
            json_object *step = json_object_array_get_idx(steps, j);
            if (!json_object_is_type(step, json_type_object) || !bounded_text(step, "step_id", 128, false) ||
                !bounded_text(step, "attempt_id", 128, true) || !bounded_text(step, "state", 64, false) ||
                !bounded_text(step, "agent_profile", 128, false) || !bounded_text(step, "kind", 64, false) ||
                !bounded_text(step, "waiting_reason", 32, false)) return false;
        }
    }
    return true;
}

static int cache_path(char path[F_PATH], const char *name) {
    char directory[F_PATH], filename[160];
    if (!f_name(name) || f_path(directory, sizeof(directory), f_home, "fleet/observations") ||
        snprintf(filename, sizeof(filename), "%s.json", name) >= (int)sizeof(filename) ||
        f_path(path, F_PATH, directory, filename)) return -1;
    return 0;
}

static bool private_cache_entry(const char *path, bool directory) {
    struct stat st;
    if (lstat(path, &st) || (directory ? !S_ISDIR(st.st_mode) : !S_ISREG(st.st_mode)) ||
        st.st_uid != geteuid() || (st.st_mode & 0022)) return false;
    return true;
}

static json_object *cache_read(const struct f_remote *remote) {
    char path[F_PATH], directory[F_PATH]; json_object *cache, *snapshot, *confirmed; const char *host, *target, *home;
    if (cache_path(path, remote->name) || f_path(directory, sizeof(directory), f_home, "fleet/observations") ||
        !private_cache_entry(directory, true) || !private_cache_entry(path, false)) return NULL;
    cache = f_read_json(path, OBSERVATION_CACHE_LIMIT); snapshot = f_field(cache, "snapshot");
    host = f_string(cache, "host"); target = f_string(cache, "target"); home = f_string(cache, "home"); confirmed = f_field(cache, "last_confirmed_at");
    if (!f_number_is(cache, "schema_version", 1) || !host || strcmp(host, remote->name) || !target || strcmp(target, remote->target) ||
        !home || strcmp(home, remote->home) || !json_object_is_type(confirmed, json_type_int) || json_object_get_int64(confirmed) < 0 ||
        !observation_snapshot_valid(snapshot)) { json_object_put(cache); return NULL; }
    return cache;
}

static void cache_write(const struct f_remote *remote, json_object *snapshot, int64_t confirmed) {
    char directory[F_PATH], path[F_PATH]; json_object *cache; const char *text;
    if (cache_path(path, remote->name) || f_path(directory, sizeof(directory), f_home, "fleet/observations") ||
        f_mkdirs(directory) || !private_cache_entry(directory, true) || confirmed < 0) return;
    cache = json_object_new_object(); json_object_object_add(cache, "schema_version", json_object_new_int(1));
    f_string_add(cache, "host", remote->name); f_string_add(cache, "target", remote->target); f_string_add(cache, "home", remote->home);
    json_object_object_add(cache, "last_confirmed_at", json_object_new_int64(confirmed)); json_object_object_add(cache, "snapshot", json_object_get(snapshot));
    text = json_object_to_json_string_ext(cache, JSON_C_TO_STRING_PLAIN); (void)f_write(path, text, strlen(text), true); json_object_put(cache);
}

static json_object *clone_json(json_object *object) {
    return object ? f_parse(json_object_to_json_string_ext(object, JSON_C_TO_STRING_PLAIN)) : NULL;
}

static const char *failure_state(const char *code) {
    if (!code) return "unavailable";
    if (!strcmp(code, "offline") || !strcmp(code, "timeout") || !strcmp(code, "cancelled") || !strcmp(code, "transport_failed")) return "unreachable";
    if (!strcmp(code, "authentication_failed")) return "authentication";
    if (!strcmp(code, "capability_unavailable") || !strcmp(code, "version_mismatch")) return "unsupported";
    if (!strcmp(code, "invalid_response")) return "malformed";
    return "remote_error";
}

static void connection(json_object *data, const char *state, const char *code) {
    json_object *value = json_object_new_object(); f_string_add(value, "state", state); add_nullable(value, "error", code); json_object_object_add(data, "connection", value);
}

static void nullable_int(json_object *object, const char *key, int64_t value) {
    if (value >= 0) json_object_object_add(object, key, json_object_new_int64(value));
    else json_object_object_add(object, key, json_object_new_null());
}

static void freshness(json_object *data, int64_t confirmed, int64_t now, bool stale) {
    json_object *value = json_object_new_object(); int64_t age = -1;
    if (confirmed >= 0 && now >= confirmed && now - confirmed <= 31536000LL) age = now - confirmed;
    f_string_add(value, "state", age >= 0 ? (stale ? "stale" : "fresh") : "unknown"); nullable_int(value, "age_seconds", age);
    json_object_object_add(data, "freshness", value); nullable_int(data, "last_confirmed_at", confirmed);
}

static json_object *decorate(json_object *row, const struct f_remote *remote, int64_t now) {
    json_object *data = f_field(row, "data"), *snapshot, *out;
    const char *code = f_string(f_field(row, "error"), "code");
    if (json_object_get_boolean(f_field(row, "ok"))) {
        snapshot = clone_json(data);
        if (!snapshot || !observation_snapshot_valid(snapshot)) {
            json_object_put(snapshot); json_object_put(row);
            row = f_error("fleet-overview", "invalid_response", "receiver returned a malformed observation snapshot");
            f_string_add(row, "host", remote->name); return row;
        }
        cache_write(remote, snapshot, now); connection(data, "reachable", NULL); freshness(data, now, now, false); json_object_put(snapshot); f_string_add(row, "host", remote->name); return row;
    }
    {
        json_object *cache = cache_read(remote);
        if (!cache) { f_string_add(row, "host", remote->name); return row; }
        snapshot = clone_json(f_field(cache, "snapshot")); out = f_success("fleet-overview", snapshot);
        f_string_add(out, "host", remote->name); connection(f_field(out, "data"), failure_state(code), code);
        freshness(f_field(out, "data"), json_object_get_int64(f_field(cache, "last_confirmed_at")), now, true);
        json_object_object_add(f_field(out, "data"), "cached", json_object_new_boolean(true)); json_object_put(cache); json_object_put(row); return out;
    }
}

json_object *f_observation_aggregate(unsigned seconds, unsigned jobs) {
    json_object *names = f_remotes(), *hosts = json_object_new_array(), *result, *data = json_object_new_object();
    size_t count = json_object_array_length(names), next = 0, finished = 0; unsigned active = 0, i;
    struct worker { pid_t pid; size_t index; char path[F_PATH]; } workers[16] = {{0}};
    char directory[] = "/tmp/hydra-fleet-observe.XXXXXX"; bool partial = false; int64_t now = (int64_t)time(NULL);
    if (count > 16 || !jobs || jobs > 16 || !mkdtemp(directory)) { json_object_put(names); json_object_put(hosts); json_object_put(data); return f_error("fleet-overview", "limit", "fleet observation supports at most 16 hosts and 16 workers"); }
    while (finished < count && !f_stopped) {
        for (i = 0; i < jobs && next < count; i++) {
            if (workers[i].pid) continue;
            workers[i].index = next++;
            if (snprintf(workers[i].path, sizeof(workers[i].path), "%s/%zu", directory, workers[i].index) >= (int)sizeof(workers[i].path)) { f_stopped = SIGTERM; break; }
            workers[i].pid = fork();
            if (workers[i].pid == 0) {
                struct f_remote remote; json_object *row; const char *text, *name = json_object_get_string(json_object_array_get_idx(names, workers[i].index));
                row = f_remote_load(name, &remote) ? f_error("fleet-overview", "invalid_alias", "remote alias is unavailable") : f_observe(&remote, "overview", seconds);
                f_string_add(row, "host", name); text = json_object_to_json_string_ext(row, JSON_C_TO_STRING_PLAIN);
                _exit(f_write(workers[i].path, text, strlen(text), false) ? 1 : 0);
            }
            if (workers[i].pid < 0) { workers[i].pid = 0; f_stopped = SIGTERM; break; }
            active++;
        }
        for (i = 0; i < jobs; i++) {
            int status; char *text; json_object *row; struct f_remote remote; const char *name;
            if (!workers[i].pid || waitpid(workers[i].pid, &status, WNOHANG) != workers[i].pid) continue;
            workers[i].pid = 0; active--; finished++; name = json_object_get_string(json_object_array_get_idx(names, workers[i].index));
            text = f_read(workers[i].path, OBSERVATION_CACHE_LIMIT); row = text ? f_parse(text) : NULL; free(text);
            if (!row || f_remote_load(name, &remote)) { json_object_put(row); row = f_error("fleet-overview", "invalid_response", "host worker did not return a valid observation"); f_string_add(row, "host", name); }
            else row = decorate(row, &remote, now);
            if (!json_object_get_boolean(f_field(row, "ok")) || f_field(f_field(row, "data"), "cached")) partial = true;
            json_object_array_add(hosts, row); unlink(workers[i].path);
        }
        if (active) { struct timespec pause = {0, 20000000}; nanosleep(&pause, NULL); }
    }
    for (i = 0; i < jobs; i++) if (workers[i].pid) { int status; kill(workers[i].pid, SIGTERM); while (waitpid(workers[i].pid, &status, 0) < 0 && errno == EINTR) { } unlink(workers[i].path); }
    rmdir(directory); json_object_put(names); json_object_array_sort(hosts, host_order); json_object_object_add(data, "hosts", hosts); json_object_object_add(data, "partial", json_object_new_boolean(partial));
    if (f_stopped) { json_object_put(data); return f_error("fleet-overview", "cancelled", "observation was interrupted; cached snapshots remain available"); }
    result = f_success("fleet-overview", data); return result;
}
