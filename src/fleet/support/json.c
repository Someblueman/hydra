#include "fleet/support/json.h"
#include "fleet/fleet.h"
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
json_object *f_parse_value(const char *text) {
    struct json_tokener *tok = json_tokener_new_ex(32);
    json_object *obj;
    size_t length = strlen(text), end;
    if (tok == NULL || length > F_LIMIT) { if (tok) json_tokener_free(tok); return NULL; }
    json_tokener_set_flags(tok, JSON_TOKENER_STRICT | JSON_TOKENER_VALIDATE_UTF8);
    obj = json_tokener_parse_ex(tok, text, (int)length + 1);
    end = json_tokener_get_parse_end(tok);
    if (json_tokener_get_error(tok) != json_tokener_success) { json_object_put(obj); obj = NULL; }
    while (end < length && isspace((unsigned char)text[end])) end++;
    if (end < length) { json_object_put(obj); obj = NULL; }
    if (obj && !finite_json(obj)) { json_object_put(obj); obj = NULL; }
    json_tokener_free(tok);
    return obj;
}
json_object *f_parse(const char *text) {
    json_object *value = f_parse_value(text);
    if (!json_object_is_type(value, json_type_object)) { json_object_put(value); return NULL; }
    return value;
}
json_object *f_read_json(const char *path, size_t limit) {
    FILE *file; char *bytes; size_t length; json_object *object = NULL;
    if (limit > F_LIMIT) return NULL;
    file = path ? fopen(path, "rb") : stdin;
    if (!file) return NULL;
    bytes = malloc(limit + 1); if (!bytes) { if (path) fclose(file); return NULL; }
    length = fread(bytes, 1, limit + 1, file);
    if (!ferror(file) && length <= limit && !memchr(bytes, '\0', length)) {
        bytes[length] = '\0'; object = f_parse(bytes);
    }
    free(bytes); if (path) fclose(file); return object;
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
const char *f_text(json_object *value) {
    const char *text;
    if (!json_object_is_type(value, json_type_string)) return NULL;
    text = json_object_get_string(value);
    return strlen(text) == (size_t)json_object_get_string_len(value) ? text : NULL;
}
const char *f_string(json_object *obj, const char *key) {
    return f_text(f_field(obj, key));
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
