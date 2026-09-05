#include "task.h"
#include <errno.h>
#include <fcntl.h>
#include <stdlib.h>
#include <string.h>
#include <sys/file.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

bool task_runtime_valid(json_object *state) {
    const char *name = f_string(state, "state"), *intent = f_string(state, "launch_intent");
    const char *states[] = {"accepted", "starting", "running", "succeeded", "failed", "expired", "cancelled", "outcome_unknown", NULL}; size_t i;
    if (!f_number_is(state, "schema_version", 1) || !name || !intent ||
        (strcmp(intent, "pending") && strcmp(intent, "claimed") && strcmp(intent, "started"))) return false;
    if (!strcmp(name, "accepted") || !strcmp(name, "expired") || (!strcmp(name, "cancelled") && !strcmp(intent, "pending"))) {
        if (strcmp(intent, "pending") || f_field(state, "owner_pid") || f_field(state, "run_id")) return false;
    } else if (!strcmp(intent, "pending")) return false;
    if (!strcmp(name, "succeeded") && (!f_name(f_string(state, "run_id")) || !f_number_is(state, "exit_status", 0))) return false;
    for (i = 0; states[i]; i++) if (!strcmp(name, states[i])) return true;
    return false;
}
bool task_owner_active(const char *directory) {
    char path[F_PATH]; int fd; bool active = false;
    if (f_path(path, sizeof(path), directory, "owner.lock")) return false;
    fd = open(path, O_RDWR | O_NOFOLLOW); if (fd < 0) return false;
    if (flock(fd, LOCK_EX | LOCK_NB)) active = errno == EWOULDBLOCK || errno == EAGAIN;
    else (void)flock(fd, LOCK_UN);
    close(fd); return active;
}
static int detach(const char *directory, json_object *package, json_object *state, int owner) {
    pid_t child = fork(); int status;
    if (child < 0) return -1;
    if (!child) {
        int null_fd;
        signal(SIGHUP, SIG_IGN); f_stopped = 0;
        if (setsid() < 0) _exit(1);
        null_fd = open("/dev/null", O_RDWR);
        if (null_fd < 0 || dup2(null_fd, 0) < 0 || dup2(null_fd, 1) < 0 || dup2(null_fd, 2) < 0) _exit(1);
        if (null_fd > 2) close(null_fd);
        child = fork(); if (child < 0) _exit(1);
        if (child) _exit(0);
        umask(077); task_execute(directory, package, state); close(owner); _exit(0);
    }
    while (waitpid(child, &status, 0) < 0) if (errno != EINTR) return -1;
    return WIFEXITED(status) && !WEXITSTATUS(status) ? 0 : -1;
}
json_object *task_start(const char *id, const char *trust) {
    char root[F_PATH], directory[F_PATH], path[F_PATH]; int owner = -1;
    json_object *receipt = task_status(id), *package = NULL, *checked = NULL, *state = NULL, *claim = NULL, *result = NULL;
    json_object *data = f_field(receipt, "data"); const char *digest = f_string(data, "spec_sha256");
    if (!json_object_get_boolean(f_field(receipt, "ok"))) return receipt;
    if (!task_hex(trust, 64) || strcmp(trust, digest)) { result = f_error("fleet-task-start", "trust_required", "review the prepared specification and supply its exact digest with --trust-spec"); goto done; }
    if (task_store_root(root) || f_path(directory, sizeof(directory), root, id) || f_path(path, sizeof(path), directory, "owner.lock")) goto io;
    owner = open(path, O_RDWR | O_CREAT | O_NOFOLLOW, 0600);
    if (owner < 0 || fcntl(owner, F_SETFD, FD_CLOEXEC)) goto io;
    if (flock(owner, LOCK_EX | LOCK_NB)) {
        if (errno == EWOULDBLOCK || errno == EAGAIN) { result = task_status(id); goto done; }
        goto io;
    }
    state = task_read_record(directory, "state.json"); if (!task_runtime_valid(state)) goto io;
    if (strcmp(f_string(state, "state"), "accepted")) {
        if (!strcmp(f_string(state, "state"), "starting") || !strcmp(f_string(state, "state"), "running")) {
            result = f_error("fleet-task-start", "outcome_unknown", "the launch owner is gone; uncertain work is never replayed"); goto done;
        }
        result = task_status(id); goto done;
    }
    if (f_path(path, sizeof(path), directory, "launch.json")) goto io;
    if (!access(path, F_OK)) { result = f_error("fleet-task-start", "outcome_unknown", "a previous launch claim exists; inspect its recorded evidence without replaying work"); goto done; }
    if (f_path(path, sizeof(path), directory, "package.json") || !(package = f_read_json(path, TASK_PACKAGE_LIMIT))) goto io;
    checked = task_inspect(package);
    if (!json_object_get_boolean(f_field(checked, "ok")) || strcmp(f_string(package, "spec_sha256"), digest)) { result = f_error("fleet-task-start", "recovery_required", "the stored package no longer matches the acceptance digest"); goto done; }
    {
        int64_t accepted_at = json_object_get_int64(f_field(data, "accepted_at")), now = (int64_t)time(NULL);
        int64_t queue = json_object_get_int64(f_field(f_field(f_field(checked, "data"), "limits"), "queue_seconds"));
        if (now < accepted_at || now - accepted_at >= queue) {
            f_string_add(state, "state", "expired"); f_string_add(state, "failure", now < accepted_at ? "clock_changed" : "queue_deadline");
            if (task_write_json(directory, "state.json", state, true)) goto io;
            result = task_status(id); goto done;
        }
    }
    claim = json_object_new_object(); json_object_object_add(claim, "schema_version", json_object_new_int(1));
    f_string_add(claim, "trusted_spec_sha256", digest); json_object_object_add(claim, "requested_at", json_object_new_int64((int64_t)time(NULL)));
    if (task_write_json(directory, "launch.json", claim, false)) goto io;
    f_string_add(state, "state", "starting"); f_string_add(state, "launch_intent", "claimed");
    if (task_write_json(directory, "state.json", state, true) || detach(directory, package, state, owner)) {
        result = f_error("fleet-task-start", "outcome_unknown", "launch intent is durable but owner startup could not be confirmed; do not replay"); goto done;
    }
    result = task_status(id); goto done;
io:
    result = f_error("fleet-task-start", "recovery_required", "cannot validate or claim accepted task storage; work was not replayed");
done:
    if (owner >= 0) close(owner);
    json_object_put(receipt); json_object_put(package); json_object_put(checked); json_object_put(state); json_object_put(claim); return result;
}
