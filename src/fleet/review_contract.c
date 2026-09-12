#include "fleet/review_contract.h"
#include "fleet/plan/plan.h"
#include "fleet/support/files.h"
#include "fleet/support/process.h"
#include "fleet/task/task.h"
#include "fleet/workflow/workflow_data.h"
#include <fcntl.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

char *review_scalar(const char *directory, const char *name)
{
    char path[F_PATH], bytes[4097];
    struct stat st;
    int fd;
    ssize_t n;
    if (f_path(path, sizeof(path), directory, name))
        return NULL;
    fd = open(path, O_RDONLY | O_NONBLOCK | O_NOFOLLOW);
    if (fd < 0)
        return NULL;
    n = fstat(fd, &st) || !S_ISREG(st.st_mode) ? -1 : read(fd, bytes, sizeof(bytes));
    close(fd);
    if (n < 1 || n > 4096 || memchr(bytes, 0, (size_t)n))
        return NULL;
    if (bytes[n - 1] == '\n')
        n--;
    if (memchr(bytes, '\n', (size_t)n) || memchr(bytes, '\r', (size_t)n))
        return NULL;
    bytes[n] = 0;
    return strdup(bytes);
}

bool review_git_environment_clean(void)
{
    static const char *const redirects[] = {"GIT_DIR",
                                            "GIT_WORK_TREE",
                                            "GIT_COMMON_DIR",
                                            "GIT_INDEX_FILE",
                                            "GIT_OBJECT_DIRECTORY",
                                            "GIT_ALTERNATE_OBJECT_DIRECTORIES",
                                            "GIT_SHALLOW_FILE",
                                            "GIT_GRAFT_FILE",
                                            "GIT_CONFIG",
                                            "GIT_CONFIG_SYSTEM",
                                            "GIT_CONFIG_GLOBAL",
                                            "GIT_CONFIG_COUNT",
                                            "GIT_CONFIG_PARAMETERS",
                                            "GIT_EXEC_PATH"};
    size_t i;
    for (i = 0; i < sizeof(redirects) / sizeof(redirects[0]); i++)
        if (getenv(redirects[i]))
            return false;
    return true;
}

static bool exists(const char *directory, const char *name)
{
    char path[F_PATH];
    struct stat st;
    return !f_path(path, sizeof(path), directory, name) && !lstat(path, &st);
}

static bool compiled_run(const char *run)
{
    static const char *const names[] = {"compiled.json",
                                        "plan-accepted",
                                        "plan-deadline",
                                        "plan-verification.json",
                                        "verification-plan-sha256",
                                        "delivery.json"};
    size_t i;
    for (i = 0; i < sizeof(names) / sizeof(names[0]); i++)
        if (exists(run, names[i]))
            return true;
    return false;
}

static bool copy_record(const char *run, const char *name, const char *scratch)
{
    char path[F_PATH];
    return !f_path(path, sizeof(path), scratch, name) && !task_file_copy(run, name, path);
}

static bool snapshot(const char *run, const char *scratch, bool compiled)
{
    static const char *const names[] = {"resolved.yml",   "definition-hash", "graph.tsv", "base-commit",
                                        "schema-version", "project-id",      "run-id",    "parallelism",
                                        "disk-mb",        "max-heads"};
    size_t i;
    for (i = 0; i < sizeof(names) / sizeof(names[0]); i++)
        if (!copy_record(run, names[i], scratch))
            return false;
    if (exists(run, "data.json") && !copy_record(run, "data.json", scratch))
        return false;
    if (exists(run, "data-hash") && !copy_record(run, "data-hash", scratch))
        return false;
    return !compiled ||
           (copy_record(run, "compiled.json", scratch) && copy_record(run, "plan-accepted", scratch));
}

/* A private bare destination plus an explicit object format makes historical
 * Git blob IDs independent of both the caller's repository and live HEAD.
 * hash-object never writes an object; Git redirects are rejected before init. */
