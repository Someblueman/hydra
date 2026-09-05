#include "task.h"
#include <ctype.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <unistd.h>

const char *task_text(json_object *value) {
    const char *text;
    if (!json_object_is_type(value, json_type_string)) return NULL;
    text = json_object_get_string(value);
    return strlen(text) == (size_t)json_object_get_string_len(value) ? text : NULL;
}
bool task_hex(const char *text, size_t length) {
    size_t i;
    if (!text || strlen(text) != length) return false;
    for (i = 0; i < length; i++) if (!strchr("0123456789abcdef", text[i])) return false;
    return true;
}
bool task_path(const char *path) {
    const char *part, *end; size_t n;
    if (!path || !*path || strlen(path) >= 1024 || *path == '/') return false;
    for (part = path; *part; part = end + 1) {
        end = strchr(part, '/'); if (!end) end = part + strlen(part);
        n = (size_t)(end - part);
        if (!n || (n == 1 && *part == '.') || (n == 2 && !strncmp(part, "..", 2)) ||
            (n == 4 && !strncasecmp(part, ".git", 4)) ||
            (n == 11 && !strncasecmp(part, ".hydra-task", 11))) return false;
        while (part < end) {
            unsigned char c = (unsigned char)*part++;
            if (c < 32 || c == 127 || c == '\\' || c == ':') return false;
        }
        if (!*end) return true;
        if (!end[1]) return false;
    }
    return false;
}
bool task_keys(json_object *object, const char *const keys[]) {
    if (!json_object_is_type(object, json_type_object)) return false;
    json_object_object_foreach(object, key, value) {
        size_t i; (void)value;
        for (i = 0; keys[i] && strcmp(key, keys[i]); i++) {}
        if (!keys[i]) return false;
    }
    return true;
}
static json_object *strings(json_object *array, size_t max, bool paths, bool names) {
    json_object *out; size_t i, j;
    if (!json_object_is_type(array, json_type_array) || json_object_array_length(array) > max) return NULL;
    out = json_object_new_array();
    for (i = 0; i < json_object_array_length(array); i++) {
        const char *text = task_text(json_object_array_get_idx(array, i));
        if (!text || strlen(text) > 4096 || (paths && !task_path(text)) || (names && (!f_name(text) || strlen(text) > 64))) goto bad;
        if (paths || names) for (j = 0; j < i; j++) {
            const char *previous = task_text(json_object_array_get_idx(array, j));
            if (!strcmp(previous, text)) goto bad;
        }
        json_object_array_add(out, json_object_new_string(text));
    }
    return out;
bad:
    json_object_put(out); return NULL;
}
static json_object *work_spec(json_object *input) {
    const char *const keys[] = {"kind", "argv", "path", NULL};
    const char *kind = f_string(input, "kind"); json_object *work, *args;
    if (!task_keys(input, keys) || !kind) return NULL;
    work = json_object_new_object(); f_string_add(work, "kind", kind);
    if (!strcmp(kind, "exec")) {
        if (f_field(input, "path") || !(args = strings(f_field(input, "argv"), 128, false, false))) goto bad;
        json_object_object_add(work, "argv", args);
        if (!json_object_array_length(args) || !*task_text(json_object_array_get_idx(args, 0))) goto bad;
    } else if (!strcmp(kind, "workflow")) {
        const char *path = f_string(input, "path");
        if (f_field(input, "argv") || !task_path(path) || strncmp(path, ".hydra/workflows/", 17)) goto bad;
        f_string_add(work, "path", path);
    } else goto bad;
    return work;
bad:
    json_object_put(work); return NULL;
}
static json_object *limits_spec(json_object *input) {
    const char *const keys[] = {"transport_seconds", "queue_seconds", "startup_seconds", "execution_seconds", "cancellation_seconds", "log_bytes", "artifact_bytes", NULL};
    json_object *out; size_t i;
    if (!task_keys(input, keys)) return NULL;
    out = json_object_new_object();
    for (i = 0; keys[i]; i++) {
        json_object *value = f_field(input, keys[i]); int64_t n = json_object_get_int64(value);
        int64_t max = i < 5 ? (i == 0 || i == 4 ? 300 : 604800) : TASK_FILE_LIMIT;
        if (!json_object_is_type(value, json_type_int) || n < 1 || n > max) { json_object_put(out); return NULL; }
        json_object_object_add(out, keys[i], json_object_new_int64(n));
    }
    return out;
}
static json_object *input_manifest(json_object *input) {
    const char *const keys[] = {"path", "sha256", "bytes", NULL};
    json_object *out; size_t i, j;
    if (!json_object_is_type(input, json_type_array) || json_object_array_length(input) > TASK_FILES) return NULL;
    out = json_object_new_array();
    for (i = 0; i < json_object_array_length(input); i++) {
        json_object *entry = json_object_array_get_idx(input, i), *file, *bytes = f_field(entry, "bytes");
        const char *path = f_string(entry, "path"), *hash = f_string(entry, "sha256");
        if (!task_keys(entry, keys) || !task_path(path) || !task_hex(hash, 64) ||
            !json_object_is_type(bytes, json_type_int) || json_object_get_int64(bytes) < 0 || json_object_get_int64(bytes) > TASK_FILE_LIMIT) goto bad;
        for (j = 0; j < i; j++) if (!strcmp(path, f_string(json_object_array_get_idx(out, j), "path"))) goto bad;
        file = json_object_new_object(); f_string_add(file, "path", path); f_string_add(file, "sha256", hash);
        json_object_object_add(file, "bytes", json_object_new_int64(json_object_get_int64(bytes)));
        json_object_array_add(out, file);
    }
    return out;
bad:
    json_object_put(out); return NULL;
}
json_object *task_spec(json_object *input, bool prepared) {
    const char *const keys[] = {"schema_version", "host", "project", "source", "work", "inputs", "outputs", "capabilities", "completion", "limits", NULL};
    const char *const source_keys[] = {"commit", "bundle_sha256", NULL};
    const char *host = f_string(input, "host"), *project = f_string(input, "project"), *completion = f_string(input, "completion");
    json_object *out = NULL, *part, *src = f_field(input, "source"); const char *commit = f_string(src, "commit"); size_t i;
    if (!task_keys(input, keys) || strlen(json_object_to_json_string_ext(input, JSON_C_TO_STRING_PLAIN)) > 65536 ||
        !f_number_is(input, "schema_version", 1) || !f_name(host) || strlen(host) >= 128 ||
        !project || project[0] != '/' || strlen(project) >= F_PATH || !task_keys(src, source_keys) ||
        (!task_hex(commit, 40) && !task_hex(commit, 64))) return NULL;
    for (i = 0; project[i]; i++) if ((unsigned char)project[i] < 32 || (unsigned char)project[i] == 127) return NULL;
    if (prepared ? !task_hex(f_string(src, "bundle_sha256"), 64) : f_field(src, "bundle_sha256") != NULL) return NULL;
    out = json_object_new_object(); json_object_object_add(out, "schema_version", json_object_new_int(1));
    f_string_add(out, "host", host); f_string_add(out, "project", project);
    part = json_object_new_object(); f_string_add(part, "commit", commit);
    if (prepared) f_string_add(part, "bundle_sha256", f_string(src, "bundle_sha256"));
    json_object_object_add(out, "source", part);
    if (!(part = work_spec(f_field(input, "work")))) goto bad;
    json_object_object_add(out, "work", part);
    if (!completion || strcmp(completion, !strcmp(f_string(part, "kind"), "exec") ? "command-exit" : "workflow-success")) goto bad;
    f_string_add(out, "completion", completion);
    part = prepared ? input_manifest(f_field(input, "inputs")) : strings(f_field(input, "inputs"), TASK_FILES, true, false);
    if (!part) goto bad;
    json_object_object_add(out, "inputs", part);
    if (!(part = strings(f_field(input, "outputs"), TASK_FILES, true, false))) goto bad;
    json_object_object_add(out, "outputs", part);
    if (!(part = strings(f_field(input, "capabilities"), 32, false, true))) goto bad;
    json_object_object_add(out, "capabilities", part);
    if (!(part = limits_spec(f_field(input, "limits")))) goto bad;
    json_object_object_add(out, "limits", part);
    return out;
bad:
    json_object_put(out); return NULL;
}
int task_json_hash(json_object *object, const char *scratch, char digest[65]) {
    char path[F_PATH]; const char *text = json_object_to_json_string_ext(object, JSON_C_TO_STRING_PLAIN); int status;
    if (f_path(path, sizeof(path), scratch, "digest.json") || f_write(path, text, strlen(text), false)) return -1;
    status = f_hash(path, digest); unlink(path); return status;
}
