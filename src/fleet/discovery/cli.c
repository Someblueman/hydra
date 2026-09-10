#include "fleet/discovery/discovery.h"
#include "fleet/support/json.h"
#include "fleet/support/process.h"
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

static bool selection(const char **items, size_t *count, const char *value) {
    size_t i;
    for (i = 0; i < *count; i++) if (!strcmp(items[i], value)) return true;
    if (*count == HD_HOSTS) return false;
    items[(*count)++] = value; return true;
}
static bool option_value(struct hd_options *options, const char *key, const char *value) {
    if (!strcmp(key, "--ssh")) return options->ssh_count < HD_QUALIFY_BATCH && selection(options->ssh, &options->ssh_count, value);
    if (!strcmp(key, "--select")) return selection(options->select, &options->select_count, value);
    if (!strcmp(key, "--inventory") && !options->inventory) { options->inventory = value; return true; }
    if (!strcmp(key, "--ssh-config") && !options->config) { options->config = value; return true; }
    if (!strcmp(key, "--require") && options->probe && hd_text(value, 128)) { options->capability = value; return true; }
    if (!strcmp(key, "--timeout")) {
        char *end; unsigned long seconds = strtoul(value, &end, 10);
        if (!*value || *end || seconds < 1 || seconds > 30) return false;
        options->seconds = (unsigned)seconds; return true;
    }
    return false;
}
static bool parse_options(int argc, char **argv, struct hd_options *options) {
    int i;
    for (i = 1; i < argc; i++) {
        if (!strcmp(argv[i], "--json")) continue;
        if (i + 1 == argc || !option_value(options, argv[i], argv[i + 1])) return false;
        i++;
    }
    return (options->ssh_count || options->select_count) &&
        (!!options->inventory == !!options->select_count) &&
        (!options->config || hd_text(options->config, F_PATH));
}
static bool enrich(json_object *row, const struct hd_options *options, const char *config) {
    json_object *resolved, *probe;
    bool ok;
    resolved = f_stopped ? f_error("host-resolve", "cancelled", "selection was interrupted before resolution") :
        hd_resolve(f_string(row, "target"), config, options->seconds);
    json_object_object_add(row, "resolution", resolved);
    ok = json_object_get_boolean(f_field(resolved, "ok"));
    f_string_add(row, "status", ok ? "discovered" : "resolution_failed");
    if (!options->probe) return ok;
    if (!ok) probe = f_error("fleet-qualify", f_stopped ? "cancelled" : "not_checked", "effective SSH configuration is unavailable");
    else probe = hd_probe(row, config, options);
    json_object_object_add(row, "qualification", probe);
    json_object_object_add(row, "qualified_at", json_object_new_int64((int64_t)time(NULL)));
    ok = json_object_get_boolean(f_field(probe, "ok"));
    f_string_add(row, "status", ok ? "compatible" : "qualification_failed");
    return ok;
}
static json_object *run_discovery(const struct hd_options *options, json_object *rows) {
    char config[F_PATH]; json_object *data, *result; size_t i; bool failed = false;
    if (hd_config(config, options->config)) { json_object_put(rows); return f_error("fleet-discovery", "ssh_config_failed", "cannot prepare private strict SSH config; use an absolute literal config path"); }
    for (i = 0; i < json_object_array_length(rows); i++)
        if (!enrich(json_object_array_get_idx(rows, i), options, config)) failed = true;
    unlink(config);
    data = json_object_new_object();
    json_object_object_add(data, "candidate_schema_version", json_object_new_int(1));
    json_object_object_add(data, "observed_at", json_object_new_int64((int64_t)time(NULL)));
    json_object_object_add(data, "candidates", rows);
    json_object_object_add(data, "qualification_batch_size", json_object_new_int(HD_QUALIFY_BATCH));
    json_object_object_add(data, "qualification_batch_count", json_object_new_int((int)((json_object_array_length(rows) + HD_QUALIFY_BATCH - 1) / HD_QUALIFY_BATCH)));
    json_object_object_add(data, "partial_failure", json_object_new_boolean(failed));
    f_string_add(data, "required_capability", options->probe ? options->capability : "");
    result = failed ? f_error("fleet-discovery", f_stopped ? "cancelled" : "partial_failure", "one or more selected candidates could not be resolved or qualified") : f_success("fleet-discovery", NULL);
    json_object_object_add(result, "data", data); return result;
}
json_object *hd_cli(int argc, char **argv) {
    struct hd_options options = {.seconds = 5, .capability = "list"}; json_object *rows;
    options.probe = argc > 0 && !strcmp(argv[0], "qualify");
    if (!parse_options(argc, argv, &options)) return f_error("fleet-discovery", "invalid_input",
        "fleet discover|qualify --ssh ALIAS ... [--inventory FILE --select NAME ...] [--ssh-config /path] [--timeout 1-30] [--require CAPABILITY (qualify only)] [--json]");
    rows = hd_sources(&options);
    if (!rows) return f_error("fleet-discovery", "invalid_inventory", "invalid selector or closed-schema inventory; select at most 16 distinct targets");
    return run_discovery(&options, rows);
}