static bool git_oid_file(const char *path, size_t wanted, char out[65])
{
    char scratch[] = "/tmp/hydra-review-git.XXXXXX", format[32];
    char *init[] = {"git", "init", "--bare", "--template=", format, scratch, NULL};
    char *hash[] = {"git", "-C", scratch, "hash-object", (char *)path, NULL};
    struct f_capture capture = {0};
    bool ok = false;
    if (!review_git_environment_clean() || !mkdtemp(scratch))
        return false;
    (void)snprintf(format, sizeof(format), "--object-format=%s", wanted == 64U ? "sha256" : "sha1");
    if (f_run(init, NULL, 0, 30, &capture) || capture.status)
        goto cleanup;
    f_capture_free(&capture);
    if (f_run(hash, NULL, 0, 30, &capture) || capture.status || !capture.out)
        goto cleanup;
    size_t n = strcspn(capture.out, "\r\n");
    if (n == wanted) {
        memcpy(out, capture.out, n);
        out[n] = 0;
        ok = true;
    }
cleanup:
    f_capture_free(&capture);
    if (f_remove_tree(scratch))
        ok = false;
    return ok;
}

static bool blob_matches(const char *scratch, const char *file, const char *record)
{
    char path[F_PATH], actual[65];
    char *expected = review_scalar(scratch, record);
    bool ok = expected && (task_hex(expected, 40) || task_hex(expected, 64)) &&
              !f_path(path, sizeof(path), scratch, file) && git_oid_file(path, strlen(expected), actual) &&
              !strcmp(expected, actual);
    free(expected);
    return ok;
}

static bool scalar_matches(const char *scratch, const char *name, const char *expected)
{
    char *actual = review_scalar(scratch, name);
    bool ok = expected && actual && !strcmp(actual, expected);
    free(actual);
    return ok;
}

/* This internal route invokes only Hydra's existing strict YAML parser. It
 * never resolves the original definition path, validates trust, or runs steps. */
static char *parse_recipe(const char *path, const char *mode)
{
    char *argv[] = {(char *)f_hydra, "workflow", "--review-parse", (char *)path, (char *)mode, NULL};
    struct f_capture capture = {0};
    char *text = NULL;
    if (!f_run(argv, NULL, 0, 30, &capture) && !capture.status) {
        text = capture.out;
        capture.out = NULL;
    }
    f_capture_free(&capture);
    return text;
}

static bool resources_match(const char *scratch, const char *graph)
{
    char *copy = strdup(graph), *save = NULL, *field;
    bool ok = false;
    static const char *const names[] = {"parallelism", "disk-mb", "max-heads"};
    size_t i;
    if (!copy)
        return false;
    copy[strcspn(copy, "\n")] = 0;
    field = strtok_r(copy, "\t", &save);
    if (!field || strcmp(field, "workflow") || !strtok_r(NULL, "\t", &save))
        goto cleanup;
    for (i = 0; i < sizeof(names) / sizeof(names[0]); i++)
        if (!scalar_matches(scratch, names[i], strtok_r(NULL, "\t", &save)))
            goto cleanup;
    ok = strtok_r(NULL, "\t", &save) == NULL;
cleanup:
    free(copy);
    return ok;
}

static bool recipe_matches(const char *scratch, const char *recipe)
{
    char path[F_PATH];
    char *graph = NULL, *actual = NULL;
    bool ok = false;
    graph = parse_recipe(recipe, "runtime");
    if (f_path(path, sizeof(path), scratch, "graph.tsv") || !(actual = f_read(path, F_LIMIT)))
        goto cleanup;
    ok = graph && !strcmp(graph, actual) && resources_match(scratch, graph);
cleanup:
    free(actual);
    free(graph);
    return ok;
}

static bool compiled_matches(const char *scratch, json_object *data)
{
    json_object *compiled = plan_run_definition(scratch);
    char path[F_PATH];
    char *normalized = NULL, *retained = NULL;
    const char *recipe;
    bool ok = false;
    if (!compiled || !json_object_equal(f_field(compiled, "data"), data) ||
        !scalar_matches(scratch, "base-commit", f_string(f_field(compiled, "source"), "commit")))
        goto cleanup;
    recipe = f_string(compiled, "workflow");
    if (!recipe || f_path(path, sizeof(path), scratch, "compiled.yml") ||
        f_write(path, recipe, strlen(recipe), false) || !recipe_matches(scratch, path))
        goto cleanup;
    normalized = parse_recipe(path, "normalized");
    if (f_path(path, sizeof(path), scratch, "resolved.yml"))
        goto cleanup;
    retained = f_read(path, F_LIMIT);
    ok = normalized && retained && !strcmp(normalized, retained);
cleanup:
    free(retained);
    free(normalized);
    json_object_put(compiled);
    return ok;
}

