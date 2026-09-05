#include "task.h"
#include <dirent.h>
#include <errno.h>
#include <string.h>
#include <sys/stat.h>

static bool selected(const char *name) {
    const char *names[] = {"state", "run-id", "workflow-id", "created-at", "completed-at", "started-at", "finished-at",
        "base-commit", "graph.tsv", "exit-code", "attempts", "authoritative-attempt", "stdout", "stderr",
        "status", "head-commit", "worktree-hash", "argv-hash", "branch", "complete", "latest-run", "latest-status",
        "latest-head-commit", "latest-worktree-hash", "approved-by", "approved-at", "approval-reason", NULL}; size_t i;
    for (i = 0; names[i]; i++) if (!strcmp(name, names[i])) return true;
    return false;
}
static int walk(json_object *files, const char *root, const char *relative, const char *scratch, size_t *remaining, unsigned depth) {
    char path[F_PATH]; struct stat st; DIR *dir; struct dirent *entry; int status = -1;
    if (!depth || !task_path(relative) || f_path(path, sizeof(path), root, relative)) return -1;
    if (lstat(path, &st)) return errno == ENOENT ? 0 : -1;
    if (!S_ISDIR(st.st_mode) || !(dir = opendir(path))) return -1;
    while ((entry = readdir(dir))) {
        char child[F_PATH];
        if (entry->d_name[0] == '.') continue;
        if (!f_name(entry->d_name) || f_path(child, sizeof(child), relative, entry->d_name) || f_path(path, sizeof(path), root, child) || lstat(path, &st)) goto done;
        if (S_ISDIR(st.st_mode)) { if (walk(files, root, child, scratch, remaining, depth - 1)) goto done; }
        else if (selected(entry->d_name) && task_result_files(files, root, child, scratch, remaining)) goto done;
    }
    status = 0;
done:
    closedir(dir); return status;
}
int task_result_evidence(json_object *files, const char *directory, json_object *state, json_object *heads, const char *scratch) {
    const char *project = f_string(state, "execution_project_id"), *run = f_string(state, "run_id");
    char root[F_PATH], relative[F_PATH]; size_t remaining = TASK_FILE_LIMIT, i;
    if (!f_name(project) || !f_name(run) || snprintf(root, sizeof(root), "%s/state/v2/projects/%s", f_home, project) >= (int)sizeof(root)) return -1;
    if (!strcmp(f_string(state, "work_kind"), "exec")) {
        if (task_result_files(files, directory, "provenance.json", scratch, &remaining)) return -1;
        /* Interrupted attempts need not have a completed JSON response. */
        { char path[F_PATH]; struct stat st;
          if (f_path(path, sizeof(path), directory, "attempt.json")) return -1;
          if (!lstat(path, &st) && task_result_files(files, directory, "attempt.json", scratch, &remaining)) return -1; }
        if (snprintf(relative, sizeof(relative), "exec/%s", run) >= (int)sizeof(relative)) return -1;
    } else if (snprintf(relative, sizeof(relative), "workflows/runs/%s", run) >= (int)sizeof(relative)) return -1;
    if (walk(files, root, relative, scratch, &remaining, 6)) return -1;
    for (i = 0; i < json_object_array_length(heads); i++) {
        if (snprintf(relative, sizeof(relative), "heads/%s/gates", f_string(json_object_array_get_idx(heads, i), "head_id")) >= (int)sizeof(relative) ||
            walk(files, root, relative, scratch, &remaining, 5)) return -1;
    }
    return 0;
}
