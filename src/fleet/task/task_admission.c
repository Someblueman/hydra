#include "fleet/task/task.h"
#include "fleet/support/files.h"
#include <dirent.h>
#include <errno.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>

/* Shell remains the only capacity authority. The returned JSON is owned by the
 * caller, including refusal envelopes; release it with json_object_put. */
static json_object *invoke(char **argv) {
    struct f_capture cap = {0}; json_object *result = NULL;
    if (!f_run(argv, NULL, 0, 5, &cap)) result = f_parse(cap.out);
    if (cap.status && json_object_get_boolean(f_field(result, "ok"))) { json_object_put(result); result = NULL; }
    f_capture_free(&cap);
    return result;
}
static int observe(const char *directory, json_object *state, json_object *reply) {
    json_object *record = f_field(reply, "data");
    if (!json_object_get_boolean(f_field(reply, "ok")) || !f_string(record, "state")) {
        const char *code = f_string(f_field(reply, "error"), "code");
        f_string_add(state, "failure", code ? code : "admission_unavailable");
        return -1;
    }
    json_object_object_add(state, "admission", json_object_get(record));
    return task_write_json(directory, "state.json", state, true);
}
static int labels(json_object *spec, char out[2049]) {
    json_object *caps = f_field(spec, "capabilities"); size_t i, used = 0;
    for (i = 0; i < json_object_array_length(caps); i++) {
        const char *name = f_text(json_object_array_get_idx(caps, i)); int n;
        if (strncmp(name, "label.", 6)) continue;
        n = snprintf(out + used, 2049 - used, "%s%s", used ? "," : "", name + 6);
        if (n < 0 || (size_t)n >= 2049 - used) return -1;
        used += (size_t)n;
    }
    if (!used) return f_copy(out, 2049, "-");
    return 0;
}
int task_admission_request(const char *directory, json_object *state, json_object *accepted, json_object *spec) {
    const char *id = f_string(accepted, "task_id"), *project = f_string(accepted, "project_id");
    int64_t now = (int64_t)time(NULL), at = json_object_get_int64(f_field(accepted, "accepted_at"));
    int64_t queue = json_object_get_int64(f_field(f_field(spec, "limits"), "queue_seconds"));
    char seconds[32], required[2049]; json_object *reply; int status;
    char *argv[] = {(char *)f_hydra, "admission", "request", (char *)id, (char *)project, seconds, required, NULL};
    if (labels(spec, required)) { f_string_add(state, "failure", "capability_labels_too_large"); return -1; }
    if (at < 0 || now < at || now - at >= queue) { f_string_add(state, "failure", "queue_deadline"); return -1; }
    snprintf(seconds, sizeof(seconds), "%lld", (long long)(queue - (now - at)));
    /* The existing durable launch claim forbids a second request after an
     * ambiguous response. Persist the original absolute deadline as well. */
    json_object_object_add(state, "admission_deadline", json_object_new_int64(at + queue));
    f_string_add(state, "admission_id", id);
    if (task_write_json(directory, "state.json", state, true)) return -1;
    reply = invoke(argv); status = observe(directory, state, reply); json_object_put(reply);
    return status;
}
int task_admission_context(const char *directory, json_object *state, json_object *spec) {
    json_object *accepted = task_read_record(directory, "acceptance.json");
    char bytes[2400], required[2049], *existing = NULL; int n, status = -1; struct stat st;
    const char *path = ".git/hydra/admission-context";
    const char *id = f_string(accepted, "task_id"), *project = f_string(accepted, "project_id");
    if (!id || !project || !f_name(id) || !f_name(project) || labels(spec, required)) goto done;
    n = snprintf(bytes, sizeof(bytes), "%s %s %lld %s\n", id, project,
        (long long)json_object_get_int64(f_field(f_field(spec, "limits"), "queue_seconds")), required);
    if (n < 0 || n >= (int)sizeof(bytes)) goto done;
    /* Resume migrates pre-admission suspended tasks from their immutable
     * acceptance. An existing binding must match; it is never overwritten. */
    if (!lstat(path, &st)) {
        if (!S_ISREG(st.st_mode) || !(existing = f_read(path, sizeof(bytes))) || strcmp(existing, bytes)) goto done;
    } else if (errno != ENOENT || f_write(path, bytes, (size_t)n, false)) goto done;
    f_string_add(state, "admission_id", id); status = 0;
done:
    free(existing); json_object_put(accepted); return status;
}
int task_admission_wait(const char *directory, json_object *state, struct task_control *control) {
    const char *id = f_string(state, "admission_id");
    char *argv[] = {(char *)f_hydra, "admission", "claim", (char *)id, NULL};
    int64_t deadline = json_object_get_int64(f_field(state, "admission_deadline"));
    struct timespec pause = {0, 200000000};
    if (!id) return -1;
    for (;;) {
        const char *name = f_string(f_field(state, "admission"), "state");
        json_object *reply; int status;
        if (control->process.stop(control->process.context)) return -1;
        if (!name) return -1;
        if ((int64_t)time(NULL) >= deadline || (int64_t)time(NULL) < json_object_get_int64(f_field(f_field(state, "admission"), "requested_at"))) {
            f_string_add(state, "failure", "queue_deadline"); return -1;
        }
        if (!strcmp(name, "reserved")) return 0;
        if (strcmp(name, "queued")) { f_string_add(state, "failure", f_string(f_field(state, "admission"), "reason")); return -1; }
        nanosleep(&pause, NULL);
        reply = invoke(argv); status = observe(directory, state, reply); json_object_put(reply);
        if (status) return -1;
    }
}
void task_admission_close(const char *directory, json_object *state, bool confirmed) {
    const char *id = f_string(state, "admission_id"); json_object *reply;
    char *cancel[] = {(char *)f_hydra, "admission", "cancel", (char *)id, NULL};
    char *release[] = {(char *)f_hydra, "admission", confirmed ? "release" : "unknown", (char *)id, confirmed ? "--confirmed" : NULL, NULL};
    if (!id) return;
    reply = invoke(cancel);
    if (!json_object_get_boolean(f_field(reply, "ok"))) { json_object_put(reply); reply = invoke(release); }
    if (json_object_get_boolean(f_field(reply, "ok"))) (void)observe(directory, state, reply);
    else f_string_add(state, "admission_cleanup", "incomplete");
    json_object_put(reply);
}
void task_admission_children(json_object *state) {
    const char *prefix = f_string(state, "admission_id"); char root[F_PATH]; DIR *dir; struct dirent *entry;
    bool complete = true;
    if (!prefix || f_path(root, sizeof(root), f_home, "admission") || !(dir = opendir(root))) return;
    while ((entry = readdir(dir))) {
        char id[161]; size_t length = strlen(entry->d_name), n = strlen(prefix); json_object *reply;
        char *cancel[] = {(char *)f_hydra, "admission", "cancel", id, NULL};
        char *release[] = {(char *)f_hydra, "admission", "release", id, "--confirmed", NULL};
        if (length <= n + 9 || length - 8 >= sizeof(id) || strncmp(entry->d_name, prefix, n) ||
            entry->d_name[n] != '.' || strcmp(entry->d_name + length - 8, ".request")) continue;
        memcpy(id, entry->d_name, length - 8); id[length - 8] = '\0';
        reply = invoke(cancel);
        if (!json_object_get_boolean(f_field(reply, "ok"))) { json_object_put(reply); reply = invoke(release); }
        if (!json_object_get_boolean(f_field(reply, "ok"))) complete = false;
        json_object_put(reply);
    }
    closedir(dir);
    f_string_add(state, "admission_cleanup", complete ? "confirmed" : "incomplete");
}
