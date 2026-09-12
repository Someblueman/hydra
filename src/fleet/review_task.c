#include "fleet/review_task.h"
#include "fleet/review_projection.h"
#include "fleet/attention_tui.h"
#include "fleet/support/files.h"
#include "fleet/support/json.h"
#include "fleet/task/task.h"
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#define PREVIEW_LIMIT 4096U
#define PREVIEW_BUDGET (96U * 1024U)
#define DIFF_BUDGET (128U * 1024U)
#define REVIEW_SECONDS 45U
static volatile sig_atomic_t deadline_expired;
static void review_deadline(int signum) { deadline_expired = 1; f_stopped = signum; }

static const char *value(json_object *object, const char *key)
{
    const char *text = f_string(object, key);
    return text && *text ? text : "-";
}
static bool equal(json_object *a, const char *key, json_object *b, const char *other)
{
    const char *left = f_string(a, key), *right = f_string(b, other);
    return left && right && !strcmp(left, right);
}
static bool optional_equal(json_object *selection, const char *key, json_object *actual, const char *other)
{
    return !strcmp(value(selection, key), "-") || equal(selection, key, actual, other);
}
static void copy_field(json_object *to, const char *key, json_object *from, const char *other)
{
    json_object *field = f_field(from, other);
    json_object_object_add(to, key, field ? json_object_get(field) : json_object_new_null());
}
static json_object *initial_review(json_object *selection, json_object *references)
{
    static const char *const fields[] = {"source", "project_id", "host", "task_id", "run_id", "step_id", "attempt_id", "head_id", "current_instance", "request_id", "binding", "revision_sha256", "identity_sha256"};
    json_object *out = json_object_new_object(), *identity = json_object_new_object(); size_t i;
    for (i = 0; i < sizeof(fields) / sizeof(fields[0]); i++) copy_field(identity, fields[i], selection, fields[i]);
    json_object_object_add(out, "identity", identity);
    json_object_object_add(out, "selection", json_object_get(selection));
    json_object_object_add(out, "references", json_object_get(references));
    json_object_object_add(out, "accepted", json_object_new_boolean(false));
    json_object_object_add(out, "current_head_authority", json_object_new_boolean(false));
    f_string_add(out, "scope", !strcmp(value(selection, "head_id"), "-") ? "retained bundle; no head selected" : "retained selected head");
    if (strcmp(value(selection, "request_id"), "-")) f_string_add(out, "scope", "exact approval request");
    f_string_add(out, "readiness", "unavailable"); f_string_add(out, "candidate_state", "unavailable");
    f_string_add(out, "revision_state", "unavailable");
    f_string_add(out, "integration", "unavailable: review creates no collection or destination binding");
    return out;
}
static void unavailable(json_object *out, json_object *response, const char *fallback)
{
    const char *code = f_string(f_field(response, "error"), "code");
    f_string_add(out, "unavailable_reason", code ? code : fallback);
    f_string_add(out, "candidate_state", code && !strcmp(code, "evidence_expired") ? "expired" : "unavailable");
    if (f_field(response, "error")) copy_field(out, "error", response, "error");
}
/* Reuse the public observe contract, then the existing attention producer.
 * No overview cache is read or written; only this exact host/task is contacted. */
