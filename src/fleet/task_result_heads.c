#define _XOPEN_SOURCE 700
#include "task.h"
#include <dirent.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

static char *scalar(const char *root, const char *name) {
    char path[F_PATH], *text;
    if (f_path(path, sizeof(path), root, name) || !(text = f_read(path, F_PATH))) return NULL;
    text[strcspn(text, "\r\n")] = '\0'; return text;
}
static json_object *head(const char *directory, const char *root, const char *id, const char *scratch) {
    char state[F_PATH], allowed[F_PATH], canonical[F_PATH], ref[160];
    char *workspace = NULL, *branch = NULL, *instance = NULL; json_object *result = NULL;
    struct f_capture cap = {0}; struct stat st;
    char *commit[] = {"rev-parse", "HEAD", NULL}, *dirty[] = {"status", "--porcelain=v1", "--untracked-files=all", NULL};
    char *fetch[] = {"fetch", "--no-tags", "--no-write-fetch-head", NULL, NULL, NULL};
    if (!f_name(id) || strncmp(id, "head_", 5) || f_path(state, sizeof(state), root, id) || lstat(state, &st) || !S_ISDIR(st.st_mode)) goto done;
    workspace = scalar(state, "worktree"); branch = scalar(state, "branch"); instance = scalar(state, "current-instance");
    if (!workspace || !branch || !*branch || !f_name(instance) || strncmp(instance, "instance_", 9) ||
        f_path(allowed, sizeof(allowed), directory, "heads") || !realpath(workspace, canonical) ||
        strncmp(canonical, allowed, strlen(allowed)) || canonical[strlen(allowed)] != '/' || task_git(canonical, commit, &cap)) goto done;
    cap.out[strcspn(cap.out, "\r\n")] = '\0';
    if (!task_hex(cap.out, 40) && !task_hex(cap.out, 64)) goto done;
    result = json_object_new_object(); f_string_add(result, "head_id", id); f_string_add(result, "instance_id", instance);
    f_string_add(result, "branch", branch); f_string_add(result, "commit", cap.out);
    f_string_add(result, "workspace", canonical);
    snprintf(ref, sizeof(ref), "refs/heads/%s", id); f_string_add(result, "bundle_ref", ref);
    f_capture_free(&cap);
    if (task_git(canonical, dirty, &cap)) goto bad;
    json_object_object_add(result, "dirty", json_object_new_boolean(*cap.out != '\0')); f_capture_free(&cap);
    /* Fetch the observed immutable object, not a branch which may have moved. */
    fetch[3] = canonical; fetch[4] = (char *)f_string(result, "commit");
    if (task_git(scratch, fetch, &cap)) goto bad;
    f_capture_free(&cap);
    { char *update[] = {"update-ref", ref, (char *)f_string(result, "commit"), NULL}; if (task_git(scratch, update, &cap)) goto bad; }
    goto done;
bad:
    json_object_put(result); result = NULL;
done:
    free(workspace); free(branch); free(instance); f_capture_free(&cap); return result;
}
json_object *task_result_heads(const char *directory, json_object *state, const char *scratch) {
    const char *project = f_string(state, "execution_project_id");
    char root[F_PATH]; DIR *dir; struct dirent *entry; json_object *heads = NULL;
    if (!f_name(project) || strncmp(project, "project_", 8) ||
        snprintf(root, sizeof(root), "%s/state/v2/projects/%s/heads", f_home, project) >= (int)sizeof(root) || !(dir = opendir(root))) return NULL;
    heads = json_object_new_array();
    while ((entry = readdir(dir))) {
        json_object *item;
        if (strncmp(entry->d_name, "head_", 5)) continue;
        if (json_object_array_length(heads) >= TASK_FILES || !(item = head(directory, root, entry->d_name, scratch))) {
            json_object_put(heads); heads = NULL; break;
        }
        json_object_array_add(heads, item);
    }
    closedir(dir); return heads;
}
