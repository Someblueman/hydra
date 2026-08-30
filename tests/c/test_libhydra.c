#include "libhydra.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int failures = 0;

static void check(int condition, const char *message) {
    if (condition) {
        printf("[PASS] %s\n", message);
    } else {
        printf("[FAIL] %s\n", message);
        failures++;
    }
}

int main(void) {
    FILE *json = tmpfile();
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