static json_object *observe_attention(json_object *selection, json_object **observation)
{
    char *args[] = {"observe", (char *)value(selection, "host"), "--id", (char *)value(selection, "task_id")};
    json_object *response = task_remote_cli(4, args), *data, *host, *hosts, *aggregate, *attention;
    *observation = response;
    if (!json_object_get_boolean(f_field(response, "ok"))) return NULL;
    data = json_object_new_object(); hosts = json_object_new_array();
    host = f_success("fleet-overview", json_object_new_object()); f_string_add(host, "host", value(selection, "host"));
    json_object *host_data = f_field(host, "data"), *tasks = json_object_new_array(), *freshness = json_object_new_object();
    json_object_array_add(tasks, json_object_get(f_field(f_field(response, "data"), "task")));
    json_object_object_add(host_data, "tasks", tasks); f_string_add(freshness, "state", "fresh");
    json_object_object_add(host_data, "freshness", freshness);
    copy_field(host_data, "receiver_observed_at", f_field(response, "data"), "receiver_observed_at");
    json_object_array_add(hosts, host); json_object_object_add(data, "hosts", hosts);
    aggregate = f_success("fleet-overview", data); attention = f_attention_aggregate(aggregate); json_object_put(aggregate);
    return attention;
}
static json_object *current_selection(json_object *attention, json_object *selection, char revision[65])
{
    json_object *items = f_field(f_field(attention, "data"), "items"), *found = NULL; size_t i;
    if (!json_object_is_type(items, json_type_array)) return NULL;
    for (i = 0; i < json_object_array_length(items); i++) {
        json_object *item = json_object_array_get_idx(items, i); char actual[65], identity[65];
        if (!equal(item, "task_id", selection, "task_id") || !equal(item, "run_id", selection, "run_id")) continue;
        if (f_attention_item_hashes(item, actual, identity)) return NULL;
        if (strcmp(identity, value(selection, "identity_sha256"))) continue;
        if (found) return NULL;
        found = item; memcpy(revision, actual, 65U);
    }
    return found;
}
static bool configuration_matches(json_object *result, json_object *observation)
{
    static const char *const fields[] = {"source", "work", "host", "project", "completion"};
    json_object *configuration = f_field(f_field(f_field(observation, "data"), "task"), "effective_configuration");
    size_t i;
    if (!result) return true;
    for (i = 0; i < sizeof(fields) / sizeof(fields[0]); i++) {
        json_object *expected = f_field(f_field(result, "spec"), fields[i]);
        if (!expected || !json_object_equal(expected, f_field(configuration, fields[i]))) return false;
    }
    return true;
}
static bool record_observation(json_object *out, json_object *selection, json_object *result)
{
    if (f_stopped) return false;
    json_object *response = NULL, *attention = observe_attention(selection, &response);
    char revision[65]; json_object *current = current_selection(attention, selection, revision);
    bool matches = current && !strcmp(revision, value(selection, "revision_sha256"));
    if (current) {
        f_string_add(f_field(out, "identity"), "actual_revision_sha256", revision);
        json_object_object_add(out, "attention", json_object_get(current));
        f_string_add(out, "revision_state", matches ? "matches" : "changed");
    } else {
        f_string_add(out, "revision_state", attention ? "selection_replaced" : "unavailable");
        unavailable(out, response, "selection_replaced");
    }
    if (matches && !configuration_matches(result, response)) { matches = false; f_string_add(out, "configuration_state", "changed"); }
    json_object_put(attention); json_object_put(response); return matches;
}
static json_object *evidence_file(json_object *result, const char *path)
{
    json_object *files = f_field(result, "evidence"); size_t i;
    for (i = 0; i < json_object_array_length(files); i++) {
        json_object *file = json_object_array_get_idx(files, i);
        if (!strcmp(value(file, "path"), path)) return file;
    }
    return NULL;
}
static unsigned hex_digit(char c) { return (unsigned)(c <= '9' ? c - '0' : c - 'a' + 10); }
static char *file_text(json_object *file, size_t limit)
{
    const char *hex = f_string(file, "hex"); size_t i, n; char *text;
    if (!hex) return NULL;
    n = strlen(hex) / 2U; if (n > limit) n = limit;
    text = malloc(n + 1U); if (!text) return NULL;
    for (i = 0; i < n; i++) {
        unsigned c = hex_digit(hex[i * 2U]) * 16U + hex_digit(hex[i * 2U + 1U]);
        text[i] = (c < 32U && c != '\n' && c != '\t') || c >= 127U ? '?' : (char)c;
    }
    text[n] = '\0'; return text;
}
static bool scalar_matches(json_object *result, const char *path, const char *expected)
{
    json_object *file = evidence_file(result, path); char *text = file_text(file, 256U); bool matches = false;
    if (text && strlen(text) < 255U) {
        size_t length = strlen(text);
        if (length && text[length - 1U] == '\n') text[length - 1U] = '\0';
        matches = !strcmp(text, expected);
    }
    free(text); return matches;
}
static bool workflow_attempt(json_object *result, json_object *selection, json_object *checks)
{
    char path[F_PATH], prefix[F_PATH]; const char *attempt = value(selection, "attempt_id");
    if (strncmp(attempt, "attempt-", 8U) || !attempt[8] || strspn(attempt + 8, "0123456789") != strlen(attempt + 8)) return false;
    if (snprintf(prefix, sizeof(prefix), "workflows/runs/%s/steps/%s", value(selection, "run_id"), value(selection, "step_id")) >= (int)sizeof(prefix)) return false;
    if (f_path(path, sizeof(path), prefix, "authoritative-attempt") || !scalar_matches(result, path, attempt + 8)) return false;
    copy_field(checks, "authoritative_attempt_evidence", evidence_file(result, path), "sha256");
    if (snprintf(path, sizeof(path), "%s/%s/exit-code", prefix, attempt) >= (int)sizeof(path)) return false;
    f_string_add(checks, "attempt_evidence_path", path);
    copy_field(checks, "attempt_evidence_sha256", evidence_file(result, path), "sha256");
    return scalar_matches(result, path, "0");
}
static bool head_binding(json_object *result, json_object *selection)
{
    json_object *heads = f_field(result, "heads"); size_t i;
    if (!strcmp(value(selection, "head_id"), "-") && !strcmp(value(selection, "current_instance"), "-")) return true;
    for (i = 0; i < json_object_array_length(heads); i++) {
        json_object *head = json_object_array_get_idx(heads, i);
        if (equal(selection, "head_id", head, "head_id") && optional_equal(selection, "current_instance", head, "instance_id")) return true;
    }
    return false;
}
static bool result_binding(json_object *result, json_object *selection, json_object *current_receipt)
{
    json_object *receipt = f_field(result, "receipt"), *state = f_field(receipt, "runtime"), *spec = f_field(result, "spec");
    return equal(receipt, "task_id", selection, "task_id") && equal(receipt, "spec_sha256", selection, "binding") &&
        equal(state, "run_id", selection, "run_id") && equal(spec, "host", selection, "host") &&
        equal(receipt, "project", current_receipt, "project") && equal(receipt, "project_id", current_receipt, "project_id") &&
        equal(state, "execution_project_id", f_field(current_receipt, "runtime"), "execution_project_id") &&
        optional_equal(selection, "project_id", state, "execution_project_id") && head_binding(result, selection);
}
static bool artifact_binding(json_object *out, json_object *result)
{
    const char *revision = f_string(f_field(out, "attention"), "revision");
    json_object *semantic = revision ? f_parse(revision) : NULL;
    json_object *files = f_parse_value(json_object_to_json_string_ext(f_field(result, "artifacts"), JSON_C_TO_STRING_PLAIN));
    size_t i; bool matches;
    for (i = 0; i < json_object_array_length(files); i++) json_object_object_del(json_object_array_get_idx(files, i), "hex");
    matches = semantic && json_object_equal(files, f_field(semantic, "artifact_inventory"));
    json_object_put(files); json_object_put(semantic); return matches;
}
static void inventory(json_object *out, json_object *result, const char *key, size_t *budget)
{
    json_object *files = f_field(result, key), *rows = json_object_new_array(); size_t i;
    for (i = 0; i < json_object_array_length(files); i++) {
        json_object *file = json_object_array_get_idx(files, i), *row = json_object_new_object();
        size_t size = (size_t)json_object_get_int64(f_field(file, "bytes")), limit = *budget < PREVIEW_LIMIT ? *budget : PREVIEW_LIMIT;
        char *preview = file_text(file, limit);
        copy_field(row, "path", file, "path"); copy_field(row, "sha256", file, "sha256"); copy_field(row, "bytes", file, "bytes");
        if (f_field(file, "head_id")) copy_field(row, "head_id", file, "head_id");
        f_string_add(row, "state", "verified");
        f_string_add(row, "preview_encoding", "ASCII preview; other bytes replaced");
        if (preview) { f_string_add(row, "preview", preview); *budget -= strlen(preview); free(preview); }
        json_object_object_add(row, "preview_truncated", json_object_new_boolean(size > limit));
        json_object_array_add(rows, row);
    }
    json_object_object_add(out, key, rows);
}
static int git_ok(const char *scratch, char *const args[])
{
    struct f_capture capture = {0}; int status = task_git(scratch, args, &capture);
    f_capture_free(&capture); return status;
}
static bool import_bundle(json_object *result, const char *scratch)
{
    const char *source = f_string(f_field(f_field(result, "spec"), "source"), "commit"); char path[F_PATH];
    char *init[] = {"init", "--bare", "--template=", strlen(source) == 64U ? "--object-format=sha256" : "--object-format=sha1", NULL};
    char *fetch[] = {"fetch", "--no-tags", "--no-write-fetch-head", "result.bundle", "+refs/heads/*:refs/heads/*", NULL};
    return !f_path(path, sizeof(path), scratch, "result.bundle") && !f_hex_write(path, f_string(result, "bundle_hex"), 0600) &&
        !git_ok(scratch, init) && !git_ok(scratch, fetch);
}
static json_object *head_diff(json_object *head, const char *source, const char *scratch, size_t *budget)
{
    json_object *row = json_object_new_object(); struct f_capture capture = {0}; size_t length;
    char *args[] = {"--no-pager", "diff", "--no-ext-diff", "--no-textconv", "--no-color", "--src-prefix=a/", "--dst-prefix=b/", "--unified=3", (char *)source, (char *)f_string(head, "commit"), "--", NULL};
    copy_field(row, "head_id", head, "head_id"); copy_field(row, "instance_id", head, "instance_id");
    copy_field(row, "branch", head, "branch"); copy_field(row, "head_commit", head, "commit"); copy_field(row, "dirty", head, "dirty");
    f_string_add(row, "source_commit", source); f_string_add(row, "scope", "recorded commits; uncommitted artifacts are listed separately");
    f_string_add(row, "state", "unavailable");
    if (!*budget) { f_string_add(row, "reason", "preview_budget_exhausted"); goto cleanup; }
    if (task_git(scratch, args, &capture) || !capture.out) { f_string_add(row, "reason", "diff_capture_failed"); goto cleanup; }
    length = capture.out_bytes < *budget ? capture.out_bytes : *budget;
    /* The JSON view retains exact diff bytes except NUL, which Git does not emit
     * without -z. The terminal projection separately neutralizes controls. */
    if (memchr(capture.out, '\0', capture.out_bytes)) { f_string_add(row, "reason", "invalid_diff_text"); goto cleanup; }
    json_object_object_add(row, "text", json_object_new_string_len(capture.out, (int)length));
    json_object_object_add(row, "truncated", json_object_new_boolean(length < capture.out_bytes));
    f_string_add(row, "state", "available"); *budget -= length;
cleanup:
    f_capture_free(&capture); return row;
}
static void diffs(json_object *out, json_object *result, json_object *selection)
{
    char scratch[] = "/tmp/hydra-review-diff.XXXXXX"; size_t i, budget = DIFF_BUDGET;
    json_object *rows = json_object_new_array(), *heads = f_field(result, "heads");
    json_object_object_add(out, "diffs", rows);
    if (!mkdtemp(scratch)) { f_string_add(out, "diff_state", "temporary_storage_unavailable"); return; }
    if (!import_bundle(result, scratch)) { f_string_add(out, "diff_state", "bundle_import_failed"); goto cleanup; }
    for (i = 0; i < json_object_array_length(heads); i++) {
        json_object *head = json_object_array_get_idx(heads, i);
        if (f_stopped) break;
        if (!optional_equal(selection, "head_id", head, "head_id")) continue;
        json_object_array_add(rows, head_diff(head, f_string(f_field(f_field(result, "spec"), "source"), "commit"), scratch, &budget));
    }
    f_string_add(out, "diff_state", "recorded_bundle");
cleanup:
    f_remove_tree(scratch);
}
static bool reviewed_result(json_object *out, json_object *result, json_object *selection, json_object *collection)
{
    json_object *checks = json_object_new_object(), *receipt = f_field(result, "receipt"), *state = f_field(receipt, "runtime"), *spec = f_field(result, "spec");
    json_object *provenance = json_object_new_object(); size_t budget = PREVIEW_BUDGET; bool passed;
    f_string_add(checks, "integrity", "verified by task_result_verify"); copy_field(checks, "completion_policy", spec, "completion"); copy_field(checks, "execution_state", state, "state");
    passed = !strcmp(value(state, "state"), "succeeded");
    if (!strcmp(value(f_field(spec, "work"), "kind"), "workflow")) passed = workflow_attempt(result, selection, checks) && passed;
    else if (strcmp(value(selection, "step_id"), "unavailable") || strcmp(value(selection, "attempt_id"), "-")) passed = false;
    f_string_add(checks, "state", passed ? "passed" : "failed_or_unavailable");
    f_string_add(checks, "scope", "retained engine completion evidence; no human acceptance");
    json_object_object_add(out, "checks", checks);
    copy_field(provenance, "result_sha256", collection, "result_sha256"); copy_field(provenance, "bundle_sha256", result, "bundle_sha256");
    copy_field(provenance, "spec_sha256", receipt, "spec_sha256"); copy_field(provenance, "project", receipt, "project");
    copy_field(provenance, "registered_project_id", receipt, "project_id"); copy_field(provenance, "execution_project_id", state, "execution_project_id");
    copy_field(provenance, "source_commit", f_field(spec, "source"), "commit"); copy_field(provenance, "heads", result, "heads");
    f_string_add(provenance, "current_process_binding", "unavailable: retained immutable evidence only"); json_object_object_add(out, "provenance", provenance);
    inventory(out, result, "artifacts", &budget); inventory(out, result, "evidence", &budget); diffs(out, result, selection);
    f_string_add(out, "candidate_state", "verified_retained"); return passed;
}
/* task_git honors the process environment. Reject repository redirects before
 * invoking any verifier/import, so review cannot write outside private scratch. */
