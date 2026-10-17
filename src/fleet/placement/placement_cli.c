#define _XOPEN_SOURCE 700
#include "fleet/placement/placement.h"
#include "fleet/support/files.h"
#include "fleet/task/task.h"
#include <errno.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

static json_object *usage(void) {
    json_object *data = json_object_new_object();
    f_string_add(data, "usage", "fleet placement explain TASK_ID; fleet placement replay TASK_ID; fleet placement withdraw TASK_ID [--timeout SECONDS]; fleet placement drain HOST; fleet placement undrain HOST; fleet placement policy; fleet task submit --placement auto|HOST ... (see fleet task help)");
    return f_success("fleet-placement-help", data);
}
static json_object *explain(const char *task_id) {
    json_object *record = placement_record_read(task_id); char directory[F_PATH];
    if (!record) return f_error("fleet-placement", "not_found", "no valid placement record exists for this task ID; only --placement submissions record decisions");
    if (!placement_directory(task_id, directory, false)) f_string_add(record, "record_directory", directory);
    return f_success("fleet-placement", record);
}
/* Replay re-derives every automatic round from its recorded observations and
 * the recorded policy; the candidates and decision must be identical. */
static json_object *replay(const char *task_id) {
    json_object *record = placement_record_read(task_id), *generations, *data; size_t g, r, rounds = 0, automatic = 0;
    if (!record) return f_error("fleet-placement", "not_found", "no valid placement record exists for this task ID");
    generations = f_field(record, "generations");
    for (g = 0; json_object_is_type(generations, json_type_array) && g < json_object_array_length(generations); g++) {
        json_object *list = f_field(json_object_array_get_idx(generations, g), "rounds");
        for (r = 0; json_object_is_type(list, json_type_array) && r < json_object_array_length(list); r++) {
            json_object *round = json_object_array_get_idx(list, r), *decision = NULL, *candidates; const char *mode = f_string(round, "mode"); bool identical;
            rounds++;
            if (!mode || strcmp(mode, "auto")) continue;
            automatic++;
            candidates = placement_decide(f_field(record, "inputs"), f_field(round, "observations"), f_field(record, "policy"),
                                          f_field(round, "excluded_hosts"), f_field(round, "separation_hosts"), json_object_get_int64(f_field(round, "decided_at")), &decision);
            identical = json_object_equal(candidates, f_field(round, "candidates")) && json_object_equal(decision, f_field(round, "decision"));
            json_object_put(candidates); json_object_put(decision);
            if (!identical) {
                json_object *error = f_error("fleet-placement", "replay_mismatch", "re-deriving the decision from the recorded observations and policy gives a different result; the record or the policy implementation changed");
                json_object_object_add(f_field(error, "error"), "generation", json_object_new_int64((int64_t)g + 1));
                json_object_object_add(f_field(error, "error"), "round", json_object_new_int64((int64_t)r + 1));
                json_object_put(record); return error;
            }
        }
    }
    data = json_object_new_object(); f_string_add(data, "task_id", task_id);
    json_object_object_add(data, "identical", json_object_new_boolean(true));
    json_object_object_add(data, "rounds", json_object_new_int64((int64_t)rounds)); json_object_object_add(data, "automatic_rounds", json_object_new_int64((int64_t)automatic));
    json_object_object_add(data, "policy_version", json_object_get(f_field(f_field(record, "policy"), "policy_version")));
    f_string_add(data, "record_sha256", f_string(record, "sha256"));
    json_object_put(record);
    return f_success("fleet-placement-replay", data);
}
static bool uncertain_code(const char *code) {
    const char *codes[] = {"outcome_unknown", "timeout", "offline", "cancelled", "invalid_response", "remote_failed", "output_limit", NULL}; size_t i;
    for (i = 0; code && codes[i]; i++) if (!strcmp(code, codes[i])) return true;
    return false;
}
/* Rebalancing accepted-not-started work: the old receiver records the
 * withdrawal durably before the submitter may open a new generation. A lost
 * response blocks transfer until a rerun confirms it. */
