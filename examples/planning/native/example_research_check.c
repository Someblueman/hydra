#include "example_research.h"
#include <stdio.h>
#include <string.h>

static const char *const cases[] = {"fcfs", "sjf", "recommendation", "scope", "provenance"};
static const char *const obligations[] = {"means", "tails", "fairness", "recommendation", "reproduce", "limits"};
static const char *const metrics[] = {"mean_wait", "mean_turnaround", "p95_turnaround", "max_wait", "worst_wait_job"};

static void copy(json_object *to, const char *key, json_object *from) {
    json_object_object_add(to, key, json_object_get(json_object_object_get(from, key)));
}

static bool strings_equal(json_object *value, const char *const *strings, size_t count) {
    json_object *expected = ex_strings(strings, count);
    bool equal = ex_equal(value, expected); json_object_put(expected); return equal;
}

static bool scope_ok(json_object *report, json_object *claims) {
    const char *const report_keys[] = {"schema_version", "question", "provenance", "policies", "claims", "claim_locations", "limitations"};
    const char *const claim_keys[] = {"question", "recommendation", "constraint_checks", "limits", "competing_explanations"};
    json_object *unused;
    for (size_t i = 0; i < 7; i++)
        if (!json_object_object_get_ex(report, report_keys[i], &unused)) return false;
    for (size_t i = 0; i < 5; i++)
        if (!json_object_object_get_ex(claims, claim_keys[i], &unused)) return false;
    return json_object_object_length(report) == 7 &&
        json_object_get_type(json_object_object_get(report, "schema_version")) == json_type_int &&
        json_object_get_int64(json_object_object_get(report, "schema_version")) == 3 &&
        ex_text(report, "question", research_question) && json_object_object_length(claims) == 5 &&
        ex_text(claims, "question", research_question) &&
        strings_equal(json_object_object_get(claims, "limits"), research_limits, 5) &&
        strings_equal(json_object_object_get(claims, "competing_explanations"), research_explanations, 2) &&
        strings_equal(json_object_object_get(report, "claim_locations"), research_locations, 3) &&
        strings_equal(json_object_object_get(report, "limitations"), research_limits, 5);
}

static json_object *policy_raw(json_object *claimed, json_object *computed, bool *agrees) {
    *agrees = json_object_equal(claimed, computed);
    json_object *raw = json_object_new_object(), *actual = NULL;
    if (*agrees) {
        actual = json_object_new_object();
        for (size_t i = 0; i < 5; i++) copy(actual, metrics[i], computed);
    }
    json_object_object_add(raw, "actual", actual);
    json_object_object_add(raw, "claimed", json_object_get(claimed));
    json_object_object_add(raw, "recomputed", json_object_get(computed));
    return raw;
}

static json_object *recommendation_raw(json_object *claims, json_object **computed, bool *agrees) {
    const char *const names[] = {"FCFS", "SJF"};
    json_object *checks = json_object_new_object(); const char *recommendation = "neither qualifies";
    for (size_t i = 0; i < 2; i++) {
        bool qualifies = json_object_get_int64(json_object_object_get(computed[i], "p95_turnaround")) <= 22 &&
                         json_object_get_int64(json_object_object_get(computed[i], "max_wait")) <= 16;
        json_object_object_add(checks, names[i], json_object_new_boolean(qualifies));
        if (qualifies && !strcmp(recommendation, "neither qualifies")) recommendation = names[i];
    }
    json_object *expected = json_object_new_object(), *actual = json_object_new_object();
    json_object_object_add(expected, "recommendation", json_object_new_string(recommendation));
    json_object_object_add(expected, "constraint_checks", checks);
    copy(actual, "recommendation", claims); copy(actual, "constraint_checks", claims);
    *agrees = ex_equal(actual, expected);
    json_object *raw = json_object_new_object();
    json_object_object_add(raw, "actual", actual); json_object_object_add(raw, "recomputed", expected);
    return raw;
}

