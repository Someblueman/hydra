#include "fleet/setup/agent_setup.h"
#include "fleet/agent/agent.h"
#include "fleet/support/files.h"
#include "fleet/support/json.h"
#include "fleet/task/task.h"
#include <errno.h>
#include <fcntl.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

/*
 * Built-in installer recipes (design §7). Each command was checked against the
 * provider's own installation page on verified_on; re-verify at every release.
 * Only official scripted installers that write under the user's home directory
 * are listed. They run on the remote as the SSH user; Hydra adds no sudo, no
 * PATH edits and no symlinks. Agents without a verified installer (copilot,
 * aider, gemini) have no recipe and get manual instructions instead.
 * Sign-in: login_args follow the executable (empty: running the agent itself
 * starts sign-in). auth_status_argv is listed only where the provider
 * documents its exit status (Claude Code: 0 signed in, 1 signed out).
 */
static const char *const builtin_recipes[] = {
    "{\"agent\":\"claude\",\"executable\":\"claude\",\"command\":\"curl -fsSL https://claude.ai/install.sh | bash\","
    "\"requires\":[\"curl\",\"bash\"],\"expected_dirs\":[\"~/.local/bin\"],\"login_args\":[\"auth\",\"login\"],"
    "\"auth_status_argv\":[\"auth\",\"status\"],\"docs_url\":\"https://code.claude.com/docs/en/setup\",\"verified_on\":\"2026-09-28\"}",
    "{\"agent\":\"cursor\",\"executable\":\"cursor-agent\",\"command\":\"curl https://cursor.com/install -fsS | bash\","
    "\"requires\":[\"curl\",\"bash\"],\"expected_dirs\":[\"~/.local/bin\"],\"login_args\":[\"login\"],"
    "\"docs_url\":\"https://cursor.com/docs/cli/installation\",\"verified_on\":\"2026-09-28\"}",
    "{\"agent\":\"opencode\",\"executable\":\"opencode\",\"command\":\"curl -fsSL https://opencode.ai/install | bash\","
    "\"requires\":[\"curl\",\"bash\"],\"expected_dirs\":[\"~/.opencode/bin\"],\"login_args\":[\"auth\",\"login\"],"
    "\"docs_url\":\"https://opencode.ai/docs/\",\"verified_on\":\"2026-09-28\"}",
    "{\"agent\":\"codex\",\"executable\":\"codex\",\"command\":\"curl -fsSL https://chatgpt.com/codex/install.sh | sh\","
    "\"requires\":[\"curl\"],\"expected_dirs\":[\"~/.local/bin\"],\"login_args\":[\"login\",\"--device-auth\"],"
    "\"docs_url\":\"https://learn.chatgpt.com/docs/codex/cli\",\"verified_on\":\"2026-09-28\"}",
    "{\"agent\":\"agy\",\"executable\":\"agy\",\"command\":\"curl -fsSL https://antigravity.google/cli/install.sh | bash\","
    "\"requires\":[\"curl\",\"bash\"],\"expected_dirs\":[\"~/.local/bin\"],\"login_args\":[],"
    "\"docs_url\":\"https://antigravity.google/docs/cli/install/\",\"verified_on\":\"2026-09-28\"}",
    "{\"agent\":\"pi\",\"executable\":\"pi\",\"command\":\"curl -fsSL https://pi.dev/install.sh | sh\","
    "\"requires\":[\"curl\",\"npm\"],\"expected_dirs\":[\"~/.pi/agent/bin\",\"~/.local/bin\",\"~/bin\"],\"login_args\":[],"
    "\"docs_url\":\"https://pi.dev\",\"verified_on\":\"2026-09-28\"}",
    NULL
};

