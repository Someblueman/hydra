#include "fleet/review.h"
#include "fleet/attention_tui.h"
#include "fleet/plan/plan.h"
#include "fleet/review_contract.h"
#include "fleet/review_projection.h"
#include "fleet/support/files.h"
#include "fleet/task/task.h"
#include "fleet/workflow/workflow_data.h"
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>

struct review_context {
    char root[F_PATH], run[F_PATH], step[F_PATH], attempt[F_PATH], selected_attempt[32];
    char revision[65], identity[65];
    json_object *attention, *selected, *retained, *data;
};

static const char *value(json_object *object, const char *key)
{
    const char *text = f_string(object, key);
    return text && *text ? text : "-";
}

static void copy_field(json_object *out, const char *key, json_object *from, const char *field)
{
    json_object *child = f_field(from, field);
    json_object_object_add(out, key, child ? json_object_get(child) : json_object_new_null());
}

static void scalar_add(json_object *out, const char *key, const char *directory, const char *name)
{
    char *text = review_scalar(directory, name);
    if (text)
        f_string_add(out, key, text);
    free(text);
}

static bool selected_name(const char *input, char out[32])
{
    const char *number = input;
    char *end;
    long n;
    if (!input || !strcmp(input, "-"))
        return !f_copy(out, 32, "-");
    if (!strncmp(input, "attempt-", 8))
        number += 8;
    if (!*number || *number < '1' || *number > '9' || strlen(number) > 2)
        return false;
    n = strtol(number, &end, 10);
    return !*end && n <= 11 && snprintf(out, 32, "attempt-%ld", n) < 32;
}

static bool review_paths(struct review_context *ctx, const char *project, const char *run, const char *step,
                         const char *attempt, const char *revision)
{
    const char *root = getenv("HYDRA_STATE_V2_ROOT");
    if (!f_name(project) || !f_name(run) || !plan_id(step) ||
        !selected_name(attempt, ctx->selected_attempt) || !task_hex(revision, 64))
        return false;
    if (root ? f_copy(ctx->root, sizeof(ctx->root), root)
             : f_path(ctx->root, sizeof(ctx->root), f_home, "state/v2"))
        return false;
    return snprintf(ctx->run, sizeof(ctx->run), "%s/projects/%s/workflows/runs/%s", ctx->root, project, run) <
               (int)sizeof(ctx->run) &&
           snprintf(ctx->step, sizeof(ctx->step), "%s/steps/%s", ctx->run, step) < (int)sizeof(ctx->step) &&
           !f_path(ctx->attempt, sizeof(ctx->attempt), ctx->step, ctx->selected_attempt);
}

static json_object *find_candidate(json_object *attention, const char *run, const char *step,
                                   const char *attempt, json_object *selection)
{
    json_object *items = f_field(f_field(attention, "data"), "items"), *found = NULL;
    size_t i;
    if (!json_object_get_boolean(f_field(attention, "ok")) || !json_object_is_type(items, json_type_array))
        return NULL;
    for (i = 0; i < json_object_array_length(items); i++) {
        json_object *item = json_object_array_get_idx(items, i);
        char revision[65], identity[65];
        if (strcmp(value(item, "run_id"), run) || strcmp(value(item, "step_id"), step) ||
            strcmp(value(item, "attempt_id"), attempt))
            continue;
        if (selection && (f_attention_item_hashes(item, revision, identity) ||
                          strcmp(identity, value(selection, "identity_sha256"))))
            continue;
        if (found)
            return NULL;
        found = item;
    }
    return found;
}

static json_object *identity(json_object *selected, json_object *selection, const char *revision,
                             const char *actual, const char *identity_hash)
{
    static const char *const fields[] = {"source",     "kind",    "project_id", "host",    "task_id",
                                         "run_id",     "step_id", "attempt_id", "head_id", "current_instance",
                                         "request_id", "binding"};
    json_object *out = json_object_new_object(), *original = selection ? selection : selected;
    size_t i;
    for (i = 0; i < sizeof(fields) / sizeof(fields[0]); i++)
        copy_field(out, fields[i], original, fields[i]);
    f_string_add(out, "revision_sha256", revision);
    f_string_add(out, "identity_sha256", selection ? value(selection, "identity_sha256") : identity_hash);
    f_string_add(out, "actual_revision_sha256", actual);
    return out;
}

