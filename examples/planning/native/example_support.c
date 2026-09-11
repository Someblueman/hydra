#include "example.h"
#include <fcntl.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

char *ex_read(const char *path, size_t limit, size_t *size) {
    int fd = -1;
    FILE *file = NULL;
    char *bytes = NULL;
    struct stat st;
    size_t n;
    if (!path || limit > F_LIMIT || (fd = open(path, O_RDONLY | O_NONBLOCK)) < 0 ||
        fstat(fd, &st) || !S_ISREG(st.st_mode) || st.st_size < 0 || (uint64_t)st.st_size > limit)
        goto cleanup;
    bytes = malloc(limit + 1);
    if (!bytes) goto cleanup;
    if (!(file = fdopen(fd, "rb"))) { free(bytes); bytes = NULL; goto cleanup; }
    fd = -1;
    n = fread(bytes, 1, limit + 1, file);
    if (ferror(file) || n > limit) { free(bytes); bytes = NULL; goto cleanup; }
    bytes[n] = '\0';
    if (size) *size = n;
cleanup:
    if (file) fclose(file);
    if (fd >= 0) close(fd);
    return bytes;
}

json_object *ex_json(const char *path, size_t limit) {
    size_t n = 0;
    char *bytes = ex_read(path, limit, &n);
    json_object *value = NULL;
    if (bytes && !memchr(bytes, '\0', n) && plan_json_unique(bytes)) value = f_parse_value(bytes);
    free(bytes);
    return value;
}
int ex_path(char path[F_PATH], const char *environment, const char *name) {
    const char *root = getenv(environment);
    return !root || !*root ? -1 : f_path(path, F_PATH, root, name);
}
json_object *ex_input(const char *name) {
    char path[F_PATH];
    return ex_path(path, "HYDRA_WORKFLOW_INPUTS_DIR", name) ? NULL : ex_json(path, F_LIMIT);
}
int ex_hash(const void *bytes, size_t size, char digest[65]) {
    char *argv[] = {"shasum", "-a", "256", NULL};
    struct f_capture cap = {0};
    int status = -1;
    if (f_run(argv, bytes, size, 30, &cap)) goto cleanup;
    if (cap.status == 127) {
        char *gnu[] = {"sha256sum", NULL};
        f_capture_free(&cap);
        if (f_run(gnu, bytes, size, 30, &cap)) goto cleanup;
    }
    if (!cap.status && cap.input_complete && cap.out && strlen(cap.out) >= 64 &&
        strspn(cap.out, "0123456789abcdef") == 64) {
        memcpy(digest, cap.out, 64); digest[64] = '\0'; status = 0;
    }
cleanup:
    f_capture_free(&cap);
    return status;
}
int ex_digest(json_object *value, bool slash_escape, char digest[65]) {
    json_object *ordered = plan_canonical(value);
    const char *text = json_object_to_json_string_ext(ordered,
        JSON_C_TO_STRING_PLAIN | (slash_escape ? 0 : JSON_C_TO_STRING_NOSLASHESCAPE));
    int status = text ? ex_hash(text, strlen(text), digest) : -1;
    json_object_put(ordered);
    return status;
}
int ex_write(const char *name, json_object *value) {
    char path[F_PATH];
    const char *text = json_object_to_json_string_ext(value, JSON_C_TO_STRING_PLAIN | JSON_C_TO_STRING_NOSLASHESCAPE);
    if (!text || ex_path(path, "HYDRA_WORKFLOW_OUTPUTS_DIR", name)) return -1;
    return f_write(path, text, strlen(text), true);
}
bool ex_equal(json_object *a, json_object *b) {
    /* json_object_equal equates 1 and 1.0 on some JSON-C releases. The original
     * byte-canonical acceptance distinguished these types, including bool/int. */
    json_object *ca = plan_canonical(a), *cb = plan_canonical(b);
    const char *sa = json_object_to_json_string_ext(ca, JSON_C_TO_STRING_PLAIN);
    const char *sb = json_object_to_json_string_ext(cb, JSON_C_TO_STRING_PLAIN);
    bool same = sa && sb && !strcmp(sa, sb);
    json_object_put(ca); json_object_put(cb);
    return same;
}
bool ex_text(json_object *object, const char *key, const char *text) {
    const char *value = f_string(object, key);
    return value && text && !strcmp(value, text);
}
json_object *ex_strings(const char *const *values, size_t count) {
    json_object *array = json_object_new_array();
    size_t i;
    for (i = 0; i < count; i++) json_object_array_add(array, json_object_new_string(values[i]));
    return array;
}
int ex_observe(json_object *observations, const char *id, json_object *raw, bool slash_escape) {
    char hash[65];
    json_object *observation;
    if (ex_digest(raw, slash_escape, hash)) return -1;
    observation = json_object_new_object();
    f_string_add(observation, "id", id);
    json_object_object_add(observation, "raw", json_object_get(raw));
    f_string_add(observation, "raw_sha256", hash);
    return json_object_array_add(observations, observation);
}
int ex_evidence(const char *output, const char *validator_key, const char *recipe_key,
                const char *identity, json_object *argv, json_object *requirements,
                json_object *obligations, json_object *observations, size_t failed,
                bool valid, bool slash_escape, json_object *limitations, const char *evidence) {
    json_object *binding = ex_json(getenv("HYDRA_WORKFLOW_VALIDATION_FILE"), F_LIMIT);
    json_object *data = f_field(binding, "data"), *result = NULL, *records = NULL, *cases = NULL;
    char path[F_PATH], subject_hash[65], raw_hash[65];
    const char *validator = f_string(data, validator_key), *recipe = f_string(data, recipe_key);
    size_t i, count = json_object_array_length(observations);
    bool passed = valid && !failed;
    const char *verdict = passed ? "pass" : "fail";
    int status = 2;
    if (!validator || !recipe || ex_path(path, "HYDRA_WORKFLOW_INPUTS_DIR", "subject") ||
        f_hash(path, subject_hash) || ex_digest(observations, slash_escape, raw_hash)) goto cleanup;
    cases = json_object_new_array(); records = json_object_new_array(); result = json_object_new_object();
    for (i = 0; i < count; i++)
        json_object_array_add(cases, json_object_get(f_field(json_object_array_get_idx(observations, i), "id")));
    for (i = 0; i < json_object_array_length(obligations); i++) {
        json_object *record = json_object_new_object();
        json_object *invocation = json_object_new_object();
        json_object *counts = json_object_new_object();
        f_string_add(record, "obligation_id", f_text(json_object_array_get_idx(obligations, i)));
        f_string_add(record, "subject_manifest_sha256", subject_hash);
        f_string_add(record, "validator_identity", identity);
        f_string_add(record, "validator_recipe_sha256", recipe);
        json_object_object_add(invocation, "argv", json_object_get(argv));
        json_object_object_add(invocation, "exit_code", json_object_new_int(passed ? 0 : 1));
        json_object_object_add(record, "invocation", invocation);
        json_object_object_add(record, "environment", f_parse("{\"host\":\"local\",\"toolchain\":\"C99/JSON-C\"}"));
        json_object_object_add(record, "case_inventory", json_object_get(cases));
        json_object_object_add(record, "observations", json_object_get(observations));
        f_string_add(record, "raw_evidence_sha256", raw_hash);
        json_object_object_add(counts, "executed", json_object_new_int64((int64_t)count));
        json_object_object_add(counts, "failed", json_object_new_int64((int64_t)failed));
        json_object_object_add(counts, "skipped", json_object_new_int(0));
        json_object_object_add(record, "counts", counts);
        json_object_object_add(record, "limitations", json_object_get(limitations));
        json_object_array_add(records, record);
    }
    json_object_object_add(result, "schema_version", json_object_new_int(3));
    f_string_add(result, "execution_status", "completed");
    f_string_add(result, "evidence_status", valid ? "valid" : "invalid");
    f_string_add(result, "domain_verdict", verdict); f_string_add(result, "verdict", verdict);
    f_string_add(result, "subject_sha256", subject_hash); f_string_add(result, "validator_sha256", validator);
    json_object_object_add(result, "requirements", json_object_get(requirements));
    json_object_object_add(result, "limitations", json_object_get(limitations));
    json_object_object_add(result, "evidence_records", json_object_get(records));
    f_string_add(result, "evidence", evidence);
    status = ex_write(output, result) ? 2 : (passed ? 0 : 1);
cleanup:
    json_object_put(cases); json_object_put(records); json_object_put(result); json_object_put(binding);
    return status;
}
