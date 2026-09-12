#define _POSIX_C_SOURCE 200809L
#include "fleet/review_projection.h"
#include "fleet/support/json.h"
#include "fleet/support/files.h"
#include "fleet/task/task.h"
#include <assert.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

const char *f_home = "/tmp", *f_hydra = "hydra";
static unsigned checks;
static json_object *copy(json_object *object)
{
    return f_parse(json_object_to_json_string_ext(object, JSON_C_TO_STRING_PLAIN));
}
/* Caller closes the captured public projection. */
static FILE *capture_projection(json_object *envelope, json_object *selection, bool valid)
{
    FILE *capture = tmpfile(); int saved = dup(STDOUT_FILENO);
    assert(capture && saved >= 0 && !fflush(stdout));
    assert(dup2(fileno(capture), STDOUT_FILENO) >= 0);
    assert((review_projection(envelope, selection) == 0) == valid);
    assert(!fflush(stdout) && !fseek(capture, 0L, SEEK_SET));
    assert(dup2(saved, STDOUT_FILENO) >= 0); close(saved);
    return capture;
}
static void probe(json_object *envelope, json_object *selection, bool valid)
{
    FILE *capture = capture_projection(envelope, selection, valid); char line[8192]; size_t bytes = 0U;
    while (fgets(line, sizeof(line), capture)) {
        size_t n = strlen(line); bytes += n;
        assert(n && line[n - 1U] == '\n' && !strchr(line, '\033'));
    }
    assert(!ferror(capture) && bytes <= 1024U * 1024U && (valid ? bytes > 0U : bytes == 0U));
    fclose(capture);
    json_object_put(envelope); checks++;
}
static void ordered_prefix(FILE *capture, const char *const *expected, size_t count)
{
    char line[8192];
    assert(fgets(line, sizeof(line), capture) && !strncmp(line, "HYDRA_REVIEW\t1\t", 15U));
    for (size_t i = 0U; i < count; i++) {
        assert(fgets(line, sizeof(line), capture)); assert(!strcmp(line, expected[i]));
    }
    assert(!fseek(capture, 0L, SEEK_SET));
}
static void ordered_probe(json_object *envelope, json_object *selection, const char *const *expected, size_t count)
{
    json_object *before = copy(envelope); FILE *capture = capture_projection(envelope, selection, true);
    char line[8192]; size_t occurrences[5] = {0}; bool identity = false;
    assert(count <= 5U);
    ordered_prefix(capture, expected, count);
    while (fgets(line, sizeof(line), capture)) {
        if (strncmp(line, "TEXT\t", 5U)) continue;
        for (size_t i = 0U; i < count; i++) occurrences[i] += !strcmp(line, expected[i]);
        identity |= !strncmp(line, "TEXT\tidentity: ", 15U);
    }
    assert(!ferror(capture)); assert(identity); assert(json_object_equal(before, envelope));
    for (size_t i = 0U; i < count; i++) assert(occurrences[i] == 1U);
    fclose(capture); json_object_put(before); json_object_put(envelope); checks++;
}
static void status_order_cases(json_object *original, json_object *selection)
{
    const char *ready[] = {"TEXT\treadiness: ready\n", "TEXT\taccepted: false\n", "TEXT\tchecks: state: passed\n"};
    const char *approval[] = {"TEXT\treadiness: pending\n", "TEXT\taccepted: false\n", "TEXT\trequest: state: pending\n", "TEXT\trequest: message: Exact selected request\n"};
    const char *failed[] = {"TEXT\treadiness: revoked\n", "TEXT\taccepted: false\n", "TEXT\tchecks: state: failed\n"};
    json_object *envelope, *data, *request;
    ordered_probe(copy(original), selection, ready, 3U);
    envelope = copy(original); data = f_field(envelope, "data"); request = json_object_new_object();
    f_string_add(data, "readiness", "pending"); json_object_object_del(data, "checks");
    f_string_add(request, "request_id", "long-original-identity"); f_string_add(request, "state", "pending");
    f_string_add(request, "message", "Exact selected request"); json_object_object_add(data, "request", request);
    ordered_probe(envelope, selection, approval, 4U);
    envelope = copy(original); data = f_field(envelope, "data");
    f_string_add(data, "readiness", "revoked"); f_string_add(f_field(data, "checks"), "state", "failed");
    ordered_probe(envelope, selection, failed, 3U);
}
static json_object *reference(const char *locator, const char *preview)
{
    json_object *row = json_object_new_object(), *rows = json_object_new_array();
    f_string_add(row, "kind", "log"); f_string_add(row, "state", "available"); f_string_add(row, "locator", locator);
    if (preview) f_string_add(row, "preview", preview);
    json_object_array_add(rows, row); return rows;
}
static int rehash(const char *path)
{
    json_object *envelope = f_read_json(path, TASK_PACKAGE_LIMIT + 1024U);
    char scratch[] = "/tmp/hydra-review-hash.XXXXXX", hash[65];
    assert(envelope && mkdtemp(scratch));
    assert(!task_json_hash(f_field(envelope, "result"), scratch, hash));
    f_string_add(envelope, "result_sha256", hash);
    assert(puts(json_object_to_json_string_ext(envelope, JSON_C_TO_STRING_PLAIN)) != EOF);
    json_object_put(envelope); assert(!f_remove_tree(scratch)); return 0;
}
int main(int argc, char **argv)
{
    static const char *const keys[] = {"kind", "project_id", "host", "task_id", "run_id", "step_id", "attempt_id", "head_id", "current_instance", "request_id", "binding", "revision_sha256", "identity_sha256"};
    json_object *original, *selection, *envelope, *parsed; char *args[13]; size_t i;
    char *long_text, locator[4097], preview[4098];
    if (argc == 3 && !strcmp(argv[1], "rehash")) return rehash(argv[2]);
    long_text = malloc(2U * 1024U * 1024U + 1U);
    assert(argc == 2 && long_text);
    original = f_read_json(argv[1], 1024U * 1024U); assert(original);
    selection = f_field(f_field(original, "data"), "selection"); assert(selection);
    for (i = 0; i < 13U; i++) args[i] = (char *)f_string(selection, keys[i]);
    parsed = review_selection_parse("fleet overview", 13, args); assert(parsed); json_object_put(parsed); checks++;
    args[12] = "0000000000000000000000000000000000000000000000000000000000000000";
    assert(!review_selection_parse("fleet overview", 13, args)); checks++;
    probe(copy(original), selection, true);
    status_order_cases(original, selection);
    probe(NULL, selection, false);
    envelope = copy(original); json_object_object_add(envelope, "ok", json_object_new_string("true")); probe(envelope, selection, false);
    envelope = copy(original); json_object_object_add(envelope, "schema_version", json_object_new_int(2)); probe(envelope, selection, false);
    envelope = copy(original); json_object_object_add(envelope, "data", json_object_new_array()); probe(envelope, selection, false);
    envelope = copy(original); json_object_object_add(f_field(envelope, "data"), "accepted", json_object_new_boolean(true)); probe(envelope, selection, false);
    envelope = copy(original); f_string_add(f_field(envelope, "data"), "controls", "safe\033[31m\nnext\tfield"); probe(envelope, selection, true);
    memset(preview, 'x', sizeof(preview)); preview[4096] = '\0';
    envelope = copy(original); json_object_object_add(f_field(envelope, "data"), "references", reference("/explicit.log", preview)); probe(envelope, selection, true);
    preview[4096] = 'x'; preview[4097] = '\0';
    envelope = copy(original); json_object_object_add(f_field(envelope, "data"), "references", reference("/explicit.log", preview)); probe(envelope, selection, false);
    memset(locator, 'x', sizeof(locator)); locator[0] = '/'; locator[4095] = '\0';
    envelope = copy(original); json_object_object_add(f_field(envelope, "data"), "references", reference(locator, NULL)); probe(envelope, selection, true);
    locator[4095] = 'x'; locator[4096] = '\0';
    envelope = copy(original); json_object_object_add(f_field(envelope, "data"), "references", reference(locator, NULL)); probe(envelope, selection, false);
    envelope = copy(original); json_object_object_add(f_field(envelope, "data"), "embedded_nul", json_object_new_string_len("a\0b", 3)); probe(envelope, selection, false);
    memset(long_text, 'x', 2U * 1024U * 1024U); long_text[2U * 1024U * 1024U] = '\0';
    envelope = copy(original); f_string_add(f_field(envelope, "data"), "large", long_text); probe(envelope, selection, false);
    long_text[16384] = '\0';
    envelope = copy(original); f_string_add(f_field(envelope, "data"), "long_line", long_text); probe(envelope, selection, true);
    free(long_text); json_object_put(original); printf("Review projection: %u parser, envelope, bounds and atomic-output checks passed\n", checks);
    return 0;
}
