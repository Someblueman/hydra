#include "hydra_statistics.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

static void read_all(FILE *f, char *buf, size_t cap) {
    size_t n;
    assert(fseek(f, 0, SEEK_SET) == 0);
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

static void statistics_export_cases(void) {
    struct hs_model model;
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

}

static void statistics_compare_cases(void) {
    struct hs_model model, other;
    struct hs_filter filter = {.attention = true};
    char json[32768];
    FILE *out;
    assert(load_fixture(&model));
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

}

/* Schema 3 export: cohorts and telemetry over a schema 5 feed. Numbers appear
 * only for fully reported measurements; the rest is null with its state. */
static void statistics_remote_cases(void) {
    struct hs_model model;
    struct hs_filter filter = {0};
    static char json[65536];
    FILE *f = fopen("tests/fixtures/tui/statistics-v5.tsv", "r"), *out;
    assert(f && hs_load(f, &model)); fclose(f);
    out = tmpfile(); assert(out);
    assert(hs_write_metrics_json(out, &model, &filter));
    read_all(out, json, sizeof(json)); fclose(out);
    assert(strstr(json, "\"schema_version\":3"));
    assert(strstr(json, "\"location\":\"\""));
    assert(strstr(json, "\"remote\":{\"state\":\"partial\",\"remote_steps\":4,\"transfer_bytes\":null,\"provider_cost\":null,\"cpu_seconds\":null,\"memory_bytes\":null,\"tokens\":null}"));
    assert(strstr(json, "\"cohorts\":{\"location\":{\"truncated\":false,\"cohorts\":[{\"key\":\"local\",\"runs\":1,\"steps\":3"));
    assert(strstr(json, "{\"key\":\"alpha\",\"runs\":1,\"steps\":2,\"succeeded\":2,\"failed\":0,\"duration\":{\"state\":\"known\",\"eligible\":2,\"known\":2,\"sum\":230}"));
    assert(strstr(json, "\"wall_seconds\":{\"state\":\"known\",\"eligible\":2,\"known\":2,\"sum\":227}"));
    assert(strstr(json, "\"peak_rss_bytes\":{\"state\":\"partial\",\"eligible\":2,\"known\":1,\"max\":null}"));
    assert(strstr(json, "{\"key\":\"beta\",\"runs\":2,\"steps\":2") && strstr(json, "\"unmeasured_steps\":2,\"unmeasured_reason\":\"receiver recorded no telemetry block"));
    assert(strstr(json, "{\"key\":\"generation unknown\"") && strstr(json, "\"unmeasured_reason\":\"remote receipt could not be verified"));
    assert(strstr(json, "{\"key\":\"claude\",\"runs\":1,\"steps\":1") && strstr(json, "\"cost_microusd\":{\"state\":\"known\",\"eligible\":1,\"known\":1,\"sum\":250000}"));
    assert(strstr(json, "\"telemetry\":{\"contract\":\"fleet task telemetry 1\",\"measured_by\":\"task_owner\",\"scope\":\"execution_phase\",\"local_steps\":3,\"remote_steps\":4,\"telemetry_steps\":2"));
    assert(strstr(json, "\"transport_bytes\":{\"state\":\"partial\",\"eligible\":4,\"known\":3,\"sum\":null}"));
    assert(strstr(json, "\"remote_steps_without_telemetry\":2"));
    /* The remote cohort's peak RSS is partly reported: its max is null, never 0. */
    assert(!strstr(json, "\"max\":0}"));
    memcpy(filter.location, "alpha", 6);
    out = tmpfile(); assert(out);
    assert(hs_write_metrics_json(out, &model, &filter));
    read_all(out, json, sizeof(json)); fclose(out);
    assert(strstr(json, "\"location\":\"alpha\"") && strstr(json, "\"cohort\":{\"runs\":1,\"steps\":3}"));
    assert(strstr(json, "\"remote\":{\"state\":\"partial\",\"remote_steps\":3,\"transfer_bytes\":51592,"));
    memcpy(filter.location, "local", 6);
    out = tmpfile(); assert(out);
    assert(hs_write_metrics_json(out, &model, &filter));
    read_all(out, json, sizeof(json)); fclose(out);
    assert(strstr(json, "\"remote\":{\"state\":\"unavailable\",\"remote_steps\":0,\"transfer_bytes\":null"));
    assert(strstr(json, "\"telemetry_steps\":0") && strstr(json, "\"wall_seconds\":{\"state\":\"unavailable\",\"eligible\":0,\"known\":0,\"sum\":null}"));
    /* A schema 2 feed keeps export schema 1 and its unavailable remote block. */
    assert(load_fixture(&model));
    out = tmpfile(); assert(out);
    assert(hs_write_metrics_json(out, &model, &filter));
    read_all(out, json, sizeof(json)); fclose(out);
    assert(strstr(json, "\"schema_version\":1") && !strstr(json, "\"cohorts\"") && strstr(json, "\"remote\":{\"state\":\"unavailable\",\"transfer_bytes\":null"));
}

int main(void) {
    statistics_export_cases();
    statistics_compare_cases();
    statistics_remote_cases();
    puts("Statistics exporter: bounded JSON metrics, comparison, remote cohorts and fail-closed input passed");
    return 0;
}