static const char *validate_data(const char *scratch, const char *run, const char *recipe, json_object **data)
{
    char manifest[F_PATH], graph[F_PATH];
    char *ref = parse_recipe(recipe, "data");
    if (!ref)
        return "recipe_unavailable";
    bool has_data = *ref != 0;
    free(ref);
    if (has_data != exists(scratch, "data.json"))
        return "data_presence_mismatch";
    if (!has_data)
        return NULL;
    if (!blob_matches(scratch, "data.json", "data-hash"))
        return "data_hash_mismatch";
    if (f_path(manifest, sizeof(manifest), scratch, "data.json") ||
        f_path(graph, sizeof(graph), scratch, "graph.tsv"))
        return "path_bounds";
    *data = wd_manifest(manifest, graph);
    if (!*data)
        return "malformed_declarations";
    return wd_verify(*data, run) ? "retained_evidence_invalid" : NULL;
}

static const char *validate_snapshot(const char *scratch, const char *run, const char *project, bool compiled,
                                     json_object **data)
{
    char recipe[F_PATH];
    const char *run_id = strrchr(run, '/');
    if (!scalar_matches(scratch, "schema-version", "1") || !scalar_matches(scratch, "project-id", project) ||
        !scalar_matches(scratch, "run-id", run_id ? run_id + 1 : run))
        return "run_identity_mismatch";
    char *base = review_scalar(scratch, "base-commit");
    bool base_ok = task_hex(base, 40) || task_hex(base, 64);
    free(base);
    if (!base_ok)
        return "invalid_recorded_base";
    if (!blob_matches(scratch, "resolved.yml", "definition-hash"))
        return "definition_hash_mismatch";
    if (f_path(recipe, sizeof(recipe), scratch, "resolved.yml") || !recipe_matches(scratch, recipe))
        return "graph_or_resources_mismatch";
    const char *reason = validate_data(scratch, run, recipe, data);
    if (reason)
        return reason;
    if (compiled && !compiled_matches(scratch, *data))
        return "compiled_binding_mismatch";
    return NULL;
}

json_object *review_retained(const char *run, const char *project, json_object **data)
{
    char scratch[] = "/tmp/hydra-review-contract.XXXXXX";
    const char *reason = "snapshot_unavailable";
    json_object *out = json_object_new_object();
    bool compiled = compiled_run(run);
    *data = NULL;
    json_object_object_add(out, "compiled", json_object_new_boolean(compiled));
    f_string_add(out, "source_base",
                 compiled ? "bound to accepted plan; source contents not independently retained"
                          : "recorded provenance only; source contents not independently retained");
    if (!review_git_environment_clean())
        reason = "git_environment_redirect";
    else if (mkdtemp(scratch)) {
        if (snapshot(run, scratch, compiled))
            reason = validate_snapshot(scratch, run, project, compiled, data);
        if (f_remove_tree(scratch))
            reason = "snapshot_cleanup_failed";
    }
    f_string_add(out, "state", reason ? "failed" : "passed");
    if (reason)
        f_string_add(out, "reason", reason);
    return out;
}

static bool receipt_file_valid(json_object *file)
{
    return task_hex(f_string(file, "sha256"), 64) && f_string(file, "type") &&
           json_object_is_type(f_field(file, "bytes"), json_type_int) &&
           json_object_get_int64(f_field(file, "bytes")) >= 0;
}