static json_object *review_checks(const struct review_context *ctx)
{
    char *authoritative = review_scalar(ctx->step, "authoritative-attempt"),
         *latest = review_scalar(ctx->step, "attempts");
    json_object *out = json_object_new_object(), *delivery = NULL;
    bool current = authoritative && latest && strcmp(ctx->selected_attempt, "-") &&
                   !strcmp(authoritative, ctx->selected_attempt + 8) && !strcmp(latest, authoritative);
    bool compiled = json_object_get_boolean(f_field(ctx->retained, "compiled"));
    json_object_object_add(out, "authoritative", json_object_new_boolean(current));
    if (current && compiled && !strcmp(value(ctx->retained, "state"), "passed"))
        delivery = plan_delivery(ctx->run);
    f_string_add(out, "state",
                 !current   ? "unavailable"
                 : compiled ? (delivery ? "passed" : "failed")
                            : "unavailable");
    f_string_add(out, "scope", compiled ? "compiled_plan" : "planless");
    f_string_add(out, "reason",
                 current ? "authoritative attempt plan delivery gate"
                         : "selected attempt is not the current plan attempt");
    if (delivery)
        json_object_object_add(out, "delivery", delivery);
    free(authoritative);
    free(latest);
    return out;
}

static bool review_ready(const struct review_context *ctx, json_object *checks, const char *revision)
{
    if (strcmp(ctx->revision, revision) || strcmp(value(ctx->selected, "kind"), "result") ||
        strcmp(value(ctx->selected, "freshness"), "fresh") || !f_field(ctx->selected, "output_receipt") ||
        strcmp(value(ctx->retained, "state"), "passed"))
        return false;
    return !strcmp(value(checks, "state"), "passed") ||
           (!strcmp(value(checks, "state"), "unavailable") && !strcmp(value(checks, "scope"), "planless") &&
            json_object_get_boolean(f_field(checks, "authoritative")));
}

static char *binding_field(const char *text, const char *key)
{
    char *copy = text ? strdup(text) : NULL, *line, *save = NULL, *result = NULL;
    if (!copy)
        return NULL;
    for (line = strtok_r(copy, "\n", &save); line; line = strtok_r(NULL, "\n", &save)) {
        char *tab = strchr(line, '\t');
        if (!tab)
            continue;
        *tab = 0;
        if (strcmp(line, key))
            continue;
        if (result) {
            free(result);
            result = NULL;
            break;
        }
        result = strdup(tab + 1);
    }
    free(copy);
    return result;
}

static bool request_identity(json_object *request, const char *directory)
{
    char scratch[] = "/tmp/hydra-review-request.XXXXXX", path[F_PATH];
    char *text = NULL, *head = NULL, *instance = NULL;
    bool ok = false;
    if (!mkdtemp(scratch))
        return false;
    if (f_path(path, sizeof(path), scratch, "binding.tsv") ||
        task_file_copy(directory, "binding.tsv", path) || !(text = f_read(path, 16384)))
        goto cleanup;
    head = binding_field(text, "head");
    instance = binding_field(text, "instance");
    if (!head || !instance)
        goto cleanup;
    f_string_add(request, "head", head);
    f_string_add(request, "instance", instance);
    ok = true;
cleanup:
    free(head);
    free(instance);
    free(text);
    if (f_remove_tree(scratch))
        ok = false;
    return ok;
}

static void command_add(json_object *actions, const char *key, const char *run, const char *request,
                        const char *action)
{
    const char *args[] = {"hydra", "workflow", "decide", run, request, action};
    json_object *argv = json_object_new_array();
    size_t i;
    for (i = 0; i < sizeof(args) / sizeof(args[0]); i++)
        json_object_array_add(argv, json_object_new_string(args[i]));
    json_object_object_add(actions, key, argv);
}

static bool request_current(json_object *request, const struct review_context *ctx, const char *revision)
{
    const char *expires = f_string(request, "expires_at");
    char *end;
    long long at;
    if (strcmp(ctx->revision, revision) || strcmp(value(ctx->retained, "state"), "passed") ||
        strcmp(value(ctx->selected, "kind"), "approval") ||
        strcmp(value(ctx->selected, "freshness"), "fresh") || strcmp(value(request, "state"), "pending") ||
        !expires || !*expires)
        return false;
    at = strtoll(expires, &end, 10);
    return !*end && at >= 0 && (!at || at > (long long)time(NULL));
}