static bool printable(const char *text, size_t limit) {
    size_t i, length = text ? strlen(text) : 0;
    if (!length || length > limit) return false;
    for (i = 0; i < length; i++) if ((unsigned char)text[i] < 0x20 || (unsigned char)text[i] > 0x7e) return false;
    return true;
}
static bool tool_name(const char *text) {
    return printable(text, 32) && strspn(text, "abcdefghijklmnopqrstuvwxyz0123456789_-") == strlen(text);
}
static bool directory_text(const char *text) {
    return printable(text, 255) && (text[0] == '/' || !strncmp(text, "~/", 2)) && !strstr(text, "..");
}
static bool argument_text(const char *text) { return printable(text, 256); }
static bool list_valid(json_object *list, size_t limit, bool (*valid)(const char *)) {
    size_t i;
    if (!json_object_is_type(list, json_type_array) || json_object_array_length(list) > limit) return false;
    for (i = 0; i < json_object_array_length(list); i++)
        if (!valid(f_text(json_object_array_get_idx(list, i)))) return false;
    return true;
}
static bool date_text(const char *text) {
    return text && strlen(text) == 10 && strspn(text, "0123456789-") == 10 && text[4] == '-' && text[7] == '-';
}
bool setup_recipe_valid(json_object *recipe) {
    static const char *const keys[] = {"agent", "executable", "command", "requires", "expected_dirs", "login_args",
                                       "auth_status_argv", "docs_url", "verified_on", NULL};
    const char *agent = f_string(recipe, "agent"), *url = f_string(recipe, "docs_url");
    json_object *status = f_field(recipe, "auth_status_argv");
    if (!task_keys(recipe, keys) || !agent || !f_name(agent) || strlen(agent) >= 64) return false;
    if (!agent_location_name(f_string(recipe, "executable")) || !printable(f_string(recipe, "command"), 4096)) return false;
    if (!list_valid(f_field(recipe, "requires"), 16, tool_name) || !list_valid(f_field(recipe, "expected_dirs"), 8, directory_text)) return false;
    if (!list_valid(f_field(recipe, "login_args"), 16, argument_text)) return false;
    if (status && (!list_valid(status, 16, argument_text) || !json_object_array_length(status))) return false;
    return printable(url, 511) && !strncmp(url, "https://", 8) && date_text(f_string(recipe, "verified_on"));
}
json_object *setup_recipes_builtin(void) {
    json_object *document = json_object_new_object(), *list = json_object_new_array(); size_t i;
    f_string_add(document, "schema", "agent-recipes");
    json_object_object_add(document, "schema_version", json_object_new_int(1));
    for (i = 0; builtin_recipes[i]; i++) json_object_array_add(list, f_parse(builtin_recipes[i]));
    json_object_object_add(document, "recipes", list);
    return document;
}

/* 0 absent, 1 read into *text, -1 unsafe or unreadable. */
static int private_read(const char *path, char **text) {
    struct stat st; int fd = open(path, O_RDONLY | O_NOFOLLOW | O_NONBLOCK | O_CLOEXEC); ssize_t n;
    *text = NULL;
    if (fd < 0) return errno == ENOENT ? 0 : -1;
    if (fstat(fd, &st) || !S_ISREG(st.st_mode) || st.st_uid != geteuid() || (st.st_mode & 0077) ||
        st.st_size <= 0 || st.st_size > (off_t)AGENT_RECIPES_LIMIT || !(*text = malloc((size_t)st.st_size + 1))) {
        close(fd); return -1;
    }
    n = read(fd, *text, (size_t)st.st_size); close(fd);
    if (n != (ssize_t)st.st_size || memchr(*text, 0, (size_t)n)) { free(*text); *text = NULL; return -1; }
    (*text)[n] = '\0';
    return 1;
}
static bool document_valid(json_object *document) {
    static const char *const keys[] = {"schema", "schema_version", "recipes", NULL};
    json_object *list = f_field(document, "recipes"); const char *schema = f_string(document, "schema"); size_t i;
    if (!task_keys(document, keys) || !schema || strcmp(schema, "agent-recipes") || !f_number_is(document, "schema_version", 1) ||
        !json_object_is_type(list, json_type_array) || json_object_array_length(list) > 32) return false;
    for (i = 0; i < json_object_array_length(list); i++) if (!setup_recipe_valid(json_object_array_get_idx(list, i))) return false;
    return true;
}
/* Caller-owned local document, NULL when absent; -1 in *status when unsafe. */
static json_object *local_document(int *status) {
    char path[F_PATH], *text = NULL; json_object *document;
    *status = 0;
    if (f_path(path, sizeof(path), f_home, "fleet/agent-recipes.json")) { *status = -1; return NULL; }
    *status = private_read(path, &text);
    if (*status <= 0) return NULL;
    document = f_parse(text); free(text);
    if (document_valid(document)) return document;
    json_object_put(document); *status = -1; return NULL;
}
static json_object *find(json_object *document, const char *agent, const char *source) {
    json_object *list = f_field(document, "recipes"), *copy = NULL; size_t i;
    for (i = 0; json_object_is_type(list, json_type_array) && i < json_object_array_length(list); i++) {
        json_object *recipe = json_object_array_get_idx(list, i);
        if (strcmp(f_string(recipe, "agent"), agent) || json_object_deep_copy(recipe, &copy, NULL)) continue;
        f_string_add(copy, "source", source);
        return copy;
    }
    return NULL;
}
json_object *setup_recipe(const struct setup_ctx *ctx, const char *agent, json_object **error) {
    int status; json_object *local = local_document(&status), *recipe = NULL, *builtin;
    *error = NULL;
    if (status < 0) {
        *error = setup_error(ctx, "recipe_unavailable", "the local recipe file $HYDRA_HOME/fleet/agent-recipes.json is unsafe or invalid",
                             "make it a private (0600) agent-recipes schema 1 file owned by you, or remove it", NULL);
        return NULL;
    }
    if (local) recipe = find(local, agent, "local");
    json_object_put(local);
    if (recipe) return recipe;
    builtin = setup_recipes_builtin();
    recipe = find(builtin, agent, "built-in");
    json_object_put(builtin);
    return recipe;
}
