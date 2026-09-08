#ifndef HYDRA_FLEET_SUPPORT_JSON_H
#define HYDRA_FLEET_SUPPORT_JSON_H
#include "fleet/fleet.h"
#include <json-c/json.h>

/* Input strings/objects are borrowed. Parsers and envelope constructors return
 * caller-owned JSON. Accessors f_field/f_text/f_string return borrowed values. */
json_object *f_parse(const char *text);
json_object *f_parse_value(const char *text);
json_object *f_read_json(const char *path, size_t limit);
json_object *f_error(const char *command, const char *code, const char *message);
/* Takes ownership of data, including NULL. */
json_object *f_success(const char *command, json_object *data);
bool f_number_is(json_object *obj, const char *key, int expected);
/* NUL-free text or NULL; lifetime belongs to the JSON value. */
const char *f_text(json_object *value);
const char *f_string(json_object *obj, const char *key);
json_object *f_field(json_object *obj, const char *key);
void f_string_add(json_object *obj, const char *key, const char *value);
int f_emit(json_object *obj);
#endif
