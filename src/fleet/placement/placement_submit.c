#define _XOPEN_SOURCE 700
#include "fleet/placement/placement.h"
#include "fleet/support/files.h"
#include "fleet/transport/remote.h"
#include "fleet/task/task.h"
#include <fcntl.h>
#include <stdlib.h>
#include <sys/file.h>
#include <unistd.h>
#include <string.h>
#include <time.h>

/* A host-bound copy of a prepared package: only spec.host and the derived
 * spec digest change; bundle, inputs and limits stay byte-identical. */
static json_object *rebind(json_object *package, const char *host) {
    char scratch[F_PATH], hash[65]; json_object *copy = f_parse(json_object_to_json_string_ext(package, JSON_C_TO_STRING_PLAIN)), *spec = f_field(copy, "spec");
    if (!copy || !spec) { json_object_put(copy); return NULL; }
    if (!strcmp(f_string(spec, "host"), host)) return copy;
    f_string_add(spec, "host", host);
    if (snprintf(scratch, sizeof(scratch), "%s/hydra-placement-bind.XXXXXX", getenv("TMPDIR") ? getenv("TMPDIR") : "/tmp") >= (int)sizeof(scratch) || !mkdtemp(scratch)) { json_object_put(copy); return NULL; }
    if (task_json_hash(spec, scratch, hash)) { f_remove_tree(scratch); json_object_put(copy); return NULL; }
    f_remove_tree(scratch);
    f_string_add(copy, "spec_sha256", hash);
    return copy;
}
static bool listed(const char *const names[], const char *value) {
    size_t i;
    for (i = 0; value && names[i]; i++) if (!strcmp(value, names[i])) return true;
    return false;
}
/* Receiver admission is final: accepted, refused (definitive, reconsider the
 * next host), unknown (stay assigned for reconciliation) or error (stop). */
static const char *classify(json_object *response, const char **code) {
    const char *const refusals[] = {"capability_unavailable", "unmapped_project", "missing_dependency", "invalid_package", "platform_mismatch", NULL};
    const char *const runtime_refusals[] = {"queue_full", "capability_unavailable", "capability_labels_too_large", NULL};
    *code = NULL;
    if (json_object_get_boolean(f_field(response, "ok"))) {
        json_object *runtime = f_field(f_field(response, "data"), "runtime"); const char *state = f_string(runtime, "state"), *failure = f_string(runtime, "failure");
        if (state && !strcmp(state, "failed") && listed(runtime_refusals, failure)) { *code = failure; return "refused"; }
        return "accepted";
    }
    *code = f_string(f_field(response, "error"), "code");
    if (*code && !strcmp(*code, "outcome_unknown")) return "unknown";
    return listed(refusals, *code) ? "refused" : "error";
}
static json_object *last_generation(json_object *record) {
    json_object *generations = f_field(record, "generations"); size_t n = json_object_array_length(generations);
    return n ? json_object_array_get_idx(generations, n - 1) : NULL;
}
static json_object *new_generation(json_object *record, int number) {
    json_object *entry = json_object_new_object();
    json_object_object_add(entry, "generation", json_object_new_int(number));
    json_object_object_add(entry, "host", json_object_new_null()); f_string_add(entry, "reservation", "none");
    json_object_object_add(entry, "bound_spec_sha256", json_object_new_null()); json_object_object_add(entry, "withdrawal", json_object_new_null());
    json_object_object_add(entry, "rounds", json_object_new_array());
    json_object_array_add(f_field(record, "generations"), entry);
    json_object_object_add(record, "generation", json_object_new_int(number));
    json_object_object_add(record, "assigned_host", json_object_new_null()); f_string_add(record, "reservation", "none");
    json_object_object_add(record, "bound_spec_sha256", json_object_new_null());
    return entry;
}
/* Hosts that already hold a receiver record for this task ID can never take
 * it again: an acceptance directory is immutable. */
