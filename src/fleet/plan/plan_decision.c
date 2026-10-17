#include "fleet/plan/plan_inspect.h"
#include "fleet/task/task.h"
#include "fleet/support/files.h"
#include <dirent.h>
#include <errno.h>
#include <stdlib.h>
#include <string.h>

/* A decision record keeps the candidate generator honest: the selected and
 * rejected compiled alternatives, the stated reasons, the compiler and
 * analysis versions, the policy digest and the exact analysis inputs are
 * written beside the deterministic comparison, so `decision show` can rerun
 * the comparison from the recorded inputs and compare it byte for byte. The
 * shell owns the directory: this module writes only into the directory it is
 * given and never grants validation, approval or execution. */
#define DECISION_RECORD "hydra-plan-decision-1"
#define DECISION_ALTERNATIVES 9U

struct alternative { json_object *compiled; char digest[65]; const char *reason; };

static bool reason_valid(const char *text) {
    return text && *text && strlen(text) <= 4096 && plan_utf8(text, strlen(text)) && !strpbrk(text, "\r\n");
}
static bool estimates_valid(json_object *file, const struct alternative *alternatives, size_t count) {
    json_object *plans = f_field(file, "plans");
    if (!f_number_is(file, "schema_version", 1) || json_object_object_length(file) != 2 ||
        !json_object_is_type(plans, json_type_object) || !json_object_object_length(plans)) return false;
    json_object_object_foreach(plans, id, entry) {
        size_t i = 0;
        while (i < count && strcmp(alternatives[i].digest, id)) i++;
        if (i == count || !pi_estimates_valid(entry, alternatives[i].compiled)) return false;
    }
    return true;
}
static char *canonical_text(json_object *value) {
    json_object *canonical = plan_canonical(value);
    char *text = strdup(json_object_to_json_string_ext(canonical, JSON_C_TO_STRING_PLAIN));
    json_object_put(canonical); return text;
}
static int write_canonical(const char *directory, const char *name, json_object *value) {
    char path[F_PATH], *text = canonical_text(value); int status = -1;
    if (text && !f_path(path, sizeof(path), directory, name)) status = f_write(path, text, strlen(text), false);
    free(text); return status;
}
static json_object *alternative_record(const struct alternative *a) {
    json_object *out = json_object_new_object(); char input[80];
    snprintf(input, sizeof(input), "inputs/%s.json", a->digest);
    f_string_add(out, "sha256", a->digest); f_string_add(out, "reason", a->reason); f_string_add(out, "input", input);
    return out;
}
/* The comparison of the selected alternative against one rejected alternative,
 * with the digest of its canonical form for the replay check. */
