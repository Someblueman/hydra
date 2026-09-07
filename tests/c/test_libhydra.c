#define _DEFAULT_SOURCE
#define _DARWIN_C_SOURCE
#include "libhydra.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

static int failures = 0;

static void check(int condition, const char *message) {
    if (condition) {
        printf("[PASS] %s\n", message);
    } else {
        printf("[FAIL] %s\n", message);
        failures++;
    }
}

static void snapshot_output_failure(void) {
    char root[] = "/tmp/hydra-snapshot-output.XXXXXX", schema[256], projects[256];
    FILE *record = NULL, *readonly = NULL;
    if (!mkdtemp(root)) { check(0, "snapshot failure fixture"); return; }
    snprintf(schema, sizeof(schema), "%s/schema-version", root);
    snprintf(projects, sizeof(projects), "%s/projects", root);
    record = fopen(schema, "w");
    if (record) { fputs("2\n", record); fclose(record); }
    if (!record || mkdir(projects, 0700)) { check(0, "snapshot failure fixture"); goto done; }
    readonly = fopen(schema, "r");
    check(readonly && hydra_write_snapshot(root, readonly, stderr) != 0,
          "snapshot reports output errors");
    if (readonly) fclose(readonly);
done:
    unlink(schema); rmdir(projects); rmdir(root);
}

int main(void) {
    FILE *json = tmpfile();
    snapshot_output_failure();
    char buffer[128];
    size_t bytes;
    check(hydra_valid_id("project_0123456789abcdef") == 1, "valid opaque ID");
    check(hydra_valid_id("project_not-hex") == 0, "invalid opaque ID");
    check(json != NULL, "temporary JSON stream");
    if (json != NULL) {
        check(hydra_json_write_string(json, "quote\" slash\\ tab\t") == 0, "JSON encoder succeeds");
        rewind(json);
        bytes = fread(buffer, 1U, sizeof(buffer) - 1U, json);
        buffer[bytes] = '\0';
        check(strcmp(buffer, "\"quote\\\" slash\\\\ tab\\t\"") == 0, "canonical JSON escaping");
        fclose(json);
    }
    return failures == 0 ? 0 : 1;
}
