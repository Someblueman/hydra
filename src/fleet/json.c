#include "fleet.h"
#include <ctype.h>
#include <errno.h>
#include <fcntl.h>
#include <math.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

static bool finite_json(json_object *value) {
    size_t i;
    if (json_object_is_type(value, json_type_double)) return isfinite(json_object_get_double(value));
    if (json_object_is_type(value, json_type_array)) {
        for (i = 0; i < json_object_array_length(value); i++) if (!finite_json(json_object_array_get_idx(value, i))) return false;
    } else if (json_object_is_type(value, json_type_object)) {
        json_object_object_foreach(value, key, child) { (void)key; if (!finite_json(child)) return false; }
    }
    return true;
}
json_object *f_parse(const char *text) {
    struct json_tokener *tok = json_tokener_new_ex(32);
    json_object *obj;
    size_t length = strlen(text), end;
    if (tok == NULL || length > F_LIMIT) { if (tok) json_tokener_free(tok); return NULL; }
    json_tokener_set_flags(tok, JSON_TOKENER_STRICT | JSON_TOKENER_VALIDATE_UTF8);
    obj = json_tokener_parse_ex(tok, text, (int)length + 1);
    end = json_tokener_get_parse_end(tok);
    if (json_tokener_get_error(tok) != json_tokener_success) { json_object_put(obj); obj = NULL; }
    while (end < length && isspace((unsigned char)text[end])) end++;
    if (end < length || !json_object_is_type(obj, json_type_object)) { json_object_put(obj); obj = NULL; }
    if (obj && !finite_json(obj)) { json_object_put(obj); obj = NULL; }
    json_tokener_free(tok);
    return obj;
}
json_object *f_field(json_object *obj, const char *key) {
    json_object *value = NULL;
    if (json_object_is_type(obj, json_type_object)) (void)json_object_object_get_ex(obj, key, &value);
    return value;
}
bool f_number_is(json_object *obj, const char *key, int expected) {
    json_object *value = f_field(obj, key);
    return json_object_is_type(value, json_type_int) && json_object_get_int64(value) == expected;
}
const char *f_string(json_object *obj, const char *key) {
    json_object *value = f_field(obj, key);
    const char *text;
    if (!json_object_is_type(value, json_type_string)) return NULL;
    text = json_object_get_string(value);
    return strlen(text) == (size_t)json_object_get_string_len(value) ? text : NULL;
}
void f_string_add(json_object *obj, const char *key, const char *value) {
    json_object_object_add(obj, key, json_object_new_string(value ? value : ""));
}
json_object *f_success(const char *command, json_object *data) {
    json_object *obj = json_object_new_object();
    json_object_object_add(obj, "schema_version", json_object_new_int(1));
    json_object_object_add(obj, "ok", json_object_new_boolean(true));
    f_string_add(obj, "command", command);
    json_object_object_add(obj, "data", data);
    return obj;
}
json_object *f_error(const char *command, const char *code, const char *message) {
    json_object *obj = f_success(command, NULL), *err = json_object_new_object();
    json_object_object_del(obj, "data");
    json_object_object_add(obj, "ok", json_object_new_boolean(false));
    f_string_add(err, "code", code); f_string_add(err, "message", message);
    f_string_add(err, "recovery", "inspect remote state and the reported error before retrying; mutations are never replayed automatically");
    json_object_object_add(obj, "error", err);
    return obj;
}
int f_emit(json_object *obj) {
    int status = json_object_get_boolean(f_field(obj, "ok")) ? 0 : 1;
    if (puts(json_object_to_json_string_ext(obj, JSON_C_TO_STRING_PLAIN)) == EOF) return 1;
    return status;
}
int f_copy(char *dst, size_t size, const char *value) {
    int n = snprintf(dst, size, "%s", value ? value : "");
    return n >= 0 && (size_t)n < size ? 0 : -1;
}
int f_path(char *dst, size_t size, const char *a, const char *b) {
    int n = snprintf(dst, size, "%s/%s", a, b);
    return n >= 0 && (size_t)n < size ? 0 : -1;
}
int f_mkdirs(const char *path) {
    char copy[F_PATH]; char *p;
    if (f_copy(copy, sizeof(copy), path)) return -1;
    for (p = copy + 1; ; p++) {
        if (*p == '/' || *p == '\0') {
            char c = *p; struct stat st;
            *p = '\0';
            if (mkdir(copy, 0700) && errno != EEXIST) return -1;
            if (stat(copy, &st) || !S_ISDIR(st.st_mode)) return -1;
            *p = c;
            if (!c) break;
        }
    }
    return 0;
}
char *f_read(const char *path, size_t limit) {
    FILE *fp = path ? fopen(path, "rb") : stdin;
    char *data = NULL; size_t size;
    if (!fp) return NULL;
    data = malloc(limit + 1);
    if (!data) { if (path) fclose(fp); return NULL; }
    size = fread(data, 1, limit, fp);
    if (ferror(fp) || (size == limit && fgetc(fp) != EOF)) { free(data); data = NULL; }
    else data[size] = '\0';
    if (path) fclose(fp);
    return data;
}
int f_write(const char *path, const char *data, size_t size, bool replace) {
    char tmp[F_PATH]; int fd, status = -1;
    if (snprintf(tmp, sizeof(tmp), "%s.XXXXXX", path) >= (int)sizeof(tmp)) return -1;
    fd = mkstemp(tmp);
    if (fd < 0) return -1;
    while (size) {
        ssize_t n = write(fd, data, size);
        if (n < 0 && errno == EINTR) continue;
        if (n <= 0) goto done;
        size -= (size_t)n; data += n;
    }
    if (fsync(fd)) goto done;
    if (replace ? rename(tmp, path) == 0 : link(tmp, path) == 0) status = 0;
done:
    close(fd); unlink(tmp); return status;
}
bool f_name(const char *s) {
    if (!s || !isalnum((unsigned char)*s)) return false;
    for (; *s; s++) if (!isalnum((unsigned char)*s) && !strchr("_.-", *s)) return false;
    return true;
}
bool f_target(const char *s) {
    if (!s || !isalnum((unsigned char)*s)) return false;
    for (; *s; s++) if (!isalnum((unsigned char)*s) && !strchr("_.-@:", *s)) return false;
    return true;
}
