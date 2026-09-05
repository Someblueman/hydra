#include "fleet.h"
#include <dirent.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

static bool config_path(const char *path) {
    const char *suffix;
    if (!path || strstr(path, "..") || strchr(path, '\\')) return false;
    if (!strcmp(path, "config.yml")) return true;
    if (!strncmp(path, "workflows/", 10)) suffix = path + 10;
    else if (!strncmp(path, "profiles/", 9)) suffix = path + 9;
    else if (!strncmp(path, "templates/", 10)) suffix = path + 10;
    else return false;
    return f_name(suffix) && strlen(suffix) > 4 && !strcmp(suffix + strlen(suffix) - 4, ".yml");
}
static bool history_path(const char *path) {
    const char *names[] = {"run-id", "workflow-id", "state", "created-at", "completed-at", "base-commit", "resolved.yml", "graph.tsv", "exit-code", "attempts", "stdout", "stderr", NULL};
    const char *file = path; size_t i;
    if (!path || strstr(path, "..")) return false;
    if (!strncmp(path, "steps/", 6)) {
        char step[128]; const char *slash = strchr(path + 6, '/'); size_t n;
        if (!slash || (n = (size_t)(slash - path - 6)) >= sizeof(step)) return false;
        memcpy(step, path + 6, n); step[n] = '\0'; if (!f_name(step)) return false;
        file = slash + 1;
    }
    for (i = 0; names[i]; i++) if (!strcmp(file, names[i])) return true;
    return false;
}
static int add_file(json_object *files, const char *root, const char *relative) {
    char path[F_PATH], *hex; json_object *file;
    if (f_path(path, sizeof(path), root, relative) || !(hex = f_hex_read(path))) return -1;
    file = json_object_new_object(); f_string_add(file, "path", relative); f_string_add(file, "hex", hex); free(hex);
    json_object_array_add(files, file); return 0;
}
static int history_files(json_object *files, const char *root, const char *relative) {
    char dirpath[F_PATH]; DIR *dir; struct dirent *entry;
    if (f_path(dirpath, sizeof(dirpath), root, relative) || !(dir = opendir(dirpath))) return -1;
    while ((entry = readdir(dir))) {
        char rel[F_PATH], path[F_PATH]; struct stat st;
        if (entry->d_name[0] == '.') continue;
        if (relative[0]) { if (f_path(rel, sizeof(rel), relative, entry->d_name)) continue; }
        else if (f_copy(rel, sizeof(rel), entry->d_name)) continue;
        if (f_path(path, sizeof(path), root, rel) || lstat(path, &st)) continue;
        if (S_ISREG(st.st_mode) && history_path(rel)) {
            if (add_file(files, root, rel)) { closedir(dir); return -1; }
        } else if (S_ISDIR(st.st_mode) && (!strcmp(rel, "steps") || (!strcmp(relative, "steps") && f_name(entry->d_name)))) {
            if (history_files(files, root, rel)) { closedir(dir); return -1; }
        }
    }
    closedir(dir); return 0;
}
json_object *f_bundle_export(const char *project, json_object *selected, const char *run) {
    json_object *bundle = json_object_new_object(), *files = json_object_new_array(), *result = NULL;
    char root[F_PATH]; size_t i;
    json_object_object_add(bundle, "schema_version", json_object_new_int(1)); f_string_add(bundle, "kind", run ? "history" : "config");
    json_object_object_add(bundle, "files", files);
    if (run) {
        struct f_capture cap = {0}; char *argv[] = {(char *)"git", (char *)"rev-parse", (char *)"--git-common-dir", NULL};
        char identity[F_PATH], *id = NULL, state[F_PATH], *value = NULL;
        if (!f_name(run) || strncmp(run, "run_", 4) || f_run(argv, NULL, 0, 5, &cap) || cap.status) { f_capture_free(&cap); goto done; }
        cap.out[strcspn(cap.out, "\r\n")] = '\0';
        if (!f_path(identity, sizeof(identity), cap.out, "hydra/project-id")) id = f_read(identity, 128);
        f_capture_free(&cap); if (!id) goto done; id[strcspn(id, "\r\n")] = '\0';
        if (!f_name(id) || snprintf(root, sizeof(root), "%s/state/v2/projects/%s/workflows/runs/%s", f_home, id, run) >= (int)sizeof(root)) { free(id); goto done; }
        free(id);
        if (!f_path(state, sizeof(state), root, "state")) value = f_read(state, 64);
        if (!value) goto done;
        value[strcspn(value, "\r\n")] = '\0';
        if (strcmp(value, "succeeded") && strcmp(value, "failed") && strcmp(value, "cancelled")) { free(value); goto done; }
        free(value); if (history_files(files, root, "")) goto done;
    } else {
        if (!selected || !json_object_is_type(selected, json_type_array) || !json_object_array_length(selected) || f_path(root, sizeof(root), project, ".hydra")) goto done;
        for (i = 0; i < json_object_array_length(selected); i++) {
            json_object *item = json_object_array_get_idx(selected, i); const char *rel;
            char parent[F_PATH], full[F_PATH]; struct stat st;
            if (!json_object_is_type(item, json_type_string)) goto done;
            rel = json_object_get_string(item);
            if (!config_path(rel) || f_path(full, sizeof(full), root, rel)) goto done;
            f_copy(parent, sizeof(parent), full); *strrchr(parent, '/') = '\0';
            if (lstat(root, &st) || !S_ISDIR(st.st_mode) || lstat(parent, &st) || !S_ISDIR(st.st_mode) || add_file(files, root, rel)) goto done;
        }
    }
    if (strlen(json_object_to_json_string_ext(bundle, JSON_C_TO_STRING_PLAIN)) > F_LIMIT / 2) goto done;
    result = f_success("fleet-export", json_object_get(bundle));
done:
    json_object_put(bundle);
    return result ? result : f_error("fleet-export", "invalid_bundle", "select declarative .hydra files or a terminal workflow run; links and live records are excluded");
}
json_object *f_bundle_import(const char *project, json_object *bundle) {
    const char *kind = f_string(bundle, "kind"); json_object *files = f_field(bundle, "files"), *result = NULL;
    char stage[F_PATH] = "", dest[F_PATH], parent[F_PATH]; size_t i;
    if (!kind || (strcmp(kind, "config") && strcmp(kind, "history")) || !f_number_is(bundle, "schema_version", 1) || !json_object_is_type(files, json_type_array) || json_object_array_length(files) > 256) goto done;
    if (!strcmp(kind, "config")) { if (f_copy(parent, sizeof(parent), project) || f_path(dest, sizeof(dest), project, ".hydra")) goto done; }
    else { if (f_path(parent, sizeof(parent), f_home, "fleet/history") || f_mkdirs(parent)) goto done; dest[0] = '\0'; }
    if (snprintf(stage, sizeof(stage), "%s/.fleet-import.XXXXXX", parent) >= (int)sizeof(stage) || !mkdtemp(stage)) { stage[0] = '\0'; goto done; }
    for (i = 0; i < json_object_array_length(files); i++) {
        json_object *file = json_object_array_get_idx(files, i); const char *rel = f_string(file, "path"), *hex = f_string(file, "hex");
        char path[F_PATH], dir[F_PATH];
        if (!(strcmp(kind, "config") ? history_path(rel) : config_path(rel)) || !hex || f_path(path, sizeof(path), stage, rel)) goto done;
        f_copy(dir, sizeof(dir), path); *strrchr(dir, '/') = '\0';
        if (f_mkdirs(dir) || f_hex_write(path, hex, 0600)) goto done;
    }
    if (!strcmp(kind, "history")) {
        if (snprintf(dest, sizeof(dest), "%s/import-%s", parent, strrchr(stage, '/') + 1) >= (int)sizeof(dest)) goto done;
    }
    { struct stat st; if (!lstat(dest, &st) || rename(stage, dest)) goto done; }
    stage[0] = '\0'; result = json_object_new_object(); f_string_add(result, "path", dest);
    result = f_success("fleet-import", result);
done:
    if (stage[0]) f_remove_tree(stage);
    return result ? result : f_error("fleet-import", "invalid_bundle", "refusing unsafe bundle or existing configuration; imports never restore live state or trust");
}