static json_object *hosts_with_records(json_object *record) {
    json_object *generations = f_field(record, "generations"), *out = json_object_new_array(); size_t g, r, a;
    for (g = 0; g < json_object_array_length(generations); g++) {
        json_object *rounds = f_field(json_object_array_get_idx(generations, g), "rounds");
        for (r = 0; r < json_object_array_length(rounds); r++) {
            json_object *attempts = f_field(json_object_array_get_idx(rounds, r), "attempts");
            for (a = 0; a < json_object_array_length(attempts); a++) {
                json_object *attempt = json_object_array_get_idx(attempts, a);
                if (json_object_get_boolean(f_field(attempt, "receiver_record"))) json_object_array_add(out, json_object_new_string(f_string(attempt, "host")));
            }
        }
    }
    return out;
}
static void set_assignment(json_object *record, json_object *generation, const char *host, const char *reservation, const char *digest) {
    json_object_object_add(generation, "host", host ? json_object_new_string(host) : json_object_new_null());
    json_object_object_add(record, "assigned_host", host ? json_object_new_string(host) : json_object_new_null());
    f_string_add(generation, "reservation", reservation); f_string_add(record, "reservation", reservation);
    json_object_object_add(generation, "bound_spec_sha256", digest ? json_object_new_string(digest) : json_object_new_null());
    json_object_object_add(record, "bound_spec_sha256", digest ? json_object_new_string(digest) : json_object_new_null());
}
/* One durable dispatch: the attempt is recorded in flight before the request
 * leaves, so a crash leaves an uncertain attempt rather than an unrecorded one. */
