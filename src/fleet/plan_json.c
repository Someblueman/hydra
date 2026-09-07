#include "plan.h"
#include <ctype.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

/* JSON-C accepts repeated object members. This small structural walk checks
 * decoded member names before the normal strict parser handles values/types. */
static void whitespace(const char **p) { while (isspace((unsigned char)**p)) (*p)++; }
static json_object *string_token(const char **p) {
    const char *start = *p; char *copy; size_t length; json_object *value;
    if (*(*p)++ != '"') return NULL;
    while (**p && **p != '"') { if (**p == '\\' && (*p)[1]) (*p)++; (*p)++; }
    if (!**p) return NULL;
    (*p)++; length = (size_t)(*p - start);
    copy = malloc(length + 1); if (!copy) return NULL;
    memcpy(copy, start, length); copy[length] = '\0'; value = f_parse_value(copy); free(copy); return value;
}
static bool unique_members(const char **p, unsigned depth) {
    char close; json_object *names = NULL; bool object, ok = false;
    whitespace(p); if (depth > 32 || !**p) return false;
    if (**p == '"') { json_object *s = string_token(p); ok = s != NULL; json_object_put(s); return ok; }
    if (**p != '{' && **p != '[') {
        const char *start = *p;
        while (**p && !strchr(",]} \r\n\t", **p)) (*p)++;
        return *p != start;
    }
    object = **p == '{'; close = object ? '}' : ']'; (*p)++;
    if (object) names = json_object_new_object();
    whitespace(p); if (**p == close) { (*p)++; ok = true; goto done; }
    for (;;) {
        if (object) {
            json_object *key; const char *name;
            whitespace(p); if (**p != '"' || !(key = string_token(p))) goto done;
            name = task_text(key);
            if (!name || f_field(names, name)) { json_object_put(key); goto done; }
            json_object_object_add(names, name, json_object_new_boolean(true)); json_object_put(key);
            whitespace(p); if (*(*p)++ != ':') goto done;
        }
        if (!unique_members(p, depth + 1)) goto done;
        whitespace(p); if (**p == close) { (*p)++; ok = true; goto done; }
        if (**p != ',') goto done;
        (*p)++;
    }
done:
    json_object_put(names); return ok;
}
json_object *plan_read(const char *path) {
    char *text = malloc(PLAN_LIMIT + 1); const char *p = text; json_object *value = NULL;
    FILE *file = fopen(path, "rb"); size_t length;
    if (!text || !file) goto done;
    length = fread(text, 1, PLAN_LIMIT + 1, file);
    if (ferror(file) || length > PLAN_LIMIT || memchr(text, '\0', length)) goto done;
    text[length] = '\0';
    if (unique_members(&p, 0)) { whitespace(&p); if (!*p) value = f_parse(text); }
done:
    if (file) fclose(file);
    free(text); return value;
}
static int key_compare(const void *a, const void *b) { return strcmp(*(const char *const *)a, *(const char *const *)b); }
json_object *plan_canonical(json_object *value) {
    json_object *out; size_t i = 0;
    if (json_object_is_type(value, json_type_object)) {
        size_t n = (size_t)json_object_object_length(value); const char **keys = calloc(n ? n : 1, sizeof(*keys));
        if (!keys) return NULL;
        json_object_object_foreach(value, key, child) { (void)child; keys[i++] = key; }
        qsort(keys, n, sizeof(*keys), key_compare); out = json_object_new_object();
        for (i = 0; i < n; i++) json_object_object_add(out, keys[i], plan_canonical(f_field(value, keys[i])));
        free(keys); return out;
    }
    if (json_object_is_type(value, json_type_array)) {
        out = json_object_new_array();
        for (i = 0; i < json_object_array_length(value); i++) json_object_array_add(out, plan_canonical(json_object_array_get_idx(value, i)));
        return out;
    }
    return json_object_get(value);
}
int plan_digest(json_object *value, char digest[65]) {
    char scratch[] = "/tmp/hydra-plan-hash.XXXXXX"; json_object *canonical = plan_canonical(value); int status = -1;
    if (canonical && mkdtemp(scratch)) { status = task_json_hash(canonical, scratch, digest); f_remove_tree(scratch); }
    json_object_put(canonical); return status;
}
void plan_error(json_object *errors, const char *path, const char *code, const char *message) {
    json_object *entry;
    if (json_object_array_length(errors) >= 128) return;
    entry = json_object_new_object(); f_string_add(entry, "path", path); f_string_add(entry, "code", code); f_string_add(entry, "message", message);
    json_object_array_add(errors, entry);
}
bool plan_text(json_object *value) { const char *s = task_text(value); return s && *s && strlen(s) <= 8192; }
bool plan_list(json_object *value, size_t minimum, size_t maximum) {
    return json_object_is_type(value, json_type_array) && json_object_array_length(value) >= minimum && json_object_array_length(value) <= maximum;
}
bool plan_id(const char *s) {
    bool separator = false;
    if (!wd_name(s)) return false;
    for (; *s; s++) {
        if (*s == '-' || *s == '_') { if (separator) return false; separator = true; }
        else separator = false;
    }
    return !separator;
}
bool plan_has(json_object *array, const char *text) {
    size_t i;
    if (!text || !json_object_is_type(array, json_type_array)) return false;
    for (i = 0; i < json_object_array_length(array); i++) {
        const char *s = task_text(json_object_array_get_idx(array, i)); if (s && !strcmp(s, text)) return true;
    }
    return false;
}
int plan_index(json_object *steps, const char *id) {
    size_t i;
    if (!id || !json_object_is_type(steps, json_type_array)) return -1;
    for (i = 0; i < json_object_array_length(steps); i++) {
        const char *s = f_string(json_object_array_get_idx(steps, i), "id"); if (s && !strcmp(s, id)) return (int)i;
    }
    return -1;
}
