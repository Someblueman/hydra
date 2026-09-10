#define _POSIX_C_SOURCE 200809L
#ifdef __APPLE__
#define _DARWIN_C_SOURCE
#endif
#include "internal.h"

static bool bound_spec(const char *digest) {
    return strlen(digest) == 64 && strspn(digest, "0123456789abcdef") == 64;
}
static const char *disabled_reason(const struct app *app, const struct task_observation *task, char key) {
    if (app->view != 4) return "Select a remote task in Overview before using task controls.";
    if (strcmp(task->freshness, "fresh") || !strcmp(task->state, "outcome_unknown"))
        return "Task evidence is stale or uncertain; inspect and reconcile before mutating.";
    if (key == 'X') {
        if (!strcmp(task->state, "succeeded") || !strcmp(task->state, "failed") || !strcmp(task->state, "cancelled"))
            return "This task is terminal; inspect its recorded outcome.";
        if (strcmp(task->cancellation, "-")) return "Cancellation is already recorded; refresh receiver evidence.";
        return NULL;
    }
    if (strcmp(task->state, "waiting_approval")) return "This task is not waiting for approval.";
    if (!bound_spec(task->spec_sha256)) return "The receiver has no bound specification digest; mutation disabled.";
    if ((key == 'Y' || key == 'N') && (!task->request_id[0] || !strcmp(task->request_id, "-")))
        return "No current approval request is available; refresh receiver evidence.";
    return NULL;
}
static const struct task_observation *current_target(const struct app *app, const struct task_observation *target) {
    for (size_t i = 0; i < app->model.task_count; i++) {
        const struct task_observation *current = &app->model.tasks[i];
        if (strcmp(current->host, target->host) || strcmp(current->task_id, target->task_id)) continue;
        if (strcmp(current->spec_sha256, target->spec_sha256) || strcmp(current->request_id, target->request_id) ||
            strcmp(current->state, target->state) || strcmp(current->cancellation, target->cancellation)) return NULL;
        return current;
    }
    return NULL;
}
bool native_fleet_control_key(struct app *app, char key, const char *action) {
    char answer[64], prompt[192];
    if (app->task_selected >= app->model.task_count) {
        copy_text(app->notice, sizeof(app->notice), "No current remote task is selected; use j/k in Overview."); return true;
    }
    /* prompt_text refreshes the entire model. Keep no borrowed model pointers
     * across the modal; copy the exact receiver-owned binding first. */
    struct task_observation target = app->model.tasks[app->task_selected];
    const char *reason = disabled_reason(app, &target, key);
    if (reason) { copy_text(app->notice, sizeof(app->notice), reason); return true; }
    snprintf(prompt, sizeof(prompt), "Type %s to dispatch for the selected task: ", action);
    if (prompt_text(app, prompt, answer, sizeof(answer)) || strcmp(answer, action)) return true;
    const struct task_observation *current = current_target(app, &target);
    if (!current || disabled_reason(app, current, key)) {
        copy_text(app->notice, sizeof(app->notice), "Task evidence changed during confirmation; no mutation dispatched."); return true;
    }
    const char *request = key == 'Y' || key == 'N' ? target.request_id : "-";
    const char *trust = key == 'X' ? NULL : target.spec_sha256;
    if (!native_fleet_control_submit(app, current, action, request, trust))
        copy_text(app->notice, sizeof(app->notice), "Remote control not dispatched; inspect task evidence and retry explicitly.");
    return true;
}
