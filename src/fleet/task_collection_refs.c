#include "task.h"
#include <stdlib.h>
#include <string.h>

int task_collection_refs(const char *project, const char *id, json_object *result, const char *bundle, bool install) {
    json_object *heads = f_field(result, "heads"); size_t i, used = 0, capacity = 32768;
    char *transaction = calloc(capacity, 1); struct f_capture cap = {0}; int status = -1;
    if (!transaction) return -1;
    if (install) {
        status = -2;
        char *fetch[] = {"bundle", "unbundle", (char *)bundle, NULL};
        if (task_git(project, fetch, &cap)) goto done;
        f_capture_free(&cap);
    }
    used = (size_t)snprintf(transaction, capacity, "start\n");
    for (i = 0; i <= json_object_array_length(heads); i++) {
        json_object *head = i ? json_object_array_get_idx(heads, i - 1) : NULL;
        const char *commit = i ? f_string(head, "commit") : f_string(f_field(f_field(result, "spec"), "source"), "commit");
        const char *suffix = i ? f_string(head, "head_id") : "source";
        char ref[256], object[80]; char *read[] = {"show-ref", "--verify", "--hash", ref, NULL};
        char *symbolic[] = {"symbolic-ref", "--quiet", ref, NULL};
        char *exists[] = {"cat-file", "-e", object, NULL}; int found, written;
        snprintf(ref, sizeof(ref), "refs/hydra/tasks/%s/%s", id, suffix);
        snprintf(object, sizeof(object), "%s^{commit}", commit);
        status = -3;
        if (task_git(project, exists, &cap)) goto done;
        f_capture_free(&cap);
        if (!task_git(project, symbolic, &cap)) goto done;
        f_capture_free(&cap); found = task_git(project, read, &cap);
        status = -4;
        if (!found) { cap.out[strcspn(cap.out, "\r\n")] = '\0'; if (strcmp(cap.out, commit)) goto done; }
        else if (!install) goto done;
        f_capture_free(&cap);
        written = snprintf(transaction + used, capacity - used, "%s %s %s\n", found ? "create" : "verify", ref, commit);
        if (written < 0 || (size_t)written >= capacity - used) goto done;
        used += (size_t)written;
    }
    if (install) {
        char *argv[] = {"git", "-c", "core.hooksPath=/dev/null", "-C", (char *)project, "update-ref", "--no-deref", "--stdin", NULL};
        if (capacity - used < 16) goto done;
        strcpy(transaction + used, "prepare\ncommit\n"); used += strlen(transaction + used);
        status = -5;
        if (f_run(argv, transaction, used, 30, &cap) || cap.status) goto done;
    }
    status = 0;
done:
    free(transaction); f_capture_free(&cap); return status;
}
