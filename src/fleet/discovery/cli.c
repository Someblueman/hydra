#include "fleet/discovery/discovery.h"
#include "fleet/support/json.h"
#include "fleet/support/process.h"
#include "fleet/support/files.h"
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
static bool progress_path(const struct hd_options *options, const char *value) {
    return options->probe && value[0] == '/' && hd_text(value, F_PATH);
}
static bool option_value(struct hd_options *options, const char *key, const char *value) {
    if (!strcmp(key, "--ssh")) return options->ssh_count < HD_QUALIFY_BATCH && selection(options->ssh, &options->ssh_count, value);
    if (!strcmp(key, "--select")) return selection(options->select, &options->select_count, value);
    if (!strcmp(key, "--inventory") && !options->inventory) { options->inventory = value; return true; }
    if (!strcmp(key, "--ssh-config") && !options->config) { options->config = value; return true; }
    if (!strcmp(key, "--progress") && !options->progress) { options->progress = value; return progress_path(options, value); }
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
json_object *hd_cli(int argc, char **argv) {
    struct hd_options options = {.seconds = 5, .capability = "list"}; json_object *rows;
    options.probe = argc > 0 && !strcmp(argv[0], "qualify");
    if (!parse_options(argc, argv, &options)) return f_error("fleet-discovery", "invalid_input",
        "fleet discover|qualify --ssh ALIAS ... [--inventory FILE --select NAME ...] [--ssh-config /path] [--timeout 1-30] [--require CAPABILITY (qualify only)] [--progress /absolute/export.json (qualify only)] [--json]");
    rows = hd_sources(&options);
    if (!rows) return f_error("fleet-discovery", "invalid_inventory", "invalid selector or closed-schema inventory; select at most 100 inventory targets or 16 SSH aliases");
    return hd_batch(&options, rows);
}
