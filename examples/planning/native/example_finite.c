#include "example.h"
#include <ctype.h>
#include <stdlib.h>
#include <string.h>

static bool manifest_valid(json_object *manifest) {
    json_object *items = f_field(manifest, "items"), *version = f_field(manifest, "schema_version");
    if (!json_object_is_type(manifest, json_type_object) ||
        json_object_object_length(manifest) != 2 || !json_object_is_type(version, json_type_int) ||
        json_object_get_int64(version) != 1 || !json_object_is_type(items, json_type_array) ||
        json_object_array_length(items) > 8)
        return false;
    for (size_t i = 0; i < json_object_array_length(items); i++) {
        json_object *item = json_object_array_get_idx(items, i), *value = f_field(item, "value");
        const char *id = f_string(item, "id");
        if (!json_object_is_type(item, json_type_object) || json_object_object_length(item) != 3 ||
            !id || !*id || strlen(id) > 32 || id[0] < 'a' || id[0] > 'z' ||
            strspn(id, "abcdefghijklmnopqrstuvwxyz0123456789-") != strlen(id) ||
            !json_object_is_type(value, json_type_int) || json_object_get_int64(value) < -10 ||
            json_object_get_int64(value) > 10 ||
            !json_object_is_type(f_field(item, "enabled"), json_type_boolean))
            return false;
        for (size_t j = 0; j < i; j++)
            if (ex_text(json_object_array_get_idx(items, j), "id", id))
                return false;
    }
    return true;
}
static json_object *members(json_object *manifest, bool skips) {
    json_object *rows = json_object_new_array(), *items = f_field(manifest, "items");
    for (size_t i = 0; i < json_object_array_length(items); i++) {
        json_object *item = json_object_array_get_idx(items, i);
        bool enabled = json_object_get_boolean(f_field(item, "enabled"));
        if (!enabled && !skips)
            continue;
        json_object *row = json_object_new_object();
        int64_t n = json_object_get_int64(f_field(item, "value"));
        f_string_add(row, "id", f_string(item, "id"));
        json_object_object_add(row, "value", json_object_new_int64(n));
        if (enabled)
            json_object_object_add(row, "square", json_object_new_int64(n * n));
        else
            f_string_add(row, "status", "skipped");
        json_object_array_add(rows, row);
    }
    return rows;
}
static int evidence(const char *output, const char *identity, const char *command,
                    const char *requirement, const char *obligation, const char *case_id,
                    json_object *raw, bool passed, bool slash, const char *limit,
                    const char *description, bool stage1) {
    const char *args[] = {"./plan-example", command, "stage1"};
    json_object *argv = ex_strings(args, stage1 ? 3 : 2), *req = ex_strings(&requirement, 1);
    json_object *obl = ex_strings(&obligation, 1), *limits = ex_strings(&limit, 1),
                *obs = json_object_new_array();
    f_string_add(raw, "actual", passed ? "pass" : "fail");
    int status = ex_observe(obs, case_id, raw, slash)
                     ? 2
                     : ex_evidence(output, "check", "check-recipe", identity, argv, req, obl, obs,
                                   passed ? 0 : 1, true, slash, limits, description);
    json_object_put(argv);
    json_object_put(req);
    json_object_put(obl);
    json_object_put(limits);
    json_object_put(obs);
    return status;
}
int ex_manifest_check(void) {
    char path[F_PATH];
    if (ex_path(path, "HYDRA_WORKFLOW_INPUTS_DIR", "manifest"))
        return 2;
    json_object *manifest = ex_json(path, 4096), *report = ex_input("subject"), *expected = NULL,
                *raw = NULL;
    int status = 2;
    if (!manifest_valid(manifest))
        goto done;
    expected = json_object_new_object();
    json_object_object_add(expected, "schema_version", json_object_new_int(1));
    json_object_object_add(expected, "members", members(manifest, true));
    raw = json_object_new_object();
    json_object_object_add(raw, "expected", json_object_get(f_field(expected, "members")));
    json_object_object_add(raw, "actual_members", json_object_get(f_field(report, "members")));
    status = evidence(
        "check.json", "manifest-map-check-v1", "manifest-check", "membership", "membership-check",
        "membership-case", raw, ex_equal(report, expected), false,
        "Finite manifest fixture; checker establishes exact membership and arithmetic only.",
        "Reconstructed every expected enabled square and skipped member from the bound manifest.",
        false);
done:
    json_object_put(manifest);
    json_object_put(report);
    json_object_put(expected);
    json_object_put(raw);
    return status;
}
int ex_pattern_check(void) {
    const char expected[] = "A:validated input\nB:validated input\n";
    char path[F_PATH], expected_hash[65], actual_hash[65];
    size_t size;
    if (ex_path(path, "HYDRA_WORKFLOW_INPUTS_DIR", "subject"))
        return 2;
    char *bytes = ex_read(path, F_LIMIT, &size);
    if (!bytes || ex_hash(bytes, size, actual_hash) ||
        ex_hash(expected, sizeof expected - 1, expected_hash)) {
        free(bytes);
        return 2;
    }
    bool passed = size == sizeof expected - 1 && !memcmp(bytes, expected, size);
    json_object *raw = json_object_new_object();
    json_object_object_add(raw, "measurement", json_object_new_int64((int64_t)size));
    f_string_add(raw, "unit", "bytes");
    f_string_add(raw, "expected_sha256", expected_hash);
    f_string_add(raw, "actual_sha256", actual_hash);
    int status = evidence(
        "check", "pattern-check-v2", "pattern-check", "assembly", "assembly-check", "assembly-case",
        raw, passed, false, "Fixed deterministic two-line task; no semantic generalization.",
        "Compared all assembled bytes against independently fixed expected bytes.", false);
    free(bytes);
    json_object_put(raw);
    return status;
}
int ex_staged_produce(void) {
    json_object *manifest = ex_input("manifest"), *result = NULL, *rows = NULL, *selected = NULL;
    char path[F_PATH], source[65], hash[65];
    int status = 2;
    if (!manifest_valid(manifest) || ex_path(path, "HYDRA_WORKFLOW_INPUTS_DIR", "manifest") ||
        f_hash(path, source))
        goto done;
    /* Producer arithmetic is separate from the checker's reconstruction. */
    rows = json_object_new_array();
    selected = json_object_new_array();
    json_object *items = f_field(manifest, "items");
    for (size_t i = 0; i < json_object_array_length(items); i++) {
        json_object *item = json_object_array_get_idx(items, i);
        if (!json_object_get_boolean(f_field(item, "enabled")))
            continue;
        int n = json_object_get_int(f_field(item, "value"));
        json_object *row = json_object_new_object();
        f_string_add(row, "id", f_string(item, "id"));
        json_object_object_add(row, "value", json_object_new_int(n));
        json_object_object_add(row, "square", json_object_new_int(n * n));
        json_object_array_add(rows, row);
        json_object_array_add(selected, json_object_get(f_field(item, "id")));
    }
    if (ex_digest(rows, false, hash))
        goto done;
    result = json_object_new_object();
    json_object_object_add(result, "schema_version", json_object_new_int(3));
    f_string_add(result, "status", "pass");
    f_string_add(result, "source_path", "manifest.json");
    f_string_add(result, "source_sha256", source);
    f_string_add(result, "result_sha256", hash);
    json_object_object_add(result, "selected_ids", json_object_get(selected));
    json_object_object_add(result, "results", json_object_get(rows));
    const char *limit = "Finite local arithmetic selection; no runtime graph expansion.";
    json_object_object_add(result, "limitations", ex_strings(&limit, 1));
    status = ex_write("finding.json", result) ? 2 : 0;
done:
    json_object_put(manifest);
    json_object_put(result);
    json_object_put(rows);
    json_object_put(selected);
    return status;
}
/* JSON-C represents valid JSON null as a null pointer. Keep it distinct from
 * unreadable/malformed input so a valid null subject gets failed-domain evidence. */
