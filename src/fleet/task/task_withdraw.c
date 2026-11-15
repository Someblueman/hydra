#define _XOPEN_SOURCE 700
#include "fleet/support/json.h"
#include "fleet/support/files.h"
#include "fleet/task/task.h"
#include <errno.h>
#include <fcntl.h>
#include <string.h>
#include <sys/file.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>

/* The refusal for a task withdrawal cannot move, or NULL when it can. Work
 * with a launch claim is execution_owned whether or not its owner still holds
 * the lock: the answer comes from the durable claim, so a task that already
 * finished is refused the same way while its owner exits and afterwards. */
static json_object *withdraw_refusal(const char *directory, json_object *state) {
    char path[F_PATH]; struct stat st;
    bool claimed = strcmp(f_string(state, "launch_intent"), "pending") || f_path(path, sizeof(path), directory, "launch.json") ||
                   !lstat(path, &st) || errno != ENOENT;
    if (!claimed && !strcmp(f_string(state, "state"), "accepted")) return NULL;
    return f_error("fleet-task-withdraw", claimed ? "execution_owned" : "invalid_state",
                   "only an accepted task without a launch claim can be withdrawn; inspect its status without replaying work");
}
/* Withdrawal shares the launch owner lock, so a start that already holds the
 * lock wins and a withdrawn record can never be launched afterwards. The
 * admission slot is never requested before launch, so nothing is released. */
json_object *task_withdraw(const char *id) {
    char root[F_PATH], directory[F_PATH], path[F_PATH]; int owner = -1;
    json_object *receipt = task_status(id), *state = NULL, *withdrawal = NULL, *result = NULL;
    if (!json_object_get_boolean(f_field(receipt, "ok"))) return receipt;
    json_object_put(receipt);
    if (task_store_root(root) || f_path(directory, sizeof(directory), root, id) || f_path(path, sizeof(path), directory, "owner.lock")) goto io;
    owner = open(path, O_RDWR | O_CREAT | O_NOFOLLOW, 0600);
    if (owner < 0 || fcntl(owner, F_SETFD, FD_CLOEXEC)) goto io;
    if (flock(owner, LOCK_EX | LOCK_NB)) {
        if (errno == EWOULDBLOCK || errno == EAGAIN) { result = f_error("fleet-task-withdraw", "execution_owned", "a launch owner holds this task; running work is never migrated"); goto done; }
        goto io;
    }
    state = task_read_record(directory, "state.json"); if (!task_runtime_valid(state)) goto io;
    if (!strcmp(f_string(state, "state"), "withdrawn")) { result = task_status(id); goto done; }
    if ((result = withdraw_refusal(directory, state))) goto done;
    withdrawal = json_object_new_object(); json_object_object_add(withdrawal, "schema_version", json_object_new_int(1));
    json_object_object_add(withdrawal, "withdrawn_at", json_object_new_int64((int64_t)time(NULL)));
    {
        json_object *accepted = task_read_record(directory, "acceptance.json");
        json_object_object_add(withdrawal, "assignment_generation", json_object_new_int(json_object_is_type(f_field(accepted, "assignment_generation"), json_type_int) ? json_object_get_int(f_field(accepted, "assignment_generation")) : 1));
        json_object_put(accepted);
    }
    if (task_write_json(directory, "withdrawal.json", withdrawal, false)) goto io;
    f_string_add(state, "state", "withdrawn"); f_string_add(state, "failure", "withdrawn_by_submitter");
    if (task_write_json(directory, "state.json", state, true)) { result = f_error("fleet-task-withdraw", "outcome_unknown", "the withdrawal claim is durable but the state update could not be confirmed; inspect before transferring"); goto done; }
    result = task_status(id); goto done;
io:
    result = f_error("fleet-task-withdraw", "recovery_required", "cannot validate or update accepted task storage; nothing was launched");
done:
    if (owner >= 0) close(owner);
    json_object_put(state); json_object_put(withdrawal); return result;
}
/* Stale-update rejection: a withdrawn task never starts, and a request that
 * names an assignment generation must match the one recorded at acceptance. */
json_object *task_generation_fence(const char *id, json_object *request) {
    json_object *receipt = task_status(id), *data = f_field(receipt, "data"), *requested = f_field(request, "assignment_generation"), *error = NULL;
    const char *state = f_string(f_field(data, "runtime"), "state");
    if (!json_object_get_boolean(f_field(receipt, "ok"))) return receipt;
    if (state && !strcmp(state, "withdrawn")) error = f_error("fleet-task-start", "withdrawn", "this task was withdrawn by its submitter before launch; it belongs to a newer assignment generation elsewhere");
    else if (requested) {
        int stored = json_object_is_type(f_field(data, "assignment_generation"), json_type_int) ? json_object_get_int(f_field(data, "assignment_generation")) : 1;
        if (!json_object_is_type(requested, json_type_int) || json_object_get_int(requested) != stored) {
            error = f_error("fleet-task-start", "stale_generation", "the request names a different assignment generation than this receiver accepted; nothing was launched");
            json_object_object_add(f_field(error, "error"), "accepted_generation", json_object_new_int(stored));
        }
    }
    json_object_put(receipt);
    return error;
}
