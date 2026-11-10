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

static void load_text(struct hs_model *m, const char *text) {
    FILE *f = tmpfile();
    assert(f && fputs(text, f) >= 0);
    rewind(f);
    assert(hs_load(f, m));
    fclose(f);
}

static void statistics_session_unknowns(const struct hs_model *model);
static void statistics_session_compare(const struct hs_model *model);

/* Schema 5 feeds export head sessions; unknown sums are null, never zero. */
static void statistics_session_cases(void) {
    static struct hs_model model;
    struct hs_filter filter = {0};
    char json[32768];
    FILE *out;
    load_text(&model, "HYDRA_STATISTICS\t5\t1788789600\n"
        "H\thead_a\tinstance_a1\talpha\tcodex\tinteractive\tended\t1788786000\t1788787800\tdone\t600\t900\t300\t0\n"
        "H\thead_b\tinstance_b1\tbeta\tclaude\tinteractive\topen\t1788789000\t-\t-\t250\t150\t200\t300\n"
        "H\thead_d\tinstance_d1\tdelta\t-\tinteractive\tended\t-\t1788780000\t-\t-\t-\t-\t-\n"
        "Z\t0\t0\t3\n");
    out = tmpfile(); assert(out);
    assert(hs_write_metrics_json(out, &model, &filter));
    read_all(out, json, sizeof(json));
    assert(strstr(json, "{\"schema_version\":3,"));
    assert(strstr(json, "\"sessions\":{\"scope\":\"head_sessions\",\"count\":3,\"open\":1,\"undated\":1,"));
    assert(strstr(json, "\"length_seconds\":{\"state\":\"partial\",\"eligible\":3,\"known\":2,\"sum\":2400,\"mean\":1200,\"max\":1800}"));
    assert(strstr(json, "\"active_seconds\":{\"state\":\"partial\",\"eligible\":3,\"known\":2,\"sum\":850}"));
    assert(strstr(json, "\"quiet_seconds\":{\"state\":\"partial\",\"eligible\":3,\"known\":2,\"sum\":1050}"));
    assert(strstr(json, "\"unobserved_seconds\":{\"state\":\"partial\",\"eligible\":3,\"known\":2,\"sum\":500}"));
    assert(strstr(json, "\"reported_seconds\":{\"state\":\"partial\",\"eligible\":3,\"known\":2,\"sum\":300}"));
    assert(strstr(json, "\"observed_fraction\":0.791666667,\"quiet_fraction_of_observed\":0.552631579,"));
    assert(strstr(json, "\"started_daily\":[0,0,0,0,0,0,2]"));
    assert(strstr(json, "\"inference\":{\"source\":\"tmux\",\"confidence\":\"inferred\",\"exact\":\"reported_seconds\"}"));
    fclose(out);
    statistics_session_unknowns(&model);
    statistics_session_compare(&model);
}

static void statistics_session_unknowns(const struct hs_model *model) {
    struct hs_filter filter = {0};
    char json[32768];
    FILE *out;
    memcpy(filter.query, "delta", 6);
    out = tmpfile(); assert(out);
    assert(hs_write_metrics_json(out, model, &filter));
    read_all(out, json, sizeof(json));
    assert(strstr(json, "\"count\":1,\"open\":0,\"undated\":1,\"length_seconds\":{\"state\":\"unknown\",\"eligible\":1,\"known\":0,\"sum\":null,\"mean\":null,\"max\":null}"));
    assert(strstr(json, "\"active_seconds\":{\"state\":\"unknown\",\"eligible\":1,\"known\":0,\"sum\":null}"));
    assert(strstr(json, "\"observed_fraction\":null,\"quiet_fraction_of_observed\":null"));
    fclose(out);
}

/* A feed without sessions keeps the earlier export; a comparison with one
 * that has them uses the newer schema and still writes both sides. */
static void statistics_session_compare(const struct hs_model *model) {
    static struct hs_model other;
    struct hs_filter filter = {0};
    char json[32768];
    FILE *out;
    assert(load_fixture(&other));
    out = tmpfile(); assert(out);
    assert(hs_write_metrics_json(out, &other, &filter));
    read_all(out, json, sizeof(json));
    assert(strstr(json, "{\"schema_version\":1,") && !strstr(json, "\"sessions\""));
    fclose(out);
    out = tmpfile(); assert(out);
    assert(hs_write_metrics_compare_json(out, &other, model, &filter));
    read_all(out, json, sizeof(json));
    assert(strstr(json, "{\"schema_version\":3,") && strstr(json, "\"right\":{\"schema_version\":3,") && strstr(json, "\"sessions\""));
    fclose(out);
    out = tmpfile(); assert(out);
    assert(hs_write_metrics_compare_text(out, &other, model, &filter));
    read_all(out, json, sizeof(json));
    assert(strstr(json, "right sessions count=3 open=1 length_known=2 length_sum=2400 activity_known=2 active_sum=850 quiet_sum=1050 coverage_known=2 unobserved_sum=500 reported_sum=300 inferred_from=tmux\n"));
    assert(!strstr(json, "left sessions"));
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

int main(void) {
    statistics_export_cases();
    statistics_compare_cases();
    statistics_session_cases();
    puts("Statistics exporter: bounded JSON metrics, comparison and fail-closed input passed");
    return 0;
}