static const char *artifact_state(const char *directory, const char *name, json_object *file,
                                  json_object *declaration, bool expired)
{
    char path[F_PATH], scratch[] = "/tmp/hydra-review-file.XXXXXX", copy[F_PATH];
    struct stat st;
    json_object *actual = NULL;
    const char *state = "unverified";
    if (f_path(path, sizeof(path), directory, name))
        return "invalid_name";
    if (lstat(path, &st))
        return expired ? "expired" : "missing";
    if (S_ISLNK(st.st_mode))
        return "symlink";
    if (!S_ISREG(st.st_mode))
        return "not_regular";
    if (expired)
        return "expired";
    if (!receipt_file_valid(file))
        return "malformed_receipt";
    if (!mkdtemp(scratch))
        return state;
    if (!f_path(copy, sizeof(copy), scratch, "artifact") && !task_file_copy(directory, name, copy)) {
        actual = wd_file(copy, declaration);
        state = actual && json_object_equal(actual, file) ? "available" : "tampered";
    }
    json_object_put(actual);
    if (f_remove_tree(scratch))
        state = "unverified";
    return state;
}

static bool inventory_extras(json_object *rows, json_object *files, json_object *decls)
{
    bool ready = true;
    json_object_object_foreach(files, name, file)
    {
        if (!f_field(decls, name)) {
            json_object *row = json_object_new_object();
            (void)file;
            f_string_add(row, "name", name);
            f_string_add(row, "state", "unexpected");
            json_object_array_add(rows, row);
            ready = false;
        }
    }
    return ready;
}

static bool inventory_rows(json_object *out, json_object *files, json_object *decls, const char *attempt,
                           bool expired)
{
    json_object *rows = f_field(out, "artifacts"), *subjects = f_field(out, "subjects");
    char directory[F_PATH];
    bool ready = true;
    if (f_path(directory, sizeof(directory), attempt, "artifacts"))
        return false;
    json_object_object_foreach(decls, name, declaration)
    {
        json_object *file = f_field(files, name), *row = json_object_new_object();
        const char *state = !wd_name(name) ? "invalid_name"
                            : !file        ? "missing_receipt"
                                           : artifact_state(directory, name, file, declaration, expired);
        f_string_add(row, "name", name);
        f_string_add(row, "state", state);
        if (file) {
            json_object_object_add(subjects, name, json_object_get(file));
            json_object_object_add(row, "receipt", json_object_get(file));
        }
        json_object_array_add(rows, row);
        if (strcmp(state, "available"))
            ready = false;
    }
    return inventory_extras(rows, files, decls) && ready;
}

void review_inventory(json_object *out, json_object *selected, json_object *data, const char *attempt,
                      bool expired)
{
    json_object *receipt = NULL, *files, *decls;
    char path[F_PATH], scratch[] = "/tmp/hydra-review-receipt.XXXXXX";
    const char *reason = NULL;
    json_object_object_add(out, "artifacts", json_object_new_array());
    json_object_object_add(out, "subjects", json_object_new_object());
    if (f_string(selected, "request_id") && !f_field(selected, "output_receipt")) {
        f_string_add(out, "inventory_state", "not_applicable_to_request");
        return;
    }
    if (!mkdtemp(scratch)) {
        f_string_add(out, "inventory_state", "snapshot_unavailable");
        f_string_add(out, "readiness", "revoked");
        return;
    }
    if (!f_path(path, sizeof(path), scratch, "outputs.json") && copy_record(attempt, "outputs.json", scratch))
        receipt = plan_read(path);
    files = f_field(receipt, "files");
    decls = f_field(f_field(f_field(data, "steps"), f_string(selected, "step_id")), "outputs");
    if (!json_object_is_type(decls, json_type_object) || json_object_object_length(decls) > (int)WD_NAMES)
        reason = "malformed_declarations";
    else if (!f_number_is(receipt, "schema_version", 1) || !json_object_is_type(files, json_type_object) ||
             json_object_object_length(files) > (int)WD_NAMES)
        reason = "malformed_receipt";
    else if (!inventory_rows(out, files, decls, attempt, expired))
        reason = "invalid_artifacts";
    if (f_remove_tree(scratch))
        reason = "snapshot_cleanup_failed";
    if (reason) {
        f_string_add(out, "inventory_state", reason);
        f_string_add(out, "readiness", "revoked");
    }
    json_object_put(receipt);
}
