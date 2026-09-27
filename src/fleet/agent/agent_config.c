#include "fleet/support/json.h"
#include "fleet/support/files.h"
#include "fleet/agent/agent.h"
#include <stdlib.h>
#include <string.h>

/* Provider configuration that decides the model and reasoning effort when a
 * recipe does not. It is read at launch and recorded as configuration, never
 * as an observation of what the provider actually used. */

#define CONFIG_LIMIT (256U * 1024U)
#define CONFIG_VALUE 96U

static bool config_value_safe(const char *value) {
    size_t i;
    if (!*value) return false;
    for (i = 0; value[i]; i++) {
        unsigned char c = (unsigned char)value[i];
        if (c < 32 || c == 127 || c == '"' || c == '\\') return false;
    }
    return i < CONFIG_VALUE;
}

/* A single-line TOML string assignment `key = "value"`; anything else is not
 * a value this reader claims to understand. */
static bool toml_string(const char *line, const char *key, char *out, size_t size) {
    size_t length = strlen(key);
    const char *cursor = line, *end;
    while (*cursor == ' ' || *cursor == '\t') cursor++;
    if (strncmp(cursor, key, length)) return false;
    cursor += length;
    while (*cursor == ' ' || *cursor == '\t') cursor++;
    if (*cursor++ != '=') return false;
    while (*cursor == ' ' || *cursor == '\t') cursor++;
    if (*cursor++ != '"' || !(end = strchr(cursor, '"')) || (size_t)(end - cursor) >= size) return false;
    memcpy(out, cursor, (size_t)(end - cursor)); out[end - cursor] = '\0';
    return config_value_safe(out);
}

struct toml_keys { char model[CONFIG_VALUE], effort[CONFIG_VALUE], profile[CONFIG_VALUE]; };

static void toml_keys_line(const char *line, bool top, struct toml_keys *keys) {
    (void)toml_string(line, "model", keys->model, sizeof(keys->model));
    (void)toml_string(line, "model_reasoning_effort", keys->effort, sizeof(keys->effort));
    if (top) (void)toml_string(line, "profile", keys->profile, sizeof(keys->profile));
}

/* Reads top-level keys, or keys of [profiles.<profile>] when profile is set. */
static void toml_section(char *text, const char *profile, struct toml_keys *keys) {
    char section[CONFIG_VALUE + 16] = "", wanted[CONFIG_VALUE + 16] = "";
    char *save = NULL, *line;
    if (profile) snprintf(wanted, sizeof(wanted), "[profiles.%s]", profile);
    for (line = strtok_r(text, "\n", &save); line; line = strtok_r(NULL, "\n", &save)) {
        char *trimmed = line + strspn(line, " \t");
        if (*trimmed == '[') snprintf(section, sizeof(section), "%.*s", (int)strcspn(trimmed, " \t\r#"), trimmed);
        else if (!strcmp(section, wanted)) toml_keys_line(trimmed, !profile, keys);
    }
}

static int codex_config_path(char *path, size_t size) {
    const char *home = getenv("CODEX_HOME"), *user = getenv("HOME");
    if (home && *home) return f_path(path, size, home, "config.toml");
    if (!user || !*user) return -1;
    return snprintf(path, size, "%s/.codex/config.toml", user) >= (int)size ? -1 : 0;
}

static void config_add(json_object *record, const char *key, const char *value, const char *source) {
    json_object *entry;
    if (!*value) return;
    entry = json_object_new_object();
    f_string_add(entry, "value", value);
    f_string_add(entry, "source", source);
    json_object_object_add(record, key, entry);
}

static void codex_file(json_object *record, const char *path) {
    struct toml_keys top = {"", "", ""}, selected = {"", "", ""};
    char *text = f_read(path, CONFIG_LIMIT), *copy;
    if (!text) return;
    copy = strdup(text);
    toml_section(text, NULL, &top);
    if (copy && top.profile[0]) toml_section(copy, top.profile, &selected);
    config_add(record, "model", selected.model[0] ? selected.model : top.model, path);
    config_add(record, "reasoning_effort", selected.effort[0] ? selected.effort : top.effort, path);
    free(text); free(copy);
}

/* Literal recipe arguments are explicit requests; they win over the file. */
static void recipe_arguments(json_object *record, json_object *args) {
    size_t i, count = json_object_array_length(args);
    for (i = 0; i + 1 < count; i++) {
        const char *flag = f_text(json_object_array_get_idx(args, i));
        const char *value = f_text(json_object_array_get_idx(args, i + 1));
        char parsed[CONFIG_VALUE];
        if (!flag || !value) continue;
        if ((!strcmp(flag, "-m") || !strcmp(flag, "--model")) && config_value_safe(value))
            config_add(record, "model", value, "profile arguments");
        else if ((!strcmp(flag, "-c") || !strcmp(flag, "--config")) && toml_string(value, "model", parsed, sizeof(parsed)))
            config_add(record, "model", parsed, "profile arguments");
        else if ((!strcmp(flag, "-c") || !strcmp(flag, "--config")) && toml_string(value, "model_reasoning_effort", parsed, sizeof(parsed)))
            config_add(record, "reasoning_effort", parsed, "profile arguments");
    }
}

static bool codex_profile(json_object *profile) {
    const char *executable = f_string(profile, "executable"), *base;
    if (!executable) return false;
    base = strrchr(executable, '/');
    return !strcmp(base ? base + 1 : executable, "codex");
}

/* Returns a caller-owned {"model":{value,source},"reasoning_effort":...}
 * object, or NULL when nothing is configured that Hydra can read. */
json_object *agent_configuration(json_object *profile, json_object *args) {
    json_object *record = json_object_new_object();
    char path[F_PATH];
    if (codex_profile(profile)) {
        if (!codex_config_path(path, sizeof(path))) codex_file(record, path);
        recipe_arguments(record, args);
    }
    if (!json_object_object_length(record)) { json_object_put(record); return NULL; }
    f_string_add(record, "scope", "provider configuration read at launch; not an observation of the model the provider used");
    return record;
}
