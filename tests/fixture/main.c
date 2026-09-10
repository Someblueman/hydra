#include "fixture.h"
json_object *fx_read(const char *path) {
    char *text = f_read(path, F_LIMIT);
    json_object *value = text ? f_parse_value(text) : NULL;
    free(text);
    fx_require(value != NULL, path);
    return value;
}
json_object *fx_field(json_object *object, const char *name) {
    json_object *value = f_field(object, name);
    fx_require(value != NULL, name);
    return value;
}
void fx_save(const char *path, json_object *object) {
    const char *text = json_object_to_json_string_ext(object, JSON_C_TO_STRING_PLAIN);
    fx_require(text && !f_write(path, text, strlen(text), true), path);
}
void fx_path(char out[4096], const char *base, const char *tail) {
    fx_require(!f_path(out, 4096, base, tail), "path length");
}
int main(int argc, char **argv) {
    if (argc < 2)
        return 2;
    if (!fx_plan(argc, argv) || !fx_events(argc, argv) || !fx_reuse(argc, argv) ||
        !fx_v3(argc, argv))
        return 0;
    fprintf(stderr, "unknown fixture operation: %s\n", argv[1]);
    return 2;
}
