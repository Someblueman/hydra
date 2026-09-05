#include "fleet.h"
#include <errno.h>
#include <stdlib.h>
#include <string.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

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