static json_object *dispatch(json_object *record, json_object *generation, json_object *round, json_object *package, const char *host,
                             const char *key, const char *trust, unsigned seconds, const char **outcome) {
    json_object *attempts = f_field(round, "attempts"), *attempt = json_object_new_object(), *bound, *response; const char *code, *digest;
    f_string_add(attempt, "host", host); f_string_add(attempt, "outcome", "in_flight");
    json_object_object_add(attempt, "receiver_record", json_object_new_boolean(false));
    json_object_object_add(attempt, "requested_at", json_object_new_int64((int64_t)time(NULL)));
    json_object_array_add(attempts, attempt);
    if (!(bound = rebind(package, host))) { f_string_add(attempt, "outcome", "error"); f_string_add(attempt, "code", "rebind_failed"); *outcome = "error"; return f_error("fleet-task-submit", "io_failed", "cannot bind the package to the selected host"); }
    digest = f_string(bound, "spec_sha256"); f_string_add(attempt, "spec_sha256", digest);
    if (placement_record_write(record)) { json_object_put(bound); f_string_add(attempt, "outcome", "error"); *outcome = "error"; return f_error("fleet-task-submit", "io_failed", "cannot record the placement attempt durably; nothing was sent"); }
    response = task_remote_submit(host, bound, key, trust ? digest : NULL, json_object_get_int(f_field(generation, "generation")), seconds);
    json_object_put(bound);
    *outcome = classify(response, &code);
    f_string_add(attempt, "outcome", *outcome);
    if (code) f_string_add(attempt, "code", code);
    if (json_object_get_boolean(f_field(response, "ok"))) {
        const char *state = f_string(f_field(f_field(response, "data"), "runtime"), "state");
        json_object_object_add(attempt, "receiver_record", json_object_new_boolean(true));
        if (state) f_string_add(attempt, "task_state", state);
    } else if (!strcmp(*outcome, "unknown")) json_object_object_add(attempt, "receiver_record", json_object_new_boolean(true));
    if (!strcmp(*outcome, "accepted") || !strcmp(*outcome, "unknown")) set_assignment(record, generation, host, *outcome, digest);
    return response;
}
static json_object *new_round(json_object *generation, const char *mode, json_object *excluded, json_object *separation, int64_t now) {
    json_object *round = json_object_new_object();
    json_object_object_add(round, "decided_at", json_object_new_int64(now)); f_string_add(round, "mode", mode);
    json_object_object_add(round, "excluded_hosts", json_object_get(excluded)); json_object_object_add(round, "separation_hosts", json_object_get(separation));
    json_object_object_add(round, "attempts", json_object_new_array());
    json_object_array_add(f_field(generation, "rounds"), round);
    return round;
}
static json_object *explicit_round(json_object *generation, const char *host, json_object *separation, int64_t now) {
    json_object *round = new_round(generation, "explicit", json_object_new_array(), separation, now), *candidates = json_object_new_array(), *entry = json_object_new_object(), *decision = json_object_new_object();
    json_object_put(f_field(round, "excluded_hosts"));
    f_string_add(entry, "host", host); json_object_object_add(entry, "eligible", json_object_new_boolean(true));
    json_object_object_add(entry, "reasons", json_object_new_array()); json_object_object_add(entry, "score", json_object_new_null());
    json_object_object_add(entry, "estimates", placement_estimates()); json_object_object_add(entry, "rank", json_object_new_int(1));
    json_object_array_add(candidates, entry);
    f_string_add(decision, "host", host); f_string_add(decision, "reason", "explicit");
    json_object_object_add(decision, "policy_version", json_object_new_int(PLACEMENT_POLICY_VERSION));
    json_object_object_add(round, "observations", json_object_new_array());
    json_object_object_add(round, "candidates", candidates); json_object_object_add(round, "decision", decision);
    return round;
}
static int compare_names(const void *a, const void *b) { return strcmp(*(const char *const *)a, *(const char *const *)b); }
static json_object *auto_round(json_object *record, json_object *generation, json_object *separation, unsigned seconds, int64_t now) {
    json_object *names = f_remotes(), *excluded = hosts_with_records(record), *observations = json_object_new_array(), *round, *decision = NULL, *candidates;
    const char *sorted[PLACEMENT_HOSTS]; size_t i, count = json_object_array_length(names);
    const char *labels = f_string(f_field(record, "inputs"), "required_labels_key");
    if (count > PLACEMENT_HOSTS) { json_object_put(names); json_object_put(excluded); json_object_put(observations); return NULL; }
    for (i = 0; i < count; i++) sorted[i] = f_text(json_object_array_get_idx(names, i));
    qsort(sorted, count, sizeof(sorted[0]), compare_names);
    for (i = 0; i < count; i++) {
        bool drained = placement_drained(sorted[i]); size_t e; bool skip = false;
        for (e = 0; e < json_object_array_length(excluded); e++) if (!strcmp(f_text(json_object_array_get_idx(excluded, e)), sorted[i])) skip = true;
        if (skip) {
            json_object *entry = json_object_new_object(); f_string_add(entry, "host", sorted[i]);
            json_object_object_add(entry, "observed_at", json_object_new_int64(now)); json_object_object_add(entry, "drained", json_object_new_boolean(drained));
            f_string_add(entry, "error", "not_observed"); json_object_array_add(observations, entry);
        } else json_object_array_add(observations, placement_observe(sorted[i], labels, seconds, now, drained));
    }
    now = (int64_t)time(NULL); round = new_round(generation, "auto", excluded, separation, now);
    candidates = placement_decide(f_field(record, "inputs"), observations, f_field(record, "policy"), excluded, separation, now, &decision);
    json_object_object_add(round, "observations", observations); json_object_object_add(round, "candidates", candidates); json_object_object_add(round, "decision", decision);
    json_object_put(names); json_object_put(excluded);
    return round;
}
static json_object *separation_hosts(json_object *separate_from, json_object **failure) {
    json_object *hosts = json_object_new_array(); size_t i;
    for (i = 0; separate_from && i < json_object_array_length(separate_from); i++) {
        const char *id = f_text(json_object_array_get_idx(separate_from, i)); char host[128];
        if (!placement_generation(id, host) || !host[0]) {
            json_object_put(hosts); *failure = f_error("fleet-task-submit", "separation_unresolved", "a --separate-from task has no recorded current host; place it first or pin explicitly");
            return NULL;
        }
        json_object_array_add(hosts, json_object_new_string(host));
    }
    return hosts;
}
static json_object *attempt_ranked(json_object *record, json_object *generation, json_object *round, json_object *package, const char *key, const char *trust, unsigned seconds) {
    json_object *candidates = f_field(round, "candidates"), *response = NULL; const char *outcome = "none"; int rank, n = (int)json_object_array_length(candidates);
    for (rank = 1; rank <= n; rank++) {
        int i; json_object *chosen = NULL;
        for (i = 0; i < n; i++) if (f_number_is(json_object_array_get_idx(candidates, i), "rank", rank)) chosen = json_object_array_get_idx(candidates, i);
        if (!chosen) break;
        json_object_put(response);
        response = dispatch(record, generation, round, package, f_string(chosen, "host"), key, trust, seconds, &outcome);
        if (strcmp(outcome, "refused")) break;
    }
    if (!strcmp(outcome, "refused") || !strcmp(outcome, "none")) {
        set_assignment(record, generation, NULL, !strcmp(outcome, "none") ? "none" : "refused", NULL);
        json_object_put(response);
        response = f_error("fleet-task-submit", !strcmp(outcome, "none") ? "no_eligible_host" : "placement_refused",
            !strcmp(outcome, "none") ? "no registered host is eligible; inspect hydra fleet placement explain TASK_ID" : "every eligible receiver refused the task; inspect hydra fleet placement explain TASK_ID");
    }
    if (placement_record_write(record)) { json_object_put(response); return f_error("fleet-task-submit", "io_failed", "the placement record could not be updated after dispatch; reconcile from the receiver"); }
    return response;
}
static json_object *with_placement(json_object *response, json_object *record) {
    json_object *data = f_field(response, "data"), *summary = json_object_new_object(); char directory[F_PATH];
    f_string_add(summary, "task_id", f_string(record, "task_id")); json_object_object_add(summary, "generation", json_object_get(f_field(record, "generation")));
    json_object_object_add(summary, "assigned_host", json_object_get(f_field(record, "assigned_host"))); json_object_object_add(summary, "reservation", json_object_get(f_field(record, "reservation")));
    json_object_object_add(summary, "bound_spec_sha256", json_object_get(f_field(record, "bound_spec_sha256"))); f_string_add(summary, "mode", f_string(record, "mode"));
    if (!placement_directory(f_string(record, "task_id"), directory, false)) f_string_add(summary, "record_directory", directory);
    if (json_object_is_type(data, json_type_object)) json_object_object_add(data, "placement", summary);
    else { json_object_object_add(f_field(response, "error"), "placement", summary); }
    return response;
}
/* Submissions for one key serialize on the record directory, so concurrent
 * submitters never dispatch the same task to two receivers. */
