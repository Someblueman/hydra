#include "precompile.h"
#include <string.h>

/* These constructors return caller-owned values. add_step takes ownership of
 * needs/args, and the fixed bounded item IDs fit the local name buffers. */
static json_object *strings(const char *first, const char *second) {
    json_object *array = json_object_new_array();
    if (first) json_object_array_add(array, json_object_new_string(first));
    if (second) json_object_array_add(array, json_object_new_string(second));
    return array;
}
/* Caller owns the argv array. The shared source-bound shell payload receives
 * only IDs and values already admitted by the finite manifest contract. */
static json_object *payload_command(const char *mode) {
    json_object *argv = strings("sh", "payload.sh");
    json_object_array_add(argv, json_object_new_string(mode));
    return argv;
}
static json_object *compose_command(const struct manifest *manifest, bool staged) {
    json_object *argv = payload_command(staged ? "staged" : "manifest");
    json_object *items = f_field(manifest->value, "items");
    size_t i;
    for (i = 0; i < json_object_array_length(items); i++) {
        json_object *item = json_object_array_get_idx(items, i);
        bool enabled = json_object_get_boolean(f_field(item, "enabled"));
        char value[16];
        if (staged && !enabled) continue;
        snprintf(value, sizeof(value), "%d", json_object_get_int(f_field(item, "value")));
        json_object_array_add(argv, json_object_new_string(enabled ? "enabled" : "skipped"));
        json_object_array_add(argv, json_object_get(f_field(item, "id")));
        json_object_array_add(argv, json_object_new_string(value));
    }
    return argv;
}
static json_object *reference(const char *step, const char *output) {
    json_object *value = json_object_new_object();
    f_string_add(value, step ? "step" : "input", step ? step : output);
    if (step) f_string_add(value, "output", output);
    return value;
}
static json_object *output_file(const char *type, const char *path, int maximum) {
    json_object *value = json_object_new_object();
    f_string_add(value, "type", type); f_string_add(value, "path", path);
    json_object_object_add(value, "max_bytes", json_object_new_int(maximum));
    return value;
}
static void add_step(json_object *steps, const char *id, const char *role, const char *kind,
                     json_object *needs, json_object *args) {
    json_object *step = json_object_new_object();
    f_string_add(step, "id", id); f_string_add(step, "role", role); f_string_add(step, "kind", kind);
    json_object_object_add(step, "needs", needs); json_object_object_add(step, "args", args);
    json_object_object_add(step, "writes", json_object_new_array()); json_object_array_add(steps, step);
}
static void spawn(json_object *steps, const char *id, const char *head) {
    json_object *args = json_object_new_object();
    f_string_add(args, "branch", head); f_string_add(args, "terminal_mode", "headless");
    add_step(steps, id, "work", "spawn", strings(NULL, NULL), args);
}
static void execute(json_object *steps, const char *id, const char *role, const char *head,
                    json_object *needs, json_object *argv) {
    json_object *args = json_object_new_object();
    f_string_add(args, "head", head); json_object_object_add(args, "argv", argv);
    json_object_object_add(args, "timeout", json_object_new_int(30));
    add_step(steps, id, role, "exec", needs, args);
}
static json_object *base_plan(bool staged) {
    json_object *plan = f_parse("{\"schema_version\":1,\"context\":[],\"questions\":[],\"steps\":[],"
        "\"envelope\":{\"hosts\":[\"local\"],\"tools\":[\"sh\",\"./plan-example\"],\"effects\":[\"worktree\",\"execute\"],"
        "\"writes\":[],\"parallelism\":4,\"timeout_seconds\":600,\"artifact_bytes\":65536,\"max_heads\":10,"
        "\"disk_mb\":1,\"retry_budget\":0,\"repair_budget\":0},"
        "\"data\":{\"schema_version\":1,\"inputs\":{},\"steps\":{}}}");
    if (!plan) return NULL;
    f_string_add(plan, "id", staged ? "staged-stage2" : "manifest-map");
    f_string_add(plan, "objective", staged ?
        "Execute the exact bounded members selected by a fresh stage-1 finding and verify the fixed join." :
        "Compute bounded squares for enabled manifest members and explicitly report skipped members.");
    json_object_object_add(plan, "assumptions", strings(staged ?
        "Stage 2 membership is frozen by the accepted stage-1 finding." :
        "The manifest is validated and bound before compilation; membership is finite and fixed.", NULL));
    if (staged) json_object_object_add(f_field(plan, "envelope"), "timeout_seconds", json_object_new_int(300));
    return plan;
}
static void acceptance(json_object *plan, bool staged) {
    const char *requirement = staged ? "stage2" : "membership";
    const char *criterion = staged ? "Only the fresh stage-1 selection is executed and every selected result is verified." :
        "Every manifest member is executed or explicitly skipped and enabled values are squared.";
    json_object *deliverable = f_parse("{\"id\":\"report\",\"step\":\"compose\",\"output\":\"report\",\"destination\":\"run-artifact\"}");
    json_object *check = f_parse("{\"id\":\"check\",\"method\":\"executable\",\"step\":\"check\",\"input\":\"subject\",\"report\":\"check\",\"deliverable\":\"report\"}");
    json_object *req = f_parse("{\"deliverable\":\"report\",\"check\":\"check\"}");
    json_object *obligation = f_parse("{\"intent_ref\":\"objective\",\"subject\":{\"deliverable\":\"report\",\"step\":\"compose\",\"output\":\"report\"},"
        "\"evaluation\":{\"method\":\"executable\",\"check\":\"check\"},\"environment\":{\"hosts\":[\"local\"],\"tools\":[\"./plan-example\"],\"effects\":[\"execute\"]},"
        "\"completion_rule\":\"verdict=pass\"}");
    json_object *deliverables = json_object_new_array(), *checks = json_object_new_array();
    json_object *requirements = json_object_new_array(), *obligations = json_object_new_array();
    f_string_add(deliverable, "description", staged ? "Stage 2 selected-member report" : "Square results and explicit skips");
    f_string_add(check, "definition", staged ? "{\"predicate\":\"equals\",\"cases\":[{\"id\":\"stage2-membership\",\"expected\":\"pass\"}]}" :
        "{\"predicate\": \"equals\", \"cases\": [{\"id\": \"membership-case\", \"expected\": \"pass\"}]}");
    f_string_add(req, "id", requirement); f_string_add(req, "criterion", criterion);
    f_string_add(obligation, "id", staged ? "stage2-check" : "membership-check");
    f_string_add(obligation, "requirement", requirement); f_string_add(obligation, "criterion", criterion);
    json_object_object_add(obligation, "required_evidence", f_parse_value(staged ?
        "[\"subject_sha256\",\"verdict\",\"evidence\"]" :
        "[\"subject_sha256\",\"verdict\",\"evidence\",\"case_inventory\",\"observations\",\"raw_evidence_sha256\"]"));
    json_object_object_add(obligation, "limitations", strings(staged ? "Finite staged fixture." :
        "Finite arithmetic contract; no runtime graph expansion.", NULL));
    json_object_array_add(deliverables, deliverable); json_object_array_add(checks, check);
    json_object_array_add(requirements, req); json_object_array_add(obligations, obligation);
    json_object_object_add(plan, "deliverables", deliverables); json_object_object_add(plan, "checks", checks);
    json_object_object_add(plan, "requirements", requirements); json_object_object_add(plan, "obligations", obligations);
}
static void member_steps(json_object *plan, const struct manifest *manifest, const char *prefix, json_object *needs) {
    json_object *items = f_field(manifest->value, "items"), *steps = f_field(plan, "steps");
    json_object *data_steps = f_field(f_field(plan, "data"), "steps");
    size_t i;
    for (i = 0; i < json_object_array_length(items); i++) {
        json_object *item = json_object_array_get_idx(items, i);
        char id[64], head[64];
        if (!json_object_get_boolean(f_field(item, "enabled"))) continue;
        snprintf(id, sizeof(id), "spawn-item-%s", f_string(item, "id"));
        snprintf(head, sizeof(head), "%s-item-%s", prefix, f_string(item, "id"));
        spawn(steps, id, head);
    }
    for (i = 0; i < json_object_array_length(items); i++) {
        json_object *item = json_object_array_get_idx(items, i), *argv, *outputs, *data;
        char id[64], dependency[64], head[64], path[64], value[16];
        if (!json_object_get_boolean(f_field(item, "enabled"))) continue;
        snprintf(id, sizeof(id), "work-item-%s", f_string(item, "id"));
        snprintf(dependency, sizeof(dependency), "spawn-item-%s", f_string(item, "id"));
        snprintf(head, sizeof(head), "%s-item-%s", prefix, f_string(item, "id"));
        snprintf(path, sizeof(path), "result-%s.json", f_string(item, "id"));
        snprintf(value, sizeof(value), "%d", json_object_get_int(f_field(item, "value")));
        argv = payload_command("worker");
        json_object_array_add(argv, json_object_get(f_field(item, "id"))); json_object_array_add(argv, json_object_new_string(value));
        execute(steps, id, "work", head, strings(dependency, NULL), argv);
        json_object_array_add(needs, json_object_new_string(id));
        outputs = json_object_new_object(); data = json_object_new_object();
        json_object_object_add(outputs, "result", output_file("file", path, 128));
        json_object_object_add(data, "outputs", outputs); json_object_object_add(data_steps, id, data);
    }
}
static void handoffs(json_object *plan, const struct manifest *manifest, bool staged) {
    json_object *data = f_field(plan, "data"), *inputs = f_field(data, "inputs"), *steps = f_field(data, "steps");
    json_object *items = f_field(manifest->value, "items");
    json_object *compose = json_object_new_object(), *check = json_object_new_object();
    json_object *compose_inputs = json_object_new_object(), *check_inputs = json_object_new_object();
    json_object *compose_outputs = json_object_new_object(), *check_outputs = json_object_new_object();
    size_t i;
    json_object_object_add(inputs, "manifest", output_file("file", "manifest.json", 4096));
    json_object_object_add(check_inputs, "manifest", reference(NULL, "manifest"));
    if (staged) {
        json_object_object_add(inputs, "finding", output_file("file", "finding.json", 8192));
        json_object_object_add(check_inputs, "finding", reference(NULL, "finding"));
    }
    for (i = 0; i < json_object_array_length(items); i++) {
        json_object *item = json_object_array_get_idx(items, i);
        char name[64], step[64];
        if (!json_object_get_boolean(f_field(item, "enabled"))) continue;
        snprintf(name, sizeof(name), "member-%s", f_string(item, "id"));
        snprintf(step, sizeof(step), "work-item-%s", f_string(item, "id"));
        json_object_object_add(compose_inputs, name, reference(step, "result"));
    }
    json_object_object_add(check_inputs, "subject", reference("compose", "report"));
    json_object_object_add(compose_outputs, "report", output_file("file", "report.json", staged ? 8192 : 4096));
    json_object_object_add(check_outputs, "check", output_file("object", "check.json", 8192));
    json_object_object_add(compose, "inputs", compose_inputs); json_object_object_add(compose, "outputs", compose_outputs);
    json_object_object_add(check, "inputs", check_inputs); json_object_object_add(check, "outputs", check_outputs);
    json_object_object_add(steps, "compose", compose); json_object_object_add(steps, "check", check);
}
json_object *precompile_lower(const struct manifest *manifest, bool staged) {
    json_object *plan = base_plan(staged), *steps, *needs, *compose;
    const char *prefix = staged ? "staged" : "manifest";
    char compose_head[32], check_head[32];
    if (!plan) return NULL;
    compose = compose_command(manifest, staged);
    if (!compose) { json_object_put(plan); return NULL; }
    steps = f_field(plan, "steps"); needs = strings(staged ? "spawn-compose" : NULL, NULL);
    member_steps(plan, manifest, prefix, needs);
    if (!staged) json_object_array_add(needs, json_object_new_string("spawn-compose"));
    snprintf(compose_head, sizeof(compose_head), "%s-compose", prefix);
    snprintf(check_head, sizeof(check_head), "%s-check", prefix);
    spawn(steps, "spawn-compose", compose_head);
    execute(steps, "compose", "compose", compose_head, needs, compose);
    spawn(steps, "spawn-check", check_head);
    execute(steps, "check", "verify", check_head, strings("spawn-check", "compose"), strings("./plan-example", staged ? "staged-check" : "manifest-check"));
    handoffs(plan, manifest, staged); acceptance(plan, staged);
    return plan;
}
