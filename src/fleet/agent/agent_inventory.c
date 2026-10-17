#include "fleet/support/json.h"
#include "fleet/transport/remote.h"
#include "fleet/agent/agent.h"
#include <string.h>

/* Client side of the receiver's agent-inventory capability (hydra agent locate). */
static bool array(json_object *value) { return json_object_is_type(value, json_type_array); }

bool agent_remote_capability(const struct f_remote *remote, const char *capability, unsigned seconds) {
    json_object *reply = f_observe(remote, "handshake", seconds), *caps = f_field(f_field(reply, "data"), "capabilities");
    bool found = false; size_t i;
    if (json_object_get_boolean(f_field(reply, "ok")) && array(caps)) {
        for (i = 0; i < json_object_array_length(caps); i++) {
            const char *cap = f_text(json_object_array_get_idx(caps, i));
            if (cap && !strcmp(cap, capability)) found = true;
        }
    }
    json_object_put(reply);
    return found;
}
json_object *agent_remote_request(const struct f_remote *remote, const char *action, const char *const *args, unsigned seconds) {
    json_object *request = json_object_new_object(), *list = json_object_new_array(), *reply;
    json_object_object_add(request, "protocol", json_object_new_int(F_PROTOCOL));
    f_string_add(request, "action", action);
    for (; args && *args; args++) json_object_array_add(list, json_object_new_string(*args));
    json_object_object_add(request, "args", list);
    reply = f_request(remote, request, seconds);
    json_object_put(request);
    return reply;
}
json_object *agent_inventory_row(json_object *inventory, const char *executable) {
    json_object *agents = f_field(inventory, "agents"); size_t i;
    if (!array(agents) || !executable) return NULL;
    for (i = 0; i < json_object_array_length(agents); i++) {
        json_object *row = json_object_array_get_idx(agents, i);
        const char *name = f_string(row, "executable");
        if (name && !strcmp(name, executable)) return row;
    }
    return NULL;
}
static const char *absolute(const char *path) { return path && path[0] == '/' && strlen(path) < F_PATH ? path : NULL; }
/* The single recordable search candidate of a found_off_path row. */
static const char *off_path(json_object *row) {
    json_object *candidates = f_field(row, "candidates"); size_t i;
    if (!array(candidates)) return NULL;
    for (i = 0; i < json_object_array_length(candidates); i++) {
        json_object *candidate = json_object_array_get_idx(candidates, i);
        const char *source = f_string(candidate, "source");
        if (source && !strcmp(source, "search") && json_object_get_boolean(f_field(candidate, "recordable")))
            return absolute(f_string(candidate, "path"));
    }
    return NULL;
}
const char *agent_inventory_location(json_object *row) {
    const char *status = f_string(row, "status");
    if (!status) return NULL;
    if (!strcmp(status, "on_path")) return absolute(f_string(row, "on_path"));
    if (!strcmp(status, "recorded")) return absolute(f_string(row, "recorded"));
    return !strcmp(status, "found_off_path") ? off_path(row) : NULL;
}
json_object *agent_remote_inventory(const struct f_remote *remote, unsigned seconds) {
    json_object *reply, *data, *copy = NULL;
    if (!agent_remote_capability(remote, "agent-inventory", seconds)) return NULL;
    reply = agent_remote_request(remote, "agent-inventory", NULL, seconds > 120 ? seconds : 120);
    data = f_field(reply, "data");
    if (json_object_get_boolean(f_field(reply, "ok")) && f_string(data, "schema") && !strcmp(f_string(data, "schema"), "agent-inventory") &&
        f_number_is(data, "schema_version", 1) && array(f_field(data, "agents")) && json_object_deep_copy(data, &copy, NULL)) copy = NULL;
    json_object_put(reply);
    return copy;
}
