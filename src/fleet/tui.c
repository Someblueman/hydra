#include "fleet/support/json.h"
#include "fleet/transport/remote.h"
#include "fleet/cli.h"
#include "fleet/fleet.h"
#include <stdio.h>
#include <string.h>

/* Fleet is a separate internal adapter. Control bytes cannot enter terminal rows. */
static void field(const char *value) {
    const unsigned char *p = (const unsigned char *)(value ? value : "");
    for (; *p; p++) putchar(*p < 32 || *p == 127 ? '?' : *p);
}

static void field_limit(const char *value, size_t limit) {
    const unsigned char *p = (const unsigned char *)(value ? value : "");
    size_t used = 0;
    if (limit == 0) return;
    for (; *p && used + 1 < limit; p++, used++) putchar(*p < 32 || *p == 127 ? '?' : *p);
}

static bool routable(const char *value, size_t limit) {
    const unsigned char *p = (const unsigned char *)value;
    if (!value || strlen(value) >= limit) return false;
    for (; *p; p++) if (*p < 32 || *p == 127) return false;
    return true;
}

static const char *text_value(json_object *object, const char *key) {
    const char *value = f_string(object, key);
    return value && value[0] ? value : "-";
}

static const char *nested_text(json_object *object, const char *outer, const char *inner) {
    return text_value(f_field(object, outer), inner);
}

static void json_token(json_object *object, const char *key, char buffer[40]) {
    json_object *value = f_field(object, key);
    if (json_object_is_type(value, json_type_int)) {
        (void)snprintf(buffer, 40, "%lld", (long long)json_object_get_int64(value));
    } else {
        const char *text = f_text(value);
        (void)snprintf(buffer, 40, "%s", text && text[0] ? text : "-");
    }
}

static json_object *host_find(json_object *hosts, const char *name) {
    size_t i;
    if (!json_object_is_type(hosts, json_type_array)) return NULL;
    for (i = 0; i < json_object_array_length(hosts); i++) {
        json_object *host = json_object_array_get_idx(hosts, i);
        if (f_string(host, "host") && name && !strcmp(f_string(host, "host"), name)) return host;
    }
    return NULL;
}

static bool host_seen(json_object *hosts, const char *name) {
    return host_find(hosts, name) != NULL;
}

static void head_records(json_object *heads, const char *name) {
    size_t j;
    for (j = 0; json_object_is_type(heads, json_type_array) && j < json_object_array_length(heads); j++) {
        json_object *head = json_object_array_get_idx(heads, j);
        const char *keys[] = {"project_path", "branch", "head_id", "current_instance", "desired_state", NULL}; size_t k;
        if (!routable(name, 128) || !routable(f_string(head, "project_path"), 768) || !routable(f_string(head, "branch"), 256) || !routable(f_string(head, "head_id"), 256) || !routable(f_string(head, "current_instance"), 256)) {
            printf("R\tfleet\t"); field(name); puts("\thead exceeds TUI text bounds\tobserved\tuse CLI"); continue;
        }
        printf("F\t"); field(name);
        for (k = 0; keys[k]; k++) { putchar('\t'); field(f_string(head, keys[k])); }
        putchar('\n');
    }
}

static void host_record_v3(json_object *listed, json_object *observed, json_object *heads, const char *name) {
    json_object *data = observed && json_object_get_boolean(f_field(observed, "ok")) ? f_field(observed, "data") : NULL;
    json_object *connection = f_field(data, "connection"), *freshness = f_field(data, "freshness");
    const bool listed_ok = listed && json_object_get_boolean(f_field(listed, "ok"));
    const bool observed_ok = observed && json_object_get_boolean(f_field(observed, "ok"));
    char confirmed[40], age[40];
    json_token(data, "last_confirmed_at", confirmed);
    json_token(freshness, "age_seconds", age);
    /* A cached observation remains a response: transport state is carried in
     * the connection/freshness columns and must not relabel execution state. */
    printf("T\t"); field_limit(name, 128); printf("\t%s\t%zu\t", listed_ok || observed_ok ? "responded" : "failed",
        json_object_is_type(heads, json_type_array) ? json_object_array_length(heads) : 0);
    if (listed_ok) field("-");
    else if (listed) field(f_string(f_field(listed, "error"), "code"));
    else if (observed) field(f_string(f_field(observed, "error"), "code"));
    else field("unavailable");
    printf("\t"); field_limit(data ? text_value(connection, "state") : "unknown", 40);
    printf("\t"); field_limit(data ? text_value(freshness, "state") : "unknown", 32);
    printf("\t"); field_limit(confirmed, 40); printf("\t"); field_limit(age, 40); putchar('\n');
}

