#define _XOPEN_SOURCE 700
#include "fleet/plan/plan_reuse.h"
#include "fleet/task/task.h"
#include "fleet/support/files.h"
#include <stdlib.h>
#include <string.h>
#include <sys/utsname.h>
#include <unistd.h>

static bool resolve_tool(const char *name, char resolved[F_PATH]) {
    char *paths = NULL, *save = NULL, *part, candidate[F_PATH]; bool found = false;
    const char *search = getenv("PATH");
    if (!f_name(name) || !search || !*search || search[0] == ':' ||
        search[strlen(search) - 1] == ':' || strstr(search, "::") || !(paths = strdup(search))) return false;
    for (part = strtok_r(paths, ":", &save); part; part = strtok_r(NULL, ":", &save)) {
        /* Empty/relative PATH components cannot define a stable tool identity. */
        if (part[0] != '/' || f_path(candidate, sizeof(candidate), part, name)) break;
        if (!access(candidate, X_OK)) { found = realpath(candidate, resolved) != NULL; break; }
    }
    free(paths); return found;
}
static bool tools_match(json_object *tools, const char *command) {
    const char *const keys[] = {"path", "sha256", NULL};
    if (!json_object_is_type(tools, json_type_object) || !f_field(tools, command) ||
        json_object_object_length(tools) > 64) return false;
    json_object_object_foreach(tools, name, tool) {
        char actual[F_PATH], hash[65]; const char *path = f_string(tool, "path"), *digest = f_string(tool, "sha256");
        if (!task_keys(tool, keys) || !path || !task_hex(digest, 64) || !resolve_tool(name, actual) ||
            strcmp(actual, path) || f_hash(actual, hash) || strcmp(hash, digest)) return false;
    }
    return true;
}
static bool variables_match(json_object *variables) {
    const char *const keys[] = {"PATH", "LANG", "LC_ALL", "LC_CTYPE", "TZ", NULL};
    const char *const unsupported[] = {"LD_PRELOAD", "LD_LIBRARY_PATH", "DYLD_INSERT_LIBRARIES", "DYLD_LIBRARY_PATH", "ENV", "BASH_ENV", NULL};
    for (size_t i = 0; unsupported[i]; i++) {
        const char *value = getenv(unsupported[i]);
        if (value && *value) return false;
    }
    if (!task_keys(variables, keys) || json_object_object_length(variables) != 5) return false;
    for (size_t i = 0; keys[i]; i++) {
        const char *expected = f_string(variables, keys[i]), *actual = getenv(keys[i]);
        if (!expected || strlen(expected) > 8192 || strcmp(expected, actual ? actual : "")) return false;
    }
    return true;
}
bool pr_environment(const char *path, json_object *binding) {
    const char *const keys[] = {"schema_version", "scope", "dependencies", "external_observations", "effects", "platform", "tools", "variables", NULL};
    const char *const platform_keys[] = {"system", "release", "machine", NULL};
    json_object *manifest = plan_read(path), *platform = f_field(manifest, "platform"), *spec = f_field(binding, "spec");
    struct utsname current; bool valid = false;
    const char *command = f_text(json_object_array_get_idx(f_field(f_field(spec, "work"), "argv"), 0));
    if (!f_string(spec, "host") || strcmp(f_string(spec, "host"), "local") || !command ||
        !task_keys(manifest, keys) || !f_number_is(manifest, "schema_version", 1) ||
        !plan_text(f_field(manifest, "scope")) || !plan_list(f_field(manifest, "dependencies"), 1, 64) ||
        !json_object_is_type(f_field(manifest, "external_observations"), json_type_boolean) ||
        json_object_get_boolean(f_field(manifest, "external_observations")) ||
        !f_string(manifest, "effects") || strcmp(f_string(manifest, "effects"), "artifact_only") ||
        !task_keys(platform, platform_keys) || uname(&current)) goto done;
    const char *observed[] = {current.sysname, current.release, current.machine};
    for (size_t i = 0; platform_keys[i]; i++) {
        const char *expected = f_string(platform, platform_keys[i]);
        if (!expected || strcmp(expected, observed[i])) goto done;
    }
    json_object *dependencies = f_field(manifest, "dependencies");
    for (size_t i = 0; i < json_object_array_length(dependencies); i++)
        if (!plan_text(json_object_array_get_idx(dependencies, i))) goto done;
    valid = tools_match(f_field(manifest, "tools"), command) && variables_match(f_field(manifest, "variables"));
done:
    json_object_put(manifest); return valid;
}
int plan_reuse_environment(json_object *compiled, const char *scratch, json_object *errors) {
    json_object *plan = f_field(compiled, "plan"), *policy = f_field(plan, "reuse_policy");
    if (!policy) return 0;
    json_object_object_foreach(f_field(policy, "steps"), id, rule) {
        const char *name = f_string(rule, "environment_input"); char path[F_PATH];
        json_object *inputs = f_field(f_field(f_field(plan, "data"), "steps"), id);
        const char *input = f_string(f_field(f_field(inputs, "inputs"), name), "input");
        json_object *decl = f_field(f_field(f_field(compiled, "data"), "inputs"), input);
        if (!f_string(decl, "type") || strcmp(f_string(decl, "type"), "object") ||
            snprintf(path, sizeof(path), "%s/artifacts/%s", scratch, input) >= (int)sizeof(path) ||
            !pr_environment(path, f_field(f_field(compiled, "tasks"), id))) {
            plan_error(errors, "reuse_policy", "unsupported_reuse_environment", "sealed-artifact reuse currently requires local execution and a closed source-bound environment manifest matching platform, declared tool hashes, PATH and locale variables");
            return -1;
        }
    }
    return 0;
}