static json_object *withdraw(const char *task_id, const char *timeout) {
    json_object *record = placement_record_read(task_id), *generations, *generation, *withdrawal, *response, *data; const char *host, *code = NULL, *state;
    char *argv[] = {"withdraw", NULL, "--id", (char *)task_id, "--timeout", (char *)timeout, NULL};
    if (!record) return f_error("fleet-placement", "not_found", "no valid placement record exists for this task ID");
    generations = f_field(record, "generations");
    generation = json_object_array_length(generations) ? json_object_array_get_idx(generations, json_object_array_length(generations) - 1) : NULL;
    host = f_string(generation, "host");
    if (!host) { json_object_put(record); return f_error("fleet-placement", "invalid_state", "the current generation has no assigned receiver; nothing to withdraw"); }
    state = f_string(f_field(generation, "withdrawal"), "state");
    if (state && !strcmp(state, "confirmed")) { json_object_put(record); return f_error("fleet-placement", "invalid_state", "the current generation is already withdrawn; submit again with --placement auto to open the next generation"); }
    withdrawal = json_object_new_object(); f_string_add(withdrawal, "state", "requested");
    json_object_object_add(withdrawal, "requested_at", json_object_new_int64((int64_t)time(NULL)));
    json_object_object_add(generation, "withdrawal", withdrawal);
    if (placement_record_write(record)) { json_object_put(record); return f_error("fleet-placement", "io_failed", "cannot record the withdrawal intent durably; nothing was sent"); }
    argv[1] = (char *)host;
    response = task_remote_cli(timeout ? 6 : 4, argv);
    data = f_field(response, "data"); state = f_string(f_field(data, "runtime"), "state");
    if (json_object_get_boolean(f_field(response, "ok")) && state && !strcmp(state, "withdrawn")) {
        f_string_add(withdrawal, "state", "confirmed"); json_object_object_add(withdrawal, "confirmed_at", json_object_new_int64((int64_t)time(NULL)));
        json_object_object_add(record, "assigned_host", json_object_new_null()); f_string_add(record, "reservation", "withdrawn"); f_string_add(generation, "reservation", "withdrawn");
    } else {
        code = json_object_get_boolean(f_field(response, "ok")) ? "not_withdrawn" : f_string(f_field(response, "error"), "code");
        f_string_add(withdrawal, "state", uncertain_code(code) ? "unknown" : "refused"); f_string_add(withdrawal, "code", code ? code : "unknown");
        if (json_object_get_boolean(f_field(response, "ok"))) {
            /* state and data are borrowed from response: use them before it is released. */
            json_object *refusal = f_error("fleet-placement", "execution_owned", "the receiver did not record a withdrawal; running or claimed work is never migrated");
            f_string_add(f_field(refusal, "error"), "task_state", state ? state : "unknown");
            json_object_put(response); response = refusal; data = NULL;
        }
    }
    if (placement_record_write(record)) { json_object_put(response); response = f_error("fleet-placement", "io_failed", "the placement record could not be updated after the withdrawal request; inspect the receiver before transferring"); }
    else if (json_object_is_type(data, json_type_object)) {
        json_object *summary = json_object_new_object(); f_string_add(summary, "task_id", task_id); f_string_add(summary, "host", host);
        json_object_object_add(summary, "generation", json_object_get(f_field(generation, "generation"))); f_string_add(summary, "withdrawal", f_string(withdrawal, "state"));
        json_object_object_add(data, "placement", summary);
    }
    json_object_put(record);
    return response;
}
static json_object *drain(const char *alias, bool drained) {
    char dir[F_PATH], path[F_PATH]; json_object *data = json_object_new_object();
    if (!f_name(alias) || strlen(alias) >= 128) { json_object_put(data); return f_error("fleet-placement", "invalid_input", "select a registered host alias"); }
    if (f_path(dir, sizeof(dir), f_home, "fleet/placement-drained") || f_path(path, sizeof(path), dir, alias) || f_mkdirs(dir)) { json_object_put(data); return f_error("fleet-placement", "io_failed", "cannot open private drain markers"); }
    if (drained) { if (access(path, F_OK) && f_write(path, "drained\n", 8, false)) { json_object_put(data); return f_error("fleet-placement", "io_failed", "cannot record the drain marker"); } }
    else if (unlink(path) && errno != ENOENT) { json_object_put(data); return f_error("fleet-placement", "io_failed", "cannot remove the drain marker"); }
    f_string_add(data, "host", alias); json_object_object_add(data, "drained", json_object_new_boolean(placement_drained(alias)));
    return f_success("fleet-placement-drain", data);
}
static json_object *policy(void) {
    json_object *effective = placement_policy_load(), *data = json_object_new_object(); char path[F_PATH];
    if (!effective) { json_object_put(data); return f_error("fleet-placement", "policy_invalid", "fleet/placement-policy.json is malformed; it must be an object of the documented integer knobs"); }
    json_object_object_add(data, "policy", effective);
    if (!f_path(path, sizeof(path), f_home, "fleet/placement-policy.json")) { f_string_add(data, "file", path); json_object_object_add(data, "file_present", json_object_new_boolean(access(path, F_OK) == 0)); }
    json_object_object_add(data, "drained_hosts", placement_drained_list());
    json_object_object_add(data, "estimates", placement_estimates());
    return f_success("fleet-placement-policy", data);
}
json_object *placement_cli(int argc, char **argv) {
    const char *action = argc ? argv[0] : "help", *subject = argc > 1 ? argv[1] : NULL, *timeout = NULL; int i;
    if (!strcmp(action, "help") || !strcmp(action, "--help")) return usage();
    if (!strcmp(action, "policy")) return argc == 1 || (argc == 2 && !strcmp(argv[1], "--json")) ? policy() : f_error("fleet-placement", "invalid_input", "policy takes no arguments");
    if (!subject || subject[0] == '-') return f_error("fleet-placement", "invalid_input", "use hydra fleet placement help");
    for (i = 2; i < argc; i++) {
        if (!strcmp(argv[i], "--json")) continue;
        if (!strcmp(action, "withdraw") && !strcmp(argv[i], "--timeout") && !timeout && i + 1 < argc) { timeout = argv[++i]; continue; }
        return f_error("fleet-placement", "invalid_input", "use hydra fleet placement help");
    }
    if (!strcmp(action, "drain") || !strcmp(action, "undrain")) return drain(subject, !strcmp(action, "drain"));
    if (strncmp(subject, "task_", 5) || !task_hex(subject + 5, 64)) return f_error("fleet-placement", "invalid_input", "use the task ID returned by submission");
    if (!strcmp(action, "explain")) return explain(subject);
    if (!strcmp(action, "replay")) return replay(subject);
    if (!strcmp(action, "withdraw")) return withdraw(subject, timeout);
    return f_error("fleet-placement", "invalid_input", "use hydra fleet placement help");
}
