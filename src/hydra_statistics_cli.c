#include "hydra_statistics.h"
#include <errno.h>
#include <stdlib.h>
#include <string.h>

static int load_path(const char *path, struct hs_model **model, FILE *err) {
    FILE *input;
    struct hs_model *value;
    if (!strcmp(path, "-")) input = stdin;
    else input = fopen(path, "r");
    if (!input) {
        fprintf(err, "statistics_invalid_feed: %s: %s\n", path, strerror(errno));
        return 1;
    }
    value = calloc(1, sizeof(*value));
    if (!value || !hs_load(input, value)) {
        if (input != stdin) fclose(input);
        free(value);
        fprintf(err, "statistics_invalid_feed: malformed, truncated, or oversized feed\n");
        return 1;
    }
    if (input != stdin) fclose(input);
    *model = value;
    return 0;
}

int hs_statistics_cli_json(const char *path, FILE *out, FILE *err) {
    struct hs_model *model = NULL;
    struct hs_filter filter = {0};
    int result = load_path(path, &model, err);
    if (result) return result;
    result = hs_write_metrics_json(out, model, &filter) && fputc('\n', out) != EOF ? 0 : 1;
    free(model);
    return result;
}

int hs_statistics_cli_compare(const char *left_path, const char *right_path,
                              FILE *out, FILE *err) {
    struct hs_model *left = NULL, *right = NULL;
    struct hs_filter filter = {0};
    int result = load_path(left_path, &left, err);
    if (result) return result;
    result = load_path(right_path, &right, err);
    if (result) { free(left); return result; }
    result = hs_write_metrics_compare_json(out, left, right, &filter) && fputc('\n', out) != EOF ? 0 : 1;
    free(left); free(right);
    return result;
}
