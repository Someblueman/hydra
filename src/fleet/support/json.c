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
/* json-c 0.18 cannot parse non-ASCII text in strict mode: it tests
 * `c <= 0x1f` on a signed char, so every byte from 0x80 is taken for a control
 * character, and its UTF-8 validation counts a byte once per peek. Hydra
 * therefore validates UTF-8 itself (RFC 3629: no overlong forms, surrogates or
 * values beyond U+10FFFF) and hands json-c the same document with each
 * non-ASCII character of a string written as a \u escape, which parses to the
 * same bytes on every json-c version. */
static size_t utf8_scalar(const unsigned char *text, size_t available, unsigned long *scalar) {
    size_t length, i; unsigned long value; unsigned char low = 0x80, high = 0xBF, lead = text[0];
    if (lead < 0xC2 || lead > 0xF4) return 0;
    length = lead <= 0xDF ? 2 : lead <= 0xEF ? 3 : 4;
    if (lead == 0xE0) low = 0xA0;
    if (lead == 0xED) high = 0x9F;
    if (lead == 0xF0) low = 0x90;
    if (lead == 0xF4) high = 0x8F;
    if (length > available || text[1] < low || text[1] > high) return 0;
    value = lead & (0xFFu >> (length + 1));
    for (i = 1; i < length; i++) {
        if ((text[i] & 0xC0) != 0x80) return 0;
        value = (value << 6) | (text[i] & 0x3Fu);
    }
    *scalar = value; return length;
}
static size_t put_escape(char *out, unsigned long scalar) {
    if (scalar < 0x10000) return (size_t)sprintf(out, "\\u%04lx", scalar);
    scalar -= 0x10000;
    return (size_t)sprintf(out, "\\u%04lx\\u%04lx", 0xD800 + (scalar >> 10), 0xDC00 + (scalar & 0x3FF));
}
/* Tracks whether the next byte is inside a string and whether it is escaped. */
static void string_state(char c, bool *in_string, bool *escaped) {
    if (!*in_string) *in_string = c == '"';
    else if (*escaped) *escaped = false;
    else if (c == '\\') *escaped = true;
    else if (c == '"') *in_string = false;
}
/* The document with non-ASCII string text escaped, or NULL when it is not
 * well-formed UTF-8 or has non-ASCII bytes outside a string. */
static char *json_ascii(const char *text, size_t length, size_t *out_length) {
    char *out = malloc(length * 3 + 1); size_t at = 0, i = 0, used; bool in_string = false, escaped = false;
    unsigned long scalar;
    if (!out) return NULL;
    while (i < length) {
        if ((unsigned char)text[i] < 0x80) { string_state(text[i], &in_string, &escaped); out[at++] = text[i++]; continue; }
        used = in_string && !escaped ? utf8_scalar((const unsigned char *)text + i, length - i, &scalar) : 0;
        if (!used) { free(out); return NULL; }
        at += put_escape(out + at, scalar); i += used;
    }
    out[at] = '\0'; *out_length = at; return out;
}
static bool json_has_high_bytes(const char *text, size_t length) {
    size_t i;
    for (i = 0; i < length; i++) if ((unsigned char)text[i] >= 0x80) return true;
    return false;
}
static json_object *parse_ascii(const char *text, size_t length) {
    struct json_tokener *tok = json_tokener_new_ex(32);
    json_object *obj;
    size_t end;
    if (tok == NULL) return NULL;
    json_tokener_set_flags(tok, JSON_TOKENER_STRICT);
    obj = json_tokener_parse_ex(tok, text, (int)length + 1);
    end = json_tokener_get_parse_end(tok);
    if (json_tokener_get_error(tok) != json_tokener_success) { json_object_put(obj); obj = NULL; }
    while (end < length && isspace((unsigned char)text[end])) end++;
    if (end < length) { json_object_put(obj); obj = NULL; }
    if (obj && !finite_json(obj)) { json_object_put(obj); obj = NULL; }
    json_tokener_free(tok);
    return obj;
}
json_object *f_parse_value(const char *text) {
    size_t length = strlen(text), escaped_length;
    char *escaped; json_object *obj;
    if (length > F_LIMIT) return NULL;
    if (!json_has_high_bytes(text, length)) return parse_ascii(text, length);
    if (!(escaped = json_ascii(text, length, &escaped_length))) return NULL;
    obj = parse_ascii(escaped, escaped_length);
    free(escaped); return obj;
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
