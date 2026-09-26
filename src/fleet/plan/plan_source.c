#define _XOPEN_SOURCE 700
#include "fleet/support/json.h"
#include "fleet/support/process.h"
#include "fleet/plan/plan.h"
#include "fleet/task/task.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Diagnose a source that could not be bound. This runs only after binding
 * failed, reads Git state and never changes the checkout. */
#define PLAN_SOURCE_LISTED 10U

static void source_diagnostic(json_object *errors, const char *root, const char *condition, const char *message, const char *recovery) {
    json_object *entry;
    size_t before = json_object_array_length(errors);
    plan_error(errors, "source", "invalid_source", message);
    if (json_object_array_length(errors) == before) return;
    entry = json_object_array_get_idx(errors, before);
    f_string_add(entry, "source", root);
    f_string_add(entry, "condition", condition);
    f_string_add(entry, "recovery", recovery);
}
static int git_status(const char *root, char *const args[]) {
    struct f_capture cap = {0}; int status = task_git(root, args, &cap);
    f_capture_free(&cap); return status;
}
static char *join(const char *a, const char *b, const char *c) {
    size_t na = strlen(a), nb = strlen(b), nc = strlen(c); char *text = malloc(na + nb + nc + 1);
    if (!text) return NULL;
    memcpy(text, a, na); memcpy(text + na, b, nb); memcpy(text + na + nb, c, nc + 1);
    return text;
}
/* Takes ownership of message and recovery. */
static void simple_diagnostic(json_object *errors, const char *root, const char *condition, char *message, char *recovery) {
    source_diagnostic(errors, root, condition, message ? message : "source cannot be bound",
        recovery ? recovery : "run validation from the project's main checkout");
    free(message); free(recovery);
}
/* Git's -z output is NUL separated; a full capture still lists its prefix. */
static json_object *changed_paths(const char *out, size_t bytes, size_t *count) {
    json_object *paths = json_object_new_array(); size_t offset = 0;
    *count = 0;
    while (offset < bytes && out[offset]) {
        size_t length = strnlen(out + offset, bytes - offset);
        if (*count < PLAN_SOURCE_LISTED) json_object_array_add(paths, json_object_new_string_len(out + offset, (int)length));
        (*count)++; offset += length + 1;
    }
    return paths;
}
static char *tracked_message(const char *root, json_object *paths, size_t count, const char *recovery) {
    char *text = NULL; size_t size = 0, i, listed = json_object_array_length(paths); FILE *out = open_memstream(&text, &size);
    if (!out) return NULL;
    fprintf(out, "source %s has %zu tracked change%s: ", root, count, count == 1 ? "" : "s");
    for (i = 0; i < listed; i++) fprintf(out, "%s%s", i ? ", " : "", f_text(json_object_array_get_idx(paths, i)));
    if (count > listed) fprintf(out, " and %zu more", count - listed);
    fprintf(out, "; validation binds the committed revision, so %s", recovery);
    if (fclose(out)) { free(text); return NULL; }
    return text;
}
static void tracked_diagnostic(json_object *errors, const char *root, json_object *paths, size_t count) {
    size_t before = json_object_array_length(errors);
    char *recovery = join("commit or stash these changes in ", root, ", then validate again");
    char *text = recovery ? tracked_message(root, paths, count, recovery) : NULL;
    source_diagnostic(errors, root, "tracked_changes", text ? text : "source has tracked changes; commit or stash them, then validate again",
        recovery ? recovery : "commit or stash these changes, then validate again");
    if (json_object_array_length(errors) > before) {
        json_object *entry = json_object_array_get_idx(errors, before);
        json_object_object_add(entry, "changed_paths", json_object_get(paths));
        json_object_object_add(entry, "changed_count", json_object_new_int64((int64_t)count));
    }
    free(text); free(recovery);
}
static bool tracked_changes(json_object *errors, const char *root) {
    char *diff[] = {"diff", "--name-only", "--no-renames", "-z", "HEAD", "--", NULL};
    struct f_capture cap = {0}; size_t count = 0; json_object *paths;
    int status = task_git(root, diff, &cap);
    bool found = (status == 0 || status == 125) && cap.out && cap.out[0];
    if (found) {
        paths = changed_paths(cap.out, cap.out_bytes, &count);
        tracked_diagnostic(errors, root, paths, count);
        json_object_put(paths);
    }
    f_capture_free(&cap); return found;
}
void plan_source_error(json_object *errors, const char *source) {
    char root[F_PATH];
    char *repository[] = {"rev-parse", "--show-toplevel", NULL}, *commit[] = {"rev-parse", "--verify", "--quiet", "HEAD^{commit}", NULL};
    if (!source || !realpath(source, root)) {
        source = source ? source : "(none)";
        simple_diagnostic(errors, source, "missing_directory", join("source directory ", source, " does not exist or cannot be resolved"), NULL);
    } else if (git_status(root, repository)) {
        simple_diagnostic(errors, root, "not_repository", join("source ", root, " is not a Git repository"), NULL);
    } else if (git_status(root, commit)) {
        simple_diagnostic(errors, root, "no_commit", join("source ", root, " has no commit to bind"),
            join("commit the project files in ", root, ", then validate again"));
    } else if (!tracked_changes(errors, root)) {
        simple_diagnostic(errors, root, "unreadable_content",
            join("source ", root, " could not be fingerprinted within bounds: an untracked file may be unreadable or too large, or Git did not finish in time"),
            join("inspect the untracked files listed by Git status in ", root, ", remove or ignore unreadable or oversized ones, then validate again"));
    }
}