static json_object *comparison(const struct alternative *selected, const struct alternative *rejected, json_object *estimates) {
    json_object *out = alternative_record(rejected), *evaluation = pe_evaluate(selected->compiled, selected->digest, rejected->compiled, rejected->digest, estimates);
    char digest[65];
    if (plan_digest(evaluation, digest)) { json_object_put(evaluation); json_object_put(out); return NULL; }
    f_string_add(out, "evaluation_sha256", digest); json_object_object_add(out, "evaluation", evaluation);
    return out;
}
static bool budget_conflict(const struct alternative *selected, json_object *estimates) {
    json_object *metrics = pi_metrics(selected->compiled, f_field(f_field(estimates, "plans"), selected->digest));
    bool conflict = json_object_get_boolean(f_field(metrics, "estimate_budget_conflict"));
    json_object_put(metrics); return conflict;
}
static json_object *record_data(const struct alternative *alternatives, size_t count, long recorded_at, json_object *estimates, const char *estimates_id) {
    json_object *out = json_object_new_object(), *policy = json_object_new_object(), *rejected = json_object_new_array(), *estimate = json_object_new_object();
    char digest[65];
    if (plan_digest(f_field(alternatives[0].compiled, "policy"), digest)) { json_object_put(out); json_object_put(policy); json_object_put(rejected); json_object_put(estimate); return NULL; }
    json_object_object_add(out, "schema_version", json_object_new_int(1));
    f_string_add(out, "record", DECISION_RECORD);
    json_object_object_add(out, "recorded_at", json_object_new_int64(recorded_at));
    f_string_add(out, "compiler", PLAN_COMPILER); f_string_add(out, "analysis", PLAN_EVALUATION);
    json_object_object_add(policy, "schema_version", json_object_get(f_field(f_field(alternatives[0].compiled, "policy"), "schema_version")));
    f_string_add(policy, "sha256", digest); json_object_object_add(out, "policy", policy);
    json_object_object_add(out, "hard_budgets", json_object_get(f_field(f_field(alternatives[0].compiled, "plan"), "envelope")));
    if (estimates) { f_string_add(estimate, "sha256", estimates_id); f_string_add(estimate, "input", "inputs/estimates.json"); }
    else { json_object_object_add(estimate, "sha256", NULL); json_object_object_add(estimate, "input", NULL); }
    json_object_object_add(out, "estimates", estimate);
    json_object_object_add(out, "selected", alternative_record(&alternatives[0]));
    json_object_object_add(f_field(out, "selected"), "estimate_budget_conflict", estimates ? json_object_new_boolean(budget_conflict(&alternatives[0], estimates)) : NULL);
    for (size_t i = 1; i < count; i++) {
        json_object *row = comparison(&alternatives[0], &alternatives[i], estimates);
        if (!row) { json_object_put(out); json_object_put(rejected); return NULL; }
        json_object_array_add(rejected, row);
    }
    json_object_object_add(out, "rejected", rejected);
    f_string_add(out, "limits", "Reasons are the generator's statements, not verified claims. Hard budgets are the selected plan's declared envelope and are never relaxed by estimates. Recording a decision grants no validation, approval or execution.");
    return out;
}
static json_object *decision_id(const char *directory, char id[17]) {
    char path[F_PATH], digest[65]; json_object *out;
    if (f_path(path, sizeof(path), directory, "decision.json") || f_hash(path, digest)) return NULL;
    memcpy(id, digest, 16); id[16] = '\0';
    out = json_object_new_object(); f_string_add(out, "decision_id", id); return out;
}
static json_object *refuse(const char *code, const char *message) {
    return f_error("workflow plan decision", code, message);
}
static json_object *record_command(int argc, char **argv) {
    struct alternative alternatives[DECISION_ALTERNATIVES] = {{0}}; size_t count = 0;
    json_object *estimates = NULL, *data = NULL, *result = NULL; char estimates_id[65], id[17], *end; long recorded_at;
    if (argc < 6 || (argc - 6) % 2 || (size_t)(argc - 4) / 2 > DECISION_ALTERNATIVES) return refuse("invalid_arguments", "expected a directory, time, estimates file or -, a selected artifact with its reason and up to eight rejected artifacts with reasons");
    errno = 0; recorded_at = strtol(argv[2], &end, 10);
    if (errno || *end || recorded_at < 0) return refuse("invalid_arguments", "recorded time must be a nonnegative integer");
    for (int i = 4; i + 1 < argc; i += 2, count++) {
        if (!(alternatives[count].compiled = pi_read_compiled(argv[i], alternatives[count].digest))) { result = refuse("invalid_alternative", "every alternative must be a supported, structurally valid compiled artifact"); goto done; }
        alternatives[count].reason = argv[i + 1];
        if (!reason_valid(argv[i + 1])) { result = refuse("invalid_reason", "each reason is one line of 1 to 4096 UTF-8 bytes"); goto done; }
        for (size_t j = 0; j < count; j++) if (!strcmp(alternatives[j].digest, alternatives[count].digest)) { result = refuse("duplicate_alternative", "the same compiled digest cannot be both selected and rejected or rejected twice"); goto done; }
    }
    if (strcmp(argv[3], "-")) {
        if (!(estimates = plan_read(argv[3])) || !estimates_valid(estimates, alternatives, count) || plan_digest(estimates, estimates_id)) { result = refuse("invalid_estimates", "estimates must use the closed schema and name only the exact digests of the recorded alternatives"); goto done; }
    }
    if (estimates && budget_conflict(&alternatives[0], estimates)) { result = refuse("estimate_budget_conflict", "an estimate lower bound exceeds a hard budget of the selected plan; estimates cannot relax budgets, so revise the plan or its estimates"); goto done; }
    if (!(data = record_data(alternatives, count, recorded_at, estimates, estimates_id))) { result = refuse("analysis_failed", "the deterministic comparison could not be computed"); goto done; }
    for (size_t i = 0; i < count; i++) {
        char name[80]; snprintf(name, sizeof(name), "%s.json", alternatives[i].digest);
        char inputs[F_PATH]; if (f_path(inputs, sizeof(inputs), argv[1], "inputs") || write_canonical(inputs, name, alternatives[i].compiled)) { result = refuse("record_write_failed", "could not write the analysis inputs into a new private directory"); goto done; }
        if (estimates && !i && write_canonical(inputs, "estimates.json", estimates)) { result = refuse("record_write_failed", "could not write the estimates input"); goto done; }
    }
    if (write_canonical(argv[1], "decision.json", data) || !(result = decision_id(argv[1], id))) { result = refuse("record_write_failed", "could not write the decision record"); goto done; }
    f_string_add(result, "selected_sha256", alternatives[0].digest);
    json_object_object_add(result, "rejected", json_object_new_int64((int64_t)count - 1));
    result = f_success("workflow plan decision record", result);
done:
    for (size_t i = 0; i < count; i++) json_object_put(alternatives[i].compiled);
    json_object_put(estimates); json_object_put(data); return result;
}
/* Replays one recorded comparison from the recorded inputs. */
static json_object *replay_row(const char *directory, json_object *selected, const char *selected_id, json_object *row, json_object *estimates, bool *reproduced) {
    json_object *out = json_object_new_object(), *compiled = NULL, *evaluation = NULL; char path[F_PATH], digest[65], *recorded = NULL, *replayed = NULL;
    const char *id = f_string(row, "sha256"); bool same = false;
    f_string_add(out, "rejected_sha256", id); f_string_add(out, "recorded_sha256", f_string(row, "evaluation_sha256"));
    if (id && !f_path(path, sizeof(path), directory, f_string(row, "input")) && (compiled = pi_read_compiled(path, digest)) && !strcmp(digest, id)) {
        evaluation = pe_evaluate(selected, selected_id, compiled, id, estimates);
        recorded = canonical_text(f_field(row, "evaluation")); replayed = canonical_text(evaluation);
        same = recorded && replayed && !strcmp(recorded, replayed) && !plan_digest(evaluation, digest) && f_string(row, "evaluation_sha256") && !strcmp(digest, f_string(row, "evaluation_sha256"));
    }
    f_string_add(out, "replayed_sha256", evaluation && !plan_digest(evaluation, digest) ? digest : "unavailable");
    json_object_object_add(out, "reproduced", json_object_new_boolean(same));
    *reproduced = *reproduced && same;
    free(recorded); free(replayed); json_object_put(evaluation); json_object_put(compiled); return out;
}
static json_object *show_data(const char *directory, const char *expected) {
    char path[F_PATH], id[17], digest[65]; json_object *record = NULL, *selected = NULL, *estimates = NULL, *replay, *rows, *check, *out = NULL;
    bool reproduced = true;
    if (!(check = decision_id(directory, id))) return NULL;
    json_object_put(check);
    if (strcmp(id, expected) || f_path(path, sizeof(path), directory, "decision.json") || !(record = plan_read(path)) ||
        !f_number_is(record, "schema_version", 1) || !f_string(record, "record") || strcmp(f_string(record, "record"), DECISION_RECORD) ||
        !f_string(record, "analysis") || strcmp(f_string(record, "analysis"), PLAN_EVALUATION)) goto done;
    if (!f_string(f_field(record, "selected"), "input") || f_path(path, sizeof(path), directory, f_string(f_field(record, "selected"), "input")) ||
        !(selected = pi_read_compiled(path, digest)) || strcmp(digest, f_string(f_field(record, "selected"), "sha256"))) goto done;
    if (f_string(f_field(record, "estimates"), "input")) {
        if (f_path(path, sizeof(path), directory, f_string(f_field(record, "estimates"), "input")) || !(estimates = plan_read(path)) || plan_digest(estimates, digest) ||
            strcmp(digest, f_string(f_field(record, "estimates"), "sha256"))) goto done;
    }
    rows = json_object_new_array();
    for (size_t i = 0; i < json_object_array_length(f_field(record, "rejected")); i++)
        json_object_array_add(rows, replay_row(directory, selected, f_string(f_field(record, "selected"), "sha256"), json_object_array_get_idx(f_field(record, "rejected"), i), estimates, &reproduced));
    replay = json_object_new_object();
    f_string_add(replay, "status", reproduced ? "reproduced" : "diverged");
    json_object_object_add(replay, "comparisons", rows);
    f_string_add(replay, "method", "every comparison recomputed from the recorded inputs and compared byte for byte in canonical form");
    out = json_object_new_object(); f_string_add(out, "decision_id", id);
    json_object_object_add(out, "record", json_object_get(record)); json_object_object_add(out, "replay", replay);
done:
    json_object_put(estimates); json_object_put(selected); json_object_put(record); return out;
}
static void view(json_object *data) {
    json_object *record = f_field(data, "record"), *rejected = f_field(record, "rejected");
    printf("Decision %s recorded at %lld (%s, %s)\n", f_string(data, "decision_id"), (long long)json_object_get_int64(f_field(record, "recorded_at")), f_string(record, "compiler"), f_string(record, "analysis"));
    printf("Selected %.12s: %s\n", f_string(f_field(record, "selected"), "sha256"), f_string(f_field(record, "selected"), "reason"));
    for (size_t i = 0; i < json_object_array_length(rejected); i++) {
        json_object *row = json_object_array_get_idx(rejected, i), *evaluation = f_field(row, "evaluation");
        const char *concerns[] = {"decomposition", "scheduling", "placement"};
        printf("Rejected %.12s: %s\n", f_string(row, "sha256"), f_string(row, "reason"));
        for (size_t c = 0; c < 3; c++) printf("  %s: %s\n", concerns[c], f_string(f_field(evaluation, concerns[c]), "modeled_preference"));
    }
    printf("Estimates: %s\nReplay: %s\n", f_string(f_field(record, "estimates"), "sha256") ? f_string(f_field(record, "estimates"), "sha256") : "none",
           f_string(f_field(data, "replay"), "status"));
}
static int name_compare(const void *a, const void *b) { return strcmp(*(char *const *)a, *(char *const *)b); }
static json_object *list_command(const char *directory) {
    DIR *dir = opendir(directory); struct dirent *entry; char *names[1024]; size_t count = 0; json_object *rows = json_object_new_array();
    if (dir) {
        while ((entry = readdir(dir)) && count < 1024) {
            if (strlen(entry->d_name) != 16 || strspn(entry->d_name, "0123456789abcdef") != 16) continue;
            names[count++] = strdup(entry->d_name);
        }
        closedir(dir);
    }
    qsort(names, count, sizeof(*names), name_compare);
    for (size_t i = 0; i < count; i++) {
        char path[F_PATH]; json_object *data;
        if (names[i] && !f_path(path, sizeof(path), directory, names[i]) && (data = show_data(path, names[i]))) {
            json_object *row = json_object_new_object(), *record = f_field(data, "record");
            f_string_add(row, "decision_id", names[i]);
            json_object_object_add(row, "recorded_at", json_object_get(f_field(record, "recorded_at")));
            f_string_add(row, "selected_sha256", f_string(f_field(record, "selected"), "sha256"));
            json_object_object_add(row, "rejected", json_object_new_int64((int64_t)json_object_array_length(f_field(record, "rejected"))));
            f_string_add(row, "replay", f_string(f_field(data, "replay"), "status"));
            json_object_array_add(rows, row); json_object_put(data);
        }
        free(names[i]);
    }
    json_object *out = json_object_new_object(); json_object_object_add(out, "decisions", rows);
    return f_success("workflow plan decision list", out);
}
/* decision-record <dir> <time> <estimates|-> <selected> <reason> [<rejected> <reason>]...
 * decision-show <dir> <id>, decision-view <dir> <id>, decision-list <dir> */
json_object *plan_decision_cli(int argc, char **argv, bool *printed) {
    json_object *data;
    if (!strcmp(argv[0], "decision-record")) return record_command(argc, argv);
    if (!strcmp(argv[0], "decision-list") && argc == 2) return list_command(argv[1]);
    if (argc != 3 || (strcmp(argv[0], "decision-show") && strcmp(argv[0], "decision-view"))) return refuse("invalid_arguments", "expected decision-show or decision-view with a directory and decision ID");
    if (!(data = show_data(argv[1], argv[2]))) return refuse("invalid_decision", "the decision record is missing, changed since it was recorded, or its inputs no longer validate");
    if (!strcmp(argv[0], "decision-view")) { view(data); json_object_put(data); *printed = true; return NULL; }
    return f_success("workflow plan decision show", data);
}