static void review_request(json_object *out, json_object *actions, const struct review_context *ctx,
                           const char *revision)
{
    const char *request_id = f_string(ctx->selected, "request_id"), *run = value(ctx->selected, "run_id");
    json_object *request;
    char directory[F_PATH], path[F_PATH], route[512];
    if (!request_id || !f_name(request_id) ||
        snprintf(directory, sizeof(directory), "%s/approvals/%s", ctx->run, request_id) >=
            (int)sizeof(directory))
        return;
    request = json_object_new_object();
    f_string_add(request, "request_id", request_id);
    scalar_add(request, "state", directory, "state");
    scalar_add(request, "message", directory, "message");
    scalar_add(request, "expires_at", directory, "expires-at");
    scalar_add(request, "binding", directory, "binding-hash");
    scalar_add(request, "requested_head", directory, "head");
    bool recorded_identity = request_identity(request, directory);
    copy_field(request, "observed_freshness", ctx->selected, "freshness");
    copy_field(request, "observed_reason", ctx->selected, "reason");
    f_string_add(request, "evidence_directory", directory);
    if (!f_path(path, sizeof(path), directory, "binding.tsv"))
        f_string_add(request, "evidence_locator", path);
    if (!f_path(path, sizeof(path), directory, "decision"))
        scalar_add(request, "decision", path, "action");
    bool current =
        recorded_identity && request_current(request, ctx, revision) && !f_field(request, "decision");
    f_string_add(request, "context_state", current ? "current" : "stale_or_unavailable");
    json_object_object_add(out, "request", request);
    if (snprintf(route, sizeof(route), "hydra workflow requests %s --json", run) < (int)sizeof(route))
        f_string_add(actions, "request_route", route);
    if (current) {
        f_string_add(out, "readiness", "pending");
        command_add(actions, "approve_argv", run, request_id, "approve");
        command_add(actions, "reject_argv", run, request_id, "reject");
        f_string_add(actions, "decision_validation",
                     "the public decide command revalidates current bindings before recording a decision");
    }
}

/* Re-observe after all evidence reads. Any changed row, identity or receipt
 * revokes the snapshot; the requested tuple is never replaced by the new row. */
static bool observation_unchanged(const struct review_context *ctx, const char *project)
{
    json_object *attention = wd_attention(project), *item;
    json_object *selection = json_object_new_object();
    char revision[65], identity_hash[65];
    f_string_add(selection, "identity_sha256", ctx->identity);
    item = find_candidate(attention, value(ctx->selected, "run_id"), value(ctx->selected, "step_id"),
                          ctx->selected_attempt, selection);
    bool same = item && !f_attention_item_hashes(item, revision, identity_hash) &&
                !strcmp(revision, ctx->revision) && !strcmp(identity_hash, ctx->identity);
    json_object_put(attention);
    json_object_put(selection);
    return same;
}

static json_object *review_body(struct review_context *ctx, const char *project, const char *revision,
                                json_object *selection, json_object *references)
{
    json_object *out = json_object_new_object(), *checks, *actions = json_object_new_object();
    char path[F_PATH];
    struct stat st;
    ctx->retained = review_retained(ctx->run, project, &ctx->data);
    json_object_object_add(out, "identity",
                           identity(ctx->selected, selection, revision, ctx->revision, ctx->identity));
    if (selection)
        json_object_object_add(out, "selection", json_object_get(selection));
    scalar_add(out, "run_state", ctx->run, "state");
    scalar_add(out, "definition_hash", ctx->run, "definition-hash");
    scalar_add(out, "step_state", ctx->step, "state");
    scalar_add(out, "attempt_state", ctx->attempt, "state");
    checks = review_checks(ctx);
    f_string_add(out, "readiness", review_ready(ctx, checks, revision) ? "ready" : "revoked");
    f_string_add(out, "revision_state", !strcmp(ctx->revision, revision) ? "matches" : "changed");
    json_object_object_add(out, "accepted", json_object_new_boolean(false));
    json_object_object_add(out, "current_head_authority", json_object_new_boolean(false));
    json_object_object_add(out, "retained_contract", json_object_get(ctx->retained));
    bool expired = !f_path(path, sizeof(path), ctx->run, "retention.json") && !lstat(path, &st);
    review_inventory(out, ctx->selected, ctx->data, ctx->attempt, expired);
    json_object_object_add(out, "checks", checks);
    if (snprintf(path, sizeof(path), "%s/projects/%s", ctx->root, project) < (int)sizeof(path))
        scalar_add(actions, "cwd", path, "repo-root");
    review_request(out, actions, ctx, revision);
    f_string_add(actions, "integration",
                 "unavailable: no verified integration binding for this workflow result");
    json_object_object_add(out, "actions", actions);
    json_object_object_add(out, "references", json_object_get(references));
    json_object_object_add(out, "attention", json_object_get(ctx->selected));
    f_string_add(out, "diff", "unavailable: selected workflow attempt has no recorded diff projection");
    if (!observation_unchanged(ctx, project)) {
        f_string_add(out, "readiness", "revoked");
        f_string_add(out, "observation_state", "changed_during_review");
        json_object_object_del(actions, "approve_argv");
        json_object_object_del(actions, "reject_argv");
        if (f_field(out, "request"))
            f_string_add(f_field(out, "request"), "context_state", "changed_during_review");
    }
    const char *readiness = value(out, "readiness");
    f_string_add(out, "candidate_state",
                 !strcmp(readiness, "ready")     ? "verified_retained"
                 : !strcmp(readiness, "pending") ? "current_request"
                                                 : "unknown_or_stale");
    return f_success("workflow-review", out);
}