static void task_record_line(json_object *data, json_object *task, const char *name) {
    json_object *steps = f_field(task, "steps"), *step = NULL, *waiting = f_field(task, "waiting"), *owner = f_field(task, "execution_owner");
    const char *step_id = text_value(task, "step_id"), *attempt_id = text_value(task, "attempt_id"), *profile = text_value(task, "agent_profile");
    char observed_at[40], confirmed[40]; size_t pending = 0;
    if (!routable(name, 128)) return;
    if (json_object_is_type(steps, json_type_array) && json_object_array_length(steps)) step = json_object_array_get_idx(steps, 0);
    if (!strcmp(step_id, "-")) step_id = text_value(step, "step_id");
    if (!strcmp(attempt_id, "-")) attempt_id = text_value(step, "attempt_id");
    if (!strcmp(profile, "-")) profile = text_value(step, "agent_profile");
    if (json_object_is_type(f_field(task, "pending_requests"), json_type_array)) pending = json_object_array_length(f_field(task, "pending_requests"));
    json_token(data, "receiver_observed_at", observed_at); json_token(data, "last_confirmed_at", confirmed);
    printf("O\t"); field_limit(name, 128); printf("\t"); field_limit(text_value(task, "task_id"), 128);
    printf("\t"); field_limit(text_value(task, "run_id"), 128); printf("\t"); field_limit(step_id, 128);
    printf("\t"); field_limit(attempt_id, 128); printf("\t"); field_limit(text_value(task, "workspace"), 768);
    printf("\t"); field_limit(profile, 128); printf("\t"); field_limit(text_value(owner, "state"), 64);
    printf("\t"); field_limit(text_value(task, "execution_state"), 64); printf("\t"); field_limit(text_value(waiting, "reason"), 32);
    printf("\t"); field_limit(text_value(waiting, "detail"), 256); printf("\t"); field_limit(text_value(waiting, "next_action"), 256);
    printf("\t"); field_limit(observed_at, 40); printf("\t"); field_limit(confirmed, 40);
    printf("\t"); field_limit(nested_text(data, "freshness", "state"), 32); printf("\t%zu", pending);
    printf("\t"); field_limit(text_value(f_field(task, "result_collection"), "state"), 32);
    printf("\t"); field_limit(text_value(f_field(task, "verification"), "state"), 32);
    printf("\t"); field_limit(text_value(task, "spec_sha256"), 65);
    printf("\t"); field_limit(text_value(task, "cancellation"), 32);
    printf("\t"); field_limit(text_value(task, "cancellation_scope"), 64);
    printf("\t"); json_token(task, "cancel_requested_at", observed_at); field_limit(observed_at, 40);
    printf("\t");
    if (json_object_is_type(f_field(task, "pending_requests"), json_type_array) && json_object_array_length(f_field(task, "pending_requests")))
        field_limit(text_value(json_object_array_get_idx(f_field(task, "pending_requests"), 0), "request_id"), 128);
    else field("-");
    putchar('\n');
}

static void task_records(json_object *observed, const char *name) {
    json_object *data = f_field(observed, "data"), *tasks = f_field(data, "tasks"); size_t i;
    if (!observed || !json_object_get_boolean(f_field(observed, "ok")) || !json_object_is_type(tasks, json_type_array)) return;
    for (i = 0; i < json_object_array_length(tasks); i++) {
        json_object *task = json_object_array_get_idx(tasks, i);
        if (json_object_is_type(task, json_type_object)) task_record_line(data, task, name);
    }
}

static void recovery_record(const char *name, json_object *host) {
    if (!host || json_object_get_boolean(f_field(host, "ok"))) return;
    printf("R\tobservation\t"); field(name); printf("\t");
    field(f_string(f_field(host, "error"), "code")); puts("\tobserved\tinspect cached snapshot");
}

static int fleet_tui_v1(unsigned seconds, unsigned jobs) {
    json_object *result = f_aggregate("list", seconds, jobs), *hosts = f_field(f_field(result, "data"), "hosts"); size_t i;
    puts("HYDRA_FLEET_TUI\t1");
    if (!json_object_is_type(hosts, json_type_array)) puts("R\tfleet\tall\tlimit or adapter failure\tobserved\tinspect CLI");
    for (i = 0; json_object_is_type(hosts, json_type_array) && i < json_object_array_length(hosts); i++) {
        json_object *host = json_object_array_get_idx(hosts, i), *heads = f_field(f_field(host, "data"), "heads");
        const char *name = f_string(host, "host");
        if (!json_object_get_boolean(f_field(host, "ok"))) {
            printf("R\tfleet\t"); field(name); printf("\t"); field(f_string(f_field(host, "error"), "code")); puts("\tobserved\treconcile"); continue;
        }
        head_records(heads, name);
    }
    json_object_put(result); return ferror(stdout) ? 1 : 0;
}

static int fleet_tui_v3(unsigned seconds, unsigned jobs) {
    json_object *listed = f_aggregate("list", seconds, jobs), *observed = f_observation_aggregate(seconds, jobs);
    json_object *list_hosts = f_field(f_field(listed, "data"), "hosts"), *observed_hosts = f_field(f_field(observed, "data"), "hosts"); size_t i;
    puts("HYDRA_FLEET_TUI\t3");
    if (!json_object_is_type(list_hosts, json_type_array) && !json_object_is_type(observed_hosts, json_type_array))
        puts("R\tfleet\tall\tlimit or adapter failure\tobserved\tinspect CLI");
    for (i = 0; json_object_is_type(list_hosts, json_type_array) && i < json_object_array_length(list_hosts); i++) {
        json_object *listed_host = json_object_array_get_idx(list_hosts, i), *observed_host;
        const char *name = f_string(listed_host, "host"); json_object *heads = f_field(f_field(listed_host, "data"), "heads");
        if (!name) continue;
        observed_host = host_find(observed_hosts, name);
        host_record_v3(listed_host, observed_host, heads, name); head_records(heads, name); task_records(observed_host, name); recovery_record(name, observed_host);
    }
    for (i = 0; json_object_is_type(observed_hosts, json_type_array) && i < json_object_array_length(observed_hosts); i++) {
        json_object *observed_host = json_object_array_get_idx(observed_hosts, i); const char *name = f_string(observed_host, "host");
        if (!name || host_seen(list_hosts, name)) continue;
        host_record_v3(NULL, observed_host, NULL, name); task_records(observed_host, name); recovery_record(name, observed_host);
    }
    json_object_put(listed); json_object_put(observed); return ferror(stdout) ? 1 : 0;
}

int f_tui_data(unsigned seconds, unsigned jobs, bool include_hosts) {
    if (!include_hosts) return fleet_tui_v1(seconds, jobs);
    return fleet_tui_v3(seconds, jobs);
}