static bool clean_git_environment(void)
{
    static const char *const redirects[] = {"GIT_DIR", "GIT_WORK_TREE", "GIT_COMMON_DIR", "GIT_INDEX_FILE", "GIT_OBJECT_DIRECTORY", "GIT_ALTERNATE_OBJECT_DIRECTORIES", "GIT_SHALLOW_FILE", "GIT_GRAFT_FILE", "GIT_CONFIG", "GIT_CONFIG_SYSTEM", "GIT_CONFIG_GLOBAL", "GIT_CONFIG_COUNT", "GIT_CONFIG_PARAMETERS", "GIT_EXEC_PATH"};
    size_t i;
    for (i = 0; i < sizeof(redirects) / sizeof(redirects[0]); i++) if (getenv(redirects[i])) return false;
    return true;
}
static json_object *selected_request(json_object *data, json_object *selection)
{
    json_object *rows = f_field(data, "requests"), *found = NULL; size_t i;
    if (!json_object_is_type(rows, json_type_array) || json_object_array_length(rows) > 512U) return NULL;
    for (i = 0; i < json_object_array_length(rows); i++) {
        json_object *row = json_object_array_get_idx(rows, i);
        if (!equal(row, "request_id", selection, "request_id")) continue;
        if (found || !equal(row, "step_id", selection, "step_id")) return NULL;
        found = row;
    }
    return found;
}
static bool request_pending(json_object *request)
{
    json_object *expiry = f_field(request, "expires_at"); const char *decision = f_string(request, "decision");
    int64_t expires = json_object_get_int64(expiry);
    return !strcmp(value(request, "state"), "pending") && decision && !*decision &&
        (task_hex(f_string(request, "binding"), 40U) || task_hex(f_string(request, "binding"), 64U)) && json_object_is_type(expiry, json_type_int) &&
        expires >= 0 && (!expires || expires > (int64_t)time(NULL));
}
static bool request_observation_binding(json_object *out, json_object *request)
{
    const char *revision = f_string(f_field(out, "attention"), "revision");
    json_object *semantic = revision ? f_parse(revision) : NULL, *expiry = f_field(request, "expires_at");
    char expected[32]; bool matches;
    (void)snprintf(expected, sizeof(expected), "%lld", (long long)json_object_get_int64(expiry));
    matches = json_object_is_type(expiry, json_type_int) &&
        equal(request, "state", semantic, "request_state") && equal(request, "message", semantic, "request_message") &&
        !strcmp(value(semantic, "expires_at"), expected);
    json_object_put(semantic); return matches;
}
static void decision_context(json_object *out, json_object *selection)
{
    const char *choices[] = {"approve", "reject"}; json_object *actions = json_object_new_object(); size_t i, j;
    for (i = 0; i < 2U; i++) {
        json_object *args = json_object_new_array();
        const char *words[] = {"hydra", "fleet", "task", "decide", value(selection, "host"), "--id", value(selection, "task_id"), "--request", value(selection, "request_id"), "--decision", choices[i], "--trust-spec", value(selection, "binding")};
        for (j = 0; j < sizeof(words) / sizeof(words[0]); j++) json_object_array_add(args, json_object_new_string(words[j]));
        json_object_object_add(actions, choices[i], args);
    }
    json_object_object_add(actions, "executed", json_object_new_boolean(false));
    f_string_add(actions, "policy", "explicit decision required; receiver revalidates request and evidence"); json_object_object_add(out, "actions", actions);
}
static void review_request(json_object *out, json_object *selection, bool before)
{
    char *args[] = {"requests", (char *)value(selection, "host"), "--id", (char *)value(selection, "task_id")};
    json_object *response = task_remote_cli(4, args), *data = f_field(response, "data"), *request = selected_request(data, selection);
    bool bound = equal(data, "task_id", selection, "task_id") && equal(data, "spec_sha256", selection, "binding") &&
        equal(f_field(data, "runtime"), "run_id", selection, "run_id");
    if (!json_object_get_boolean(f_field(response, "ok"))) { unavailable(out, response, "request_unavailable"); goto cleanup; }
    if (!bound || !request) { f_string_add(out, "readiness", "revoked"); unavailable(out, NULL, "request_binding_changed"); goto cleanup; }
    json_object_object_add(out, "request", json_object_get(request));
    f_string_add(out, "request_evidence", "receiver request binding and locator; remote file is not opened locally");
    f_string_add(out, "candidate_state", "request_available");
    bool after = record_observation(out, selection, NULL);
    bool pending = before && after && request_pending(request) && request_observation_binding(out, request);
    f_string_add(out, "readiness", pending ? "pending" : "revoked");
    if (pending) decision_context(out, selection);
cleanup:
    json_object_put(response);
}
static json_object *review_task(json_object *selection, json_object *references)
{
    json_object *out = initial_review(selection, references), *response = NULL, *data, *collection, *result;
    char *args[] = {"result", (char *)value(selection, "host"), "--id", (char *)value(selection, "task_id"), "--timeout", "5"};
    bool before, after, verified;
    if (!clean_git_environment()) { unavailable(out, NULL, "git_environment_redirect"); goto cleanup; }
    before = record_observation(out, selection, NULL);
    if (strcmp(value(selection, "request_id"), "-")) { review_request(out, selection, before); goto cleanup; }
    response = task_remote_cli(6, args); data = f_field(response, "data"); collection = f_field(data, "collection"); result = f_field(collection, "result");
    if (!json_object_get_boolean(f_field(response, "ok"))) { unavailable(out, response, "result_unavailable"); goto cleanup; }
    if (!result_binding(result, selection, data)) { f_string_add(out, "readiness", "revoked"); unavailable(out, NULL, "retained_identity_mismatch"); goto cleanup; }
    verified = reviewed_result(out, result, selection, collection);
    if (!artifact_binding(out, result)) { verified = false; f_string_add(out, "subject_state", "observation_binding_unavailable_or_changed"); }
    else f_string_add(out, "subject_state", "matches_observation");
    after = record_observation(out, selection, result);
    f_string_add(out, "readiness", before && after && verified && !strcmp(value(selection, "kind"), "result") ? "ready" : "revoked");
cleanup:
    json_object_put(response); return f_success("fleet-review", out);
}
static json_object *review_dispatch(int argc, char **argv, bool text_projection)
{
    json_object *selection = NULL, *input = NULL, *references = NULL, *response;
    if ((argc != 13 && argc != 14) || !(selection = review_selection_parse("fleet overview", 13, argv)))
        return f_error("fleet-review", "invalid_selection", "use fleet review[-data] KIND PROJECT HOST TASK RUN STEP ATTEMPT HEAD INSTANCE REQUEST BINDING REVISION_SHA256 IDENTITY_SHA256 [REFERENCES_JSON]");
    if (!f_name(value(selection, "host")) || strncmp(value(selection, "task_id"), "task_", 5U) ||
        !task_hex(value(selection, "task_id") + 5U, 64U) || !task_hex(value(selection, "binding"), 64U)) {
        response = f_error("fleet-review", "invalid_selection", "select an exact Fleet task, host and specification digest"); goto cleanup;
    }
    if (argc == 14 && (strlen(argv[13]) > 65536U || !(input = f_parse_value(argv[13])))) {
        response = f_error("fleet-review", "invalid_reference", "references must be a bounded explicit JSON array"); goto cleanup;
    }
    references = review_references(input);
    if (!references) { response = f_error("fleet-review", "invalid_reference", "references must be explicit transcript, log or PR locators"); goto cleanup; }
    response = review_task(selection, references);
    if (text_projection) {
        int status = review_projection(response, selection); json_object_put(response);
        response = status ? f_error("fleet-review", "projection_failed", "review could not be encoded within the native text bounds") : NULL;
    }
cleanup:
    json_object_put(references); json_object_put(input); json_object_put(selection); return response;
}
json_object *review_task_cli(int argc, char **argv, bool text_projection)
{
    struct sigaction action = {0}, previous; json_object *response;
    action.sa_handler = review_deadline; sigemptyset(&action.sa_mask); deadline_expired = 0;
    if (sigaction(SIGALRM, &action, &previous)) return f_error("fleet-review", "deadline_unavailable", "cannot bound the review operation");
    alarm(REVIEW_SECONDS); response = review_dispatch(argc, argv, text_projection); alarm(0);
    (void)sigaction(SIGALRM, &previous, NULL);
    if (deadline_expired) {
        json_object_put(response);
        response = f_error("fleet-review", "review_timeout", "the 45-second review deadline expired; no current readiness is established");
    }
    return response;
}