static int lock_record(const char *id) {
    char directory[F_PATH], path[F_PATH]; int fd;
    if (placement_directory(id, directory, true) || f_path(path, sizeof(path), directory, "lock")) return -1;
    fd = open(path, O_RDWR | O_CREAT | O_NOFOLLOW, 0600);
    if (fd < 0 || fcntl(fd, F_SETFD, FD_CLOEXEC) || flock(fd, LOCK_EX)) { if (fd >= 0) close(fd); return -1; }
    return fd;
}
static json_object *placement_submit_locked(json_object *package, const char *key, const char *trust, const char *mode, json_object *separate_from, unsigned seconds, const char *id);
json_object *placement_submit(json_object *package, const char *key, const char *trust, const char *mode, json_object *separate_from, unsigned seconds) {
    char id[70]; int lock; json_object *response;
    const char *prepared = f_string(package, "spec_sha256");
    if (trust && strcmp(trust, prepared)) return f_error("fleet-task-submit", "trust_required", "--trust-spec must match the prepared package digest; the host-bound digest is recorded by placement");
    if (placement_task_id(key, id)) return f_error("fleet-task-submit", "invalid_input", "submission keys must be 1-128 letters, digits, dots, underscores, or hyphens");
    if ((lock = lock_record(id)) < 0) return f_error("fleet-task-submit", "io_failed", "cannot create or lock private placement storage");
    response = placement_submit_locked(package, key, trust, mode, separate_from, seconds, id);
    close(lock);
    return response;
}
/* The existing record for this key, validated against the package and mode,
 * or a new record bound to them. Consumes policy. */