static void assess(json_object *report, json_object *claims, json_object **computed,
                   const char *hash, json_object **raws, bool *agrees) {
    json_object *policies = json_object_object_get(report, "policies");
    raws[0] = policy_raw(json_object_object_get(policies, "FCFS"), computed[0], &agrees[0]);
    raws[1] = policy_raw(json_object_object_get(policies, "SJF"), computed[1], &agrees[1]);
    raws[2] = recommendation_raw(claims, computed, &agrees[2]);
    agrees[3] = scope_ok(report, claims);
    raws[3] = json_object_new_object();
    json_object_object_add(raws[3], "actual", json_object_new_string(agrees[3] ? "pass" : "fail"));
    copy(raws[3], "question", report); copy(raws[3], "claims", report);
    copy(raws[3], "claim_locations", report); copy(raws[3], "limitations", report);
    json_object *expected = research_provenance(hash);
    agrees[4] = ex_equal(json_object_object_get(report, "provenance"), expected);
    raws[4] = json_object_new_object();
    json_object_object_add(raws[4], "actual", json_object_new_string(agrees[4] ? "pass" : "fail"));
    json_object_object_add(raws[4], "claimed", json_object_get(json_object_object_get(report, "provenance")));
    json_object_object_add(raws[4], "expected", expected);
}

int ex_research_check(void) {
    struct research_data data;
    json_object *report = ex_input("subject"), *computed[2] = {NULL, NULL};
    bool loaded = research_load(&data) == 0;
    bool valid = loaded && json_object_get_type(report) == json_type_object;
    json_object *claims = json_object_object_get(report, "claims");
    json_object *policies = json_object_object_get(report, "policies");
    /* Missing objects behave like empty dictionaries in the original checker. */
    json_object *empty = json_object_new_object();
    if (!claims && !json_object_object_get_ex(report, "claims", &claims)) claims = empty;
    if (!policies && !json_object_object_get_ex(report, "policies", &policies)) policies = empty;
    valid = valid && json_object_get_type(claims) == json_type_object && json_object_get_type(policies) == json_type_object;
    if (valid) {
        computed[0] = research_recompute(&data, false); computed[1] = research_recompute(&data, true);
        valid = computed[0] && computed[1];
    }
    json_object *raws[5] = {NULL}; bool agrees[5] = {false};
    if (valid) assess(report, claims, computed, data.hash, raws, agrees);
    json_object *observations = json_object_new_array(); size_t failed = 0;
    char evidence[512] = "Independent schedules, metrics, source and fixed-scope claim checks. Failed cases: ";
    bool observe_failed = false;
    for (size_t i = 0; i < 5; i++) {
        if (!valid) {
            raws[i] = json_object_new_object();
            json_object_object_add(raws[i], "actual", NULL);
            json_object_object_add(raws[i], "error", json_object_new_string("Invalid subject, 12-job source table, or arithmetic overflow"));
        }
        if (!agrees[i]) {
            size_t used = strlen(evidence);
            (void)snprintf(evidence + used, sizeof(evidence) - used, "%s%s", failed ? ", " : "", cases[i]); failed++;
        }
        if (ex_observe(observations, cases[i], raws[i], true)) observe_failed = true;
        json_object_put(raws[i]);
    }
    if (!failed) {
        size_t used = strlen(evidence);
        (void)snprintf(evidence + used, sizeof(evidence) - used, "%s", "none");
    }
    const char *const argv_text[] = {"./plan-example", "research-check"};
    const char *const limitation[] = {"Fixed finite trace and exact claim schema; no general prose or production inference."};
    json_object *argv = ex_strings(argv_text, 2), *requirements = ex_strings(obligations, 6);
    json_object *limits = ex_strings(limitation, 1);
    int result = observe_failed ? 2 : ex_evidence("assessment", "assessment", "assessment-recipe", "research-checker-v5",
        argv, requirements, requirements, observations, failed, valid, true, limits, evidence);
    json_object_put(argv); json_object_put(requirements); json_object_put(limits); json_object_put(observations);
    json_object_put(computed[0]); json_object_put(computed[1]); json_object_put(report); json_object_put(empty);
    if (loaded) research_free(&data);
    return result;
}
