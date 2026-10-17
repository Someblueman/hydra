#include "fleet/placement/placement.h"
#include "fleet/support/files.h"
#include "fleet/transport/remote.h"
#include <string.h>

static bool advertises(json_object *caps, const char *name) {
    size_t i;
    for (i = 0; json_object_is_type(caps, json_type_array) && i < json_object_array_length(caps); i++) {
        const char *cap = f_text(json_object_array_get_idx(caps, i));
        if (cap && !strcmp(cap, name)) return true;
    }
    return false;
}
static json_object *copy_strings(json_object *array, const char *member, size_t limit) {
    json_object *out = json_object_new_array(); size_t i;
    for (i = 0; json_object_is_type(array, json_type_array) && i < json_object_array_length(array) && i < limit; i++) {
        json_object *item = json_object_array_get_idx(array, i);
        const char *text = member ? f_string(item, member) : f_text(item);
        if (text && strlen(text) < F_PATH) json_object_array_add(out, json_object_new_string(text));
    }
    return out;
}
static bool nonnegative(json_object *object, const char *key) {
    json_object *value = f_field(object, key);
    return json_object_is_type(value, json_type_int) && json_object_get_int64(value) >= 0;
}
/* The receiver's own `hydra admission status --json`: totals plus the number of
 * reserved slots whose required labels equal this task's class. */
static json_object *admission_summary(json_object *data, const char *required_labels) {
    const char *numbers[] = {"host_limit", "project_limit", "disk_floor_kb", "disk_free_kb", "queue_limit", "reserved", "queued", "observed_at", NULL};
    json_object *summary, *requests = f_field(data, "requests"); size_t i; int64_t class_reserved = 0;
    for (i = 0; numbers[i]; i++) if (!nonnegative(data, numbers[i])) return NULL;
    if (!f_string(data, "labels") || strlen(f_string(data, "labels")) > 2048 || !json_object_is_type(requests, json_type_array)) return NULL;
    summary = json_object_new_object();
    for (i = 0; numbers[i]; i++) json_object_object_add(summary, numbers[i], json_object_new_int64(json_object_get_int64(f_field(data, numbers[i]))));
    f_string_add(summary, "labels", f_string(data, "labels"));
    for (i = 0; i < json_object_array_length(requests); i++) {
        json_object *request = json_object_array_get_idx(requests, i);
        const char *state = f_string(request, "state"), *labels = f_string(request, "required_labels");
        if (state && labels && (!strcmp(state, "reserved") || !strcmp(state, "unknown")) && !strcmp(labels, required_labels)) class_reserved++;
    }
    json_object_object_add(summary, "class_reserved", json_object_new_int64(class_reserved));
    json_object_object_add(summary, "requests_observed", json_object_new_int64((int64_t)json_object_array_length(requests)));
    return summary;
}
json_object *placement_observe(const char *alias, const char *required_labels, unsigned seconds, int64_t now, bool drained) {
    json_object *out = json_object_new_object(), *handshake, *data, *request, *reply = NULL, *admission = NULL;
    struct f_remote remote;
    f_string_add(out, "host", alias);
    json_object_object_add(out, "observed_at", json_object_new_int64(now));
    json_object_object_add(out, "drained", json_object_new_boolean(drained));
    json_object_object_add(out, "compatible", json_object_new_boolean(false));
    if (f_remote_load(alias, &remote)) { f_string_add(out, "error", "alias_invalid"); goto empty; }
    handshake = f_observe(&remote, "handshake", seconds); data = f_field(handshake, "data");
    if (!json_object_get_boolean(f_field(handshake, "ok"))) {
        const char *code = f_string(f_field(handshake, "error"), "code");
        f_string_add(out, "error", code ? code : "offline"); json_object_put(handshake); goto empty;
    }
    json_object_object_add(out, "error", json_object_new_null());
    json_object_object_add(out, "compatible", json_object_new_boolean(f_handshake_compatible(data)));
    f_string_add(out, "hydra_version", f_string(data, "hydra_version") ? f_string(data, "hydra_version") : "");
    json_object_object_add(out, "capabilities", copy_strings(f_field(data, "capabilities"), NULL, 128));
    json_object_object_add(out, "projects", copy_strings(f_field(data, "projects"), "path", 256));
    if (f_string(f_field(data, "platform"), "os") && f_string(f_field(data, "platform"), "arch")) {
        json_object *platform = json_object_new_object();
        f_string_add(platform, "os", f_string(f_field(data, "platform"), "os")); f_string_add(platform, "arch", f_string(f_field(data, "platform"), "arch"));
        json_object_object_add(out, "platform", platform);
    } else json_object_object_add(out, "platform", json_object_new_null());
    if (advertises(f_field(data, "capabilities"), "admission")) {
        json_object *args = json_object_new_array();
        request = json_object_new_object();
        json_object_object_add(request, "protocol", json_object_new_int(F_PROTOCOL)); f_string_add(request, "action", "admission");
        json_object_array_add(args, json_object_new_string("status")); json_object_array_add(args, json_object_new_string("--json"));
        json_object_object_add(request, "args", args);
        reply = f_request(&remote, request, seconds); json_object_put(request);
        if (json_object_get_boolean(f_field(reply, "ok"))) admission = admission_summary(f_field(reply, "data"), required_labels);
        else f_string_add(out, "admission_error", f_string(f_field(reply, "error"), "code") ? f_string(f_field(reply, "error"), "code") : "admission_unavailable");
        json_object_put(reply);
    }
    json_object_object_add(out, "admission", admission ? admission : json_object_new_null());
    json_object_put(handshake);
    return out;
empty:
    json_object_object_add(out, "capabilities", json_object_new_array());
    json_object_object_add(out, "projects", json_object_new_array());
    json_object_object_add(out, "platform", json_object_new_null());
    json_object_object_add(out, "admission", json_object_new_null());
    return out;
}
