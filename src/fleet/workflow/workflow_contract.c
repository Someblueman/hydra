#include "fleet/workflow/workflow_contract.h"
#include "fleet/task/task.h"
#include <string.h>

static bool text(json_object *v) {
    const char *s = f_text(v);
    return s && *s && strlen(s) <= 256 && strlen(s) == (size_t)json_object_get_string_len(v);
}
static bool scalar(json_object *v, const char *type) {
    if (!strcmp(type, "string")) return text(v);
    if (!strcmp(type, "strings")) {
        if (!json_object_is_type(v, json_type_array) || !json_object_array_length(v) || json_object_array_length(v) > 64) return false;
        for (size_t i = 0; i < json_object_array_length(v); i++) if (!text(json_object_array_get_idx(v, i))) return false;
        return true;
    }
    if (!strcmp(type, "integer")) return json_object_is_type(v, json_type_int);
    if (!strcmp(type, "boolean")) return json_object_is_type(v, json_type_boolean);
    return false;
}
static bool field_schema(json_object *field) {
    const char *const keys[] = {"type", "unit", "equals", "minimum", "maximum", NULL};
    const char *type = f_string(field, "type");
    if (!task_keys(field, keys) || !type || (strcmp(type, "string") && strcmp(type, "strings") && strcmp(type, "integer") && strcmp(type, "boolean"))) return false;
    if (f_field(field, "unit") && (strcmp(type, "integer") || !text(f_field(field, "unit")))) return false;
    if (f_field(field, "equals") && !scalar(f_field(field, "equals"), type)) return false;
    const char *bounds[] = {"minimum", "maximum"};
    for (size_t i = 0; i < 2; i++)
        if (f_field(field, bounds[i]) && (strcmp(type, "integer") || !json_object_is_type(f_field(field, bounds[i]), json_type_int))) return false;
    return !f_field(field, "minimum") || !f_field(field, "maximum") ||
        json_object_get_int64(f_field(field, "minimum")) <= json_object_get_int64(f_field(field, "maximum"));
}
static bool cardinality(json_object *contract, bool array) {
    const char *const keys[] = {"min", "max", NULL};
    json_object *c = f_field(contract, "cardinality");
    if (!task_keys(c, keys) || !json_object_is_type(f_field(c, "min"), json_type_int) || !json_object_is_type(f_field(c, "max"), json_type_int)) return false;
    int64_t lo = json_object_get_int64(f_field(c, "min")), hi = json_object_get_int64(f_field(c, "max"));
    return array ? lo >= 1 && hi >= lo && hi <= 1024 : lo == 1 && hi == 1;
}
bool wc_schema(json_object *contract, const char *type) {
    const char *const keys[] = {"schema", "version", "type", "cardinality", "fields", NULL};
    if (!type || !task_keys(contract, keys) || !f_string(contract, "type") || strcmp(type, f_string(contract, "type")) || !wd_name(f_string(contract, "schema")) ||
        !json_object_is_type(f_field(contract, "version"), json_type_int) || json_object_get_int64(f_field(contract, "version")) < 1 ||
        !cardinality(contract, !strcmp(type, "array"))) return false;
    json_object *fields = f_field(contract, "fields");
    if (!json_object_is_type(fields, json_type_object) || json_object_object_length(fields) > 64) return false;
    if (!strcmp(type, "file")) return !json_object_object_length(fields);
    if (strcmp(type, "object") && strcmp(type, "array")) return false;
    if (!json_object_object_length(fields)) return false;
    json_object_object_foreach(fields, name, field) {
        if (!wd_name(name) || !field_schema(field)) return false;
    }
    return true;
}
static bool field_value(json_object *field, json_object *v) {
    const char *unit = f_string(field, "unit");
    if (unit) {
        const char *const keys[] = {"value", "unit", NULL};
        if (!task_keys(v, keys) || !f_string(v, "unit") || strcmp(unit, f_string(v, "unit"))) return false;
        v = f_field(v, "value");
    }
    if (!scalar(v, f_string(field, "type"))) return false;
    if (f_field(field, "equals") && !json_object_equal(v, f_field(field, "equals"))) return false;
    if (f_field(field, "minimum") && json_object_get_int64(v) < json_object_get_int64(f_field(field, "minimum"))) return false;
    return !f_field(field, "maximum") || json_object_get_int64(v) <= json_object_get_int64(f_field(field, "maximum"));
}
static bool object_value(json_object *fields, json_object *v) {
    if (!json_object_is_type(v, json_type_object) || json_object_object_length(v) != json_object_object_length(fields)) return false;
    json_object_object_foreach(fields, name, field) {
        if (!field_value(field, f_field(v, name))) return false;
    }
    return true;
}
bool wc_value(json_object *contract, json_object *value) {
    if (!contract) return true;
    if (json_object_is_type(value, json_type_array)) {
        size_t n = json_object_array_length(value); json_object *c = f_field(contract, "cardinality");
        if (n < (size_t)json_object_get_int64(f_field(c, "min")) || n > (size_t)json_object_get_int64(f_field(c, "max"))) return false;
        for (size_t i = 0; i < n; i++) if (!object_value(f_field(contract, "fields"), json_object_array_get_idx(value, i))) return false;
        return true;
    }
    return object_value(f_field(contract, "fields"), value);
}