static bool input_is_null(const char *name) {
    char path[F_PATH]; size_t size = 0;
    if (ex_path(path, "HYDRA_WORKFLOW_INPUTS_DIR", name)) return false;
    char *bytes = ex_read(path, F_LIMIT, &size);
    if (!bytes) return false;
    const char *start = bytes + strspn(bytes, " \t\r\n");
    bool is_null = !memchr(bytes, 0, size) && !strncmp(start, "null", 4) &&
                   strspn(start + 4, " \t\r\n") == strlen(start + 4);
    free(bytes); return is_null;
}
int ex_staged_check(bool first) {
    json_object *manifest = ex_input("manifest"), *subject = ex_input("subject"), *rows = NULL,
                *ids = NULL;
    json_object *finding = NULL, *joined = NULL, *raw = NULL;
    int status = 2;
    bool passed;
    char path[F_PATH], source[65], result[65];
    if (!manifest_valid(manifest) || (!subject && !input_is_null("subject")))
        goto done;
    rows = members(manifest, false);
    ids = json_object_new_array();
    raw = json_object_new_object();
    for (size_t i = 0; i < json_object_array_length(rows); i++)
        json_object_array_add(ids,
                              json_object_get(f_field(json_object_array_get_idx(rows, i), "id")));
    if (first) {
        if (ex_path(path, "HYDRA_WORKFLOW_INPUTS_DIR", "manifest") || f_hash(path, source) ||
            ex_digest(rows, false, result))
            goto done;
        json_object *version = f_field(subject, "schema_version"), *limitations;
        passed = json_object_is_type(subject, json_type_object) &&
                 json_object_object_length(subject) == 8 &&
                 json_object_object_get_ex(subject, "limitations", &limitations) &&
                 json_object_is_type(version, json_type_int) &&
                 json_object_get_int64(version) == 3 && ex_text(subject, "status", "pass") &&
                 ex_text(subject, "source_path", "manifest.json") &&
                 ex_text(subject, "source_sha256", source) &&
                 ex_text(subject, "result_sha256", result) &&
                 ex_equal(f_field(subject, "selected_ids"), ids) &&
                 ex_equal(f_field(subject, "results"), rows);
        json_object_object_add(raw, "selected_ids",
                               json_object_get(f_field(subject, "selected_ids")));
        json_object_object_add(raw, "expected", json_object_get(ids));
    } else {
        finding = ex_input("finding");
        if (!finding || !f_field(finding, "selected_ids"))
            goto done;
        joined = json_object_new_object();
        json_object_object_add(joined, "schema_version", json_object_new_int(1));
        json_object_object_add(joined, "selected_ids",
                               json_object_get(f_field(finding, "selected_ids")));
        json_object_object_add(joined, "members", json_object_get(rows));
        passed = ex_equal(subject, joined) && ex_equal(f_field(finding, "selected_ids"), ids);
        json_object_object_add(raw, "expected_members", json_object_get(rows));
        json_object_object_add(raw, "observed_members", json_object_get(subject));
    }
    status =
        evidence("check.json", first ? "staged-stage1-check-v1" : "staged-check-v1", "staged-check",
                 first ? "stage1" : "stage2", first ? "stage1-check" : "stage2-check",
                 first ? "stage1-finding" : "stage2-membership", raw, passed, first,
                 "Finite staged fixture.",
                 first ? "Validated stage-1 finding source binding and selected membership."
                       : "Verified exact frozen stage-1 membership and arithmetic.",
                 first);
done:
    json_object_put(manifest);
    json_object_put(subject);
    json_object_put(rows);
    json_object_put(ids);
    json_object_put(finding);
    json_object_put(joined);
    json_object_put(raw);
    return status;
}
int ex_feature_evidence(void) {
    json_object *raw = f_parse("{\"actual\":\"pass\",\"verdict\":\"pass\",\"measurement\":1,"
                               "\"requirements\":[\"normalization\",\"bounds\",\"cli\"]}");
    json_object *obs = json_object_new_array(), *argv = f_parse_value("[\"sh\",\"verify.sh\"]"),
                *req = f_parse_value("[\"normalization\",\"bounds\",\"cli\"]");
    json_object *obl = f_parse_value("[\"slug-check\",\"bounds-check\",\"cli-check\"]"),
                *limits = f_parse_value("[\"fixture-only\"]");
    int status = 2;
    for (size_t i = 0; i < 3; i++)
        if (ex_observe(obs, f_text(json_object_array_get_idx(obl, i)), raw, false))
            goto done;
    status = ex_evidence("verification.json", "slug-check", "slug-check-recipe", "feature-slug-v3",
                         argv, req, obl, obs, 0, true, false, limits,
                         "sealed archive independently built and tested");
done:
    json_object_put(raw);
    json_object_put(obs);
    json_object_put(argv);
    json_object_put(req);
    json_object_put(obl);
    json_object_put(limits);
    return status;
}