static json_object *record_open(json_object *package, const char *key, const char *mode, const char *id, json_object *policy, json_object **error) {
    json_object *record = placement_record_read(id); const char *prepared = f_string(package, "spec_sha256");
    *error = NULL;
    if (record) {
        const char *key_recorded = f_string(record, "submission_key"), *mode_recorded = f_string(record, "mode");
        json_object_put(policy);
        if (key_recorded && !strcmp(key_recorded, key) && !strcmp(prepared, f_string(record, "prepared_spec_sha256")) && mode_recorded && !strcmp(mode_recorded, mode)) return record;
        json_object_put(record);
        *error = f_error("fleet-task-submit", "placement_conflict", "this key already has a placement record bound to a different package or mode");
        return NULL;
    }
    record = json_object_new_object(); json_object_object_add(record, "schema_version", json_object_new_int(PLACEMENT_SCHEMA));
    f_string_add(record, "task_id", id); f_string_add(record, "submission_key", key); f_string_add(record, "mode", mode);
    f_string_add(record, "prepared_spec_sha256", prepared); f_string_add(record, "prepared_host", f_string(f_field(package, "spec"), "host"));
    json_object_object_add(record, "inputs", placement_inputs(f_field(package, "spec"))); json_object_object_add(record, "policy", policy);
    json_object_object_add(record, "generations", json_object_new_array());
    new_generation(record, 1);
    return record;
}
/* Reconciliation: an assigned or uncertain dispatch is repeated to the same receiver only. */
static json_object *reconcile(json_object *record, json_object *generation, json_object *package, const char *key, const char *trust, json_object *separation, unsigned seconds, int64_t now) {
    const char *outcome, *host = f_string(generation, "host"); size_t rounds = json_object_array_length(f_field(generation, "rounds")); json_object *round, *response;
    round = rounds ? json_object_array_get_idx(f_field(generation, "rounds"), rounds - 1) : explicit_round(generation, host, separation, now);
    response = dispatch(record, generation, round, package, host, key, trust, seconds, &outcome);
    if (!strcmp(outcome, "refused")) set_assignment(record, generation, NULL, "refused", NULL);
    if (placement_record_write(record)) { json_object_put(response); response = f_error("fleet-task-submit", "io_failed", "the placement record could not be updated after dispatch; reconcile from the receiver"); }
    return with_placement(response, record);
}
static json_object *placement_submit_locked(json_object *package, const char *key, const char *trust, const char *mode, json_object *separate_from, unsigned seconds, const char *id) {
    json_object *record, *generation, *round = NULL, *policy, *separation, *failure = NULL, *response = NULL; int64_t now = (int64_t)time(NULL);
    const char *state; bool automatic = !strcmp(mode, "auto"); size_t i;
    if (!(policy = placement_policy_load())) return f_error("fleet-task-submit", "policy_invalid", "fleet/placement-policy.json is malformed; fix or remove it");
    if (!(separation = separation_hosts(separate_from, &failure))) { json_object_put(policy); return failure; }
    if (!(record = record_open(package, key, automatic ? "auto" : "explicit", id, policy, &failure))) { json_object_put(separation); return failure; }
    generation = last_generation(record); state = f_string(f_field(generation, "withdrawal"), "state");
    if (state && !strcmp(state, "unknown")) response = f_error("fleet-task-submit", "withdrawal_unconfirmed", "the previous receiver's withdrawal response was lost; rerun hydra fleet placement withdraw before transferring");
    else if (state && !strcmp(state, "confirmed")) generation = new_generation(record, json_object_get_int(f_field(record, "generation")) + 1);
    else if (f_string(generation, "host")) response = reconcile(record, generation, package, key, trust, separation, seconds, now);
    if (response) goto done;
    if (automatic) {
        round = auto_round(record, generation, separation, seconds, now);
        if (!round) response = f_error("fleet-task-submit", "too_many_hosts", "automatic placement considers at most 16 registered aliases");
    } else {
        for (i = 0; i < json_object_array_length(separation); i++)
            if (!strcmp(f_text(json_object_array_get_idx(separation, i)), mode)) response = f_error("fleet-task-submit", "validator_separation", "the pinned host currently holds a task this one must be separated from");
        if (!response) round = explicit_round(generation, mode, separation, now);
    }
    if (!response) response = with_placement(attempt_ranked(record, generation, round, package, key, trust, seconds), record);
done:
    json_object_put(separation); json_object_put(record);
    return response;
}
