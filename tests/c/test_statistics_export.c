#include "hydra_statistics.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

static void read_all(FILE *f, char *buf, size_t cap) {
    size_t n;
    rewind(f);
    n = fread(buf, 1, cap - 1, f);
    assert(!ferror(f));
    buf[n] = '\0';
}

static bool load_fixture(struct hs_model *m) {
    FILE *f = fopen("tests/fixtures/tui/statistics-v2.tsv", "r");
    bool ok;
    assert(f);
    ok = hs_load(f, m);
    fclose(f);
    return ok;
}

int main(void) {
    struct hs_model model, other;
    struct hs_filter filter = {0};
    char json[32768];
    FILE *out;

    assert(load_fixture(&model));
    out = tmpfile(); assert(out);
    assert(hs_write_metrics_json(out, &model, &filter));
    read_all(out, json, sizeof(json));
    assert(strstr(json, "\"schema_version\":1"));
    assert(strstr(json, "\"queue\":{\"state\":\"known\""));
    assert(strstr(json, "\"eligible\":10"));
    assert(strstr(json, "\"known\":7"));
    assert(strstr(json, "\"mean\":10"));
    assert(strstr(json, "\"p95\":10"));
    fclose(out);

    filter.attention = true;
    out = tmpfile(); assert(out);
    assert(hs_write_metrics_json(out, &model, &filter));
    read_all(out, json, sizeof(json));
    assert(strstr(json, "\"recoveries\":{\"state\":\"known\""));
    fclose(out);

    assert(load_fixture(&other));
    other.runs[0].started = 0;
    out = tmpfile(); assert(out);
    assert(hs_write_metrics_compare_json(out, &model, &other, &filter));
    read_all(out, json, sizeof(json));
    assert(strstr(json, "\"delta\":{\"metrics\""));
    assert(strstr(json, "\"elapsed\":{\"state\":\"known\""));
    fclose(out);

    /* A malformed feed never reaches the exporter: callers must fail closed. */
    out = tmpfile(); assert(out);
    assert(!hs_load(out, &other));
    fclose(out);
    puts("Statistics exporter: bounded JSON metrics, comparison and fail-closed input passed");
    return 0;
}