static json_object *candidate(const char *project, const char *run, const char *step, const char *attempt,
                              const char *revision, json_object *selection, json_object *references)
{
    struct review_context ctx = {0};
    json_object *refs = NULL, *out = NULL;
    if (!review_paths(&ctx, project, run, step, attempt, revision))
        return f_error("workflow-review", "candidate_unavailable",
                       "the selected immutable workflow attempt is unavailable");
    if (!review_git_environment_clean())
        return f_error("workflow-review", "git_environment_redirect",
                       "review requires Git repository redirects to be unset");
    ctx.attention = wd_attention(project);
    ctx.selected = find_candidate(ctx.attention, run, step, ctx.selected_attempt, selection);
    if (!ctx.selected || f_attention_item_hashes(ctx.selected, ctx.revision, ctx.identity)) {
        out = f_error("workflow-review", "candidate_unavailable",
                      "the exact selected candidate is absent, ambiguous or cannot be fingerprinted");
        goto cleanup;
    }
    refs = review_references(references);
    if (!refs) {
        out = f_error("workflow-review", "invalid_reference",
                      "references must be explicit bounded transcript, log or PR locators");
        goto cleanup;
    }
    out = review_body(&ctx, project, revision, selection, refs);
cleanup:
    json_object_put(ctx.data);
    json_object_put(ctx.retained);
    json_object_put(refs);
    json_object_put(ctx.attention);
    return out;
}

json_object *review_candidate(const char *project, const char *run, const char *step, const char *attempt,
                              const char *revision, json_object *references)
{
    return candidate(project, run, step, attempt, revision, NULL, references);
}

int review_emit(json_object *review)
{
    return review ? f_emit(review) : 1;
}

static json_object *selected_review(json_object *selection, const char *reference_text)
{
    json_object *refs = reference_text ? f_parse_value(reference_text) : NULL, *review;
    if (reference_text && !json_object_is_type(refs, json_type_array)) {
        json_object_put(refs);
        return f_error("workflow-review", "invalid_reference", "references must be a bounded JSON array");
    }
    review =
        candidate(value(selection, "project_id"), value(selection, "run_id"), value(selection, "step_id"),
                  value(selection, "attempt_id"), value(selection, "revision_sha256"), selection, refs);
    json_object_put(refs);
    return review;
}

json_object *review_cli(int argc, char **argv)
{
    json_object *refs = NULL, *selection, *review;
    if (argc == 13 || argc == 14) {
        selection = review_selection_parse("workflow records", 13, argv);
        if (!selection)
            return f_error("workflow-review", "invalid_selection",
                           "expected the exact 13-field workflow review selection");
        review = selected_review(selection, argc == 14 ? argv[13] : NULL);
        json_object_put(selection);
        return review;
    }
    if (argc != 5 && argc != 6)
        return f_error("workflow-review", "invalid_input",
                       "use PROJECT RUN STEP ATTEMPT REVISION or the exact 13-field attention selection, "
                       "plus optional REFERENCES_JSON");
    if (argc == 6 && (!(refs = f_parse_value(argv[5])) || !json_object_is_type(refs, json_type_array))) {
        json_object_put(refs);
        return f_error("workflow-review", "invalid_reference", "references must be a bounded JSON array");
    }
    review = review_candidate(argv[0], argv[1], argv[2], argv[3], argv[4], refs);
    json_object_put(refs);
    return review;
}

int review_workflow_data_cli(int argc, char **argv)
{
    json_object *selection = NULL, *review;
    int status;
    if ((argc != 13 && argc != 14) || !(selection = review_selection_parse("workflow records", 13, argv))) {
        review = f_error("workflow-review", "invalid_selection",
                         "expected the exact 13-field workflow review selection");
        status = f_emit(review);
        json_object_put(review);
        return status;
    }
    review = selected_review(selection, argc == 14 ? argv[13] : NULL);
    status = !json_object_get_boolean(f_field(review, "ok"));
    if (review_projection(review, selection))
        status = 1;
    json_object_put(review);
    json_object_put(selection);
    return status;
}
