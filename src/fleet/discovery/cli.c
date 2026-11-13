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
static bool source_value(struct hd_options *options, const char *value) {
    size_t i;
    if (options->source_count == HD_SOURCES || !hd_source_valid(value)) return false;
    for (i = 0; i < options->source_count; i++) if (!strcmp(options->sources[i], value)) return false;
    options->sources[options->source_count++] = value; return true;
}
static bool option_value(struct hd_options *options, const char *key, const char *value) {
    if (!strcmp(key, "--ssh")) return options->ssh_count < HD_QUALIFY_BATCH && selection(options->ssh, &options->ssh_count, value);
    if (!strcmp(key, "--select")) return selection(options->select, &options->select_count, value);
    if (!strcmp(key, "--source")) return source_value(options, value);
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
        if (!strcmp(argv[i], "--diff")) { options->diff = true; continue; }
        if (i + 1 == argc || !option_value(options, argv[i], argv[i + 1])) return false;
        i++;
    }
    /* Selections need an inventory or a live source; qualify never connects
     * to every host a live source lists, only to explicitly selected names. */
    if (options->select_count && !options->inventory && !options->source_count) return false;
    if (options->inventory && !options->select_count) return false;
    if (options->probe && options->source_count && !options->select_count) return false;
    return (options->ssh_count || options->select_count || options->source_count) &&
        (!options->config || hd_text(options->config, F_PATH));
}
static json_object *with_sources(json_object *result, const struct hd_options *options) {
    if (options->live) json_object_object_add(result, "data", hd_live_public(options->live, options->diff));
    return result;
}
json_object *hd_cli(int argc, char **argv) {
    struct hd_options options = {.seconds = 5, .capability = "list"}; json_object *rows, *result;
    options.probe = argc > 0 && !strcmp(argv[0], "qualify");
    if (!parse_options(argc, argv, &options)) return f_error("fleet-discovery", "invalid_input",
        "fleet discover|qualify --ssh ALIAS ... [--inventory FILE --select NAME ...] [--source ssh-config[:FILE]|ansible-inventory:FILE|dns-sd[:DOMAIN] ... [--select NAME ...] [--diff]] [--ssh-config /path] [--timeout 1-30] [--require CAPABILITY (qualify only)] [--progress /absolute/export.json (qualify only)] [--json]; qualify with --source requires --select");
    if (options.source_count) {
        options.live = hd_live_acquire(&options);
        if (!options.live) return f_error("fleet-discovery", "invalid_input", "invalid or repeated --source selector; use ssh-config[:/absolute/file], ansible-inventory:/absolute/file or dns-sd[:domain]");
    }
    rows = hd_sources(&options);
    if (!rows) {
        result = with_sources(f_error("fleet-discovery", "invalid_inventory", "invalid selector or closed-schema inventory; select at most 100 inventory targets or 16 SSH aliases; a --select must name an importable record (not conflicting or revoked)"), &options);
        json_object_put(options.live); return result;
    }
    result = hd_batch(&options, rows);
    json_object_put(options.live);
    return result;
}
