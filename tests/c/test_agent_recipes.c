/* Installer recipes (agent-recipes schema 1) and recorded agent locations. */
#include "fleet/setup/agent_setup.h"
#include "fleet/agent/agent.h"
#include "fleet/support/files.h"
#include "fleet/support/json.h"
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>
const char *f_home, *f_hydra = "hydra";

#define expect(condition) check((condition), __LINE__)
static void check(bool ok, int line) {
    if (ok) return;
    fprintf(stderr, "test_agent_recipes.c:%d: expectation failed\n", line);
    exit(1);
}

static const char *valid =
    "{\"agent\":\"x\",\"executable\":\"x-cli\",\"command\":\"curl -fsSL https://example.invalid/x.sh | sh\",\"requires\":[\"curl\"],"
    "\"expected_dirs\":[\"~/.local/bin\"],\"login_args\":[\"login\"],\"docs_url\":\"https://example.invalid/docs\",\"verified_on\":\"2026-09-28\"}";

/* valid with one member replaced (or added) by key:value text. */
static bool variant(const char *key, const char *value) {
    json_object *recipe = f_parse(valid), *replacement = f_parse_value(value); bool ok;
    expect(recipe && replacement);
    json_object_object_add(recipe, key, replacement);
    ok = setup_recipe_valid(recipe);
    json_object_put(recipe);
    return ok;
}
static void write_file(const char *path, const char *text, mode_t mode) {
    FILE *file = fopen(path, "w");
    expect(file != NULL);
    expect(fputs(text, file) >= 0 && !fclose(file) && !chmod(path, mode));
}
static void builtins(void) {
    json_object *document = setup_recipes_builtin(), *list = f_field(document, "recipes"); size_t i;
    const char *agents[] = {"claude", "cursor", "opencode", "codex", "agy", "pi", NULL};
    expect(!strcmp(f_string(document, "schema"), "agent-recipes") && f_number_is(document, "schema_version", 1));
    expect(json_object_array_length(list) == 6);
    for (i = 0; agents[i]; i++) {
        json_object *recipe = json_object_array_get_idx(list, i);
        expect(setup_recipe_valid(recipe) && !strcmp(f_string(recipe, "agent"), agents[i]));
        expect(!strncmp(f_string(recipe, "docs_url"), "https://", 8) && !strcmp(f_string(recipe, "verified_on"), "2026-09-28"));
        expect(!strstr(f_string(recipe, "command"), "sudo"));
    }
    json_object_put(document);
}
static void schema_members(void) {
    json_object *recipe = f_parse(valid);
    expect(setup_recipe_valid(recipe));
    json_object_object_del(recipe, "docs_url");
    expect(!setup_recipe_valid(recipe));
    json_object_put(recipe);
    expect(variant("auth_status_argv", "[\"status\"]"));
    expect(!variant("auth_status_argv", "[]"));
    expect(!variant("extra", "1"));
    expect(!variant("login_args", "\"login\""));
    expect(!variant("verified_on", "\"yesterday\""));
}
static void schema_text(void) {
    expect(!variant("command", "\"curl x\\nrm -rf ~\""));
    expect(!variant("command", "\"\""));
    expect(!variant("docs_url", "\"http://example.invalid\""));
    expect(!variant("executable", "\"../x\""));
    expect(!variant("executable", "\"a/b\""));
    expect(!variant("agent", "\"Bad Name\""));
    expect(!variant("requires", "[\"cu rl\"]"));
    expect(!variant("expected_dirs", "[\"relative\"]"));
    expect(!variant("expected_dirs", "[\"~/../x\"]"));
}
static const char *error_code(json_object *error) { return f_string(f_field(error, "error"), "code"); }
static void builtin_lookup(struct setup_ctx *ctx) {
    json_object *error = NULL, *recipe = setup_recipe(ctx, "claude", &error);
    expect(recipe && !error && !strcmp(f_string(recipe, "source"), "built-in"));
    json_object_put(recipe);
    expect(!setup_recipe(ctx, "copilot", &error) && !error);
}
static void local_lookup(struct setup_ctx *ctx, const char *path) {
    char text[2048]; json_object *error = NULL, *recipe;
    write_file(path, "{\"schema\":\"agent-recipes\",\"schema_version\":1,\"recipes\":[", 0600);
    expect(!setup_recipe(ctx, "claude", &error) && error && !strcmp(error_code(error), "recipe_unavailable"));
    json_object_put(error);
    snprintf(text, sizeof(text), "{\"schema\":\"agent-recipes\",\"schema_version\":1,\"recipes\":[%s]}", valid);
    write_file(path, text, 0600);
    recipe = setup_recipe(ctx, "x", &error);
    expect(recipe && !error && !strcmp(f_string(recipe, "source"), "local") && !strcmp(f_string(recipe, "executable"), "x-cli"));
    json_object_put(recipe);
    recipe = setup_recipe(ctx, "claude", &error);
    expect(recipe && !strcmp(f_string(recipe, "source"), "built-in"));
    json_object_put(recipe);
}
static void local_unsafe(struct setup_ctx *ctx, const char *path) {
    json_object *error = NULL;
    expect(!chmod(path, 0640));
    expect(!setup_recipe(ctx, "x", &error) && error);
    json_object_put(error); error = NULL;
    expect(!chmod(path, 0600) && !unlink(path) && !symlink("/dev/null", path));
    expect(!setup_recipe(ctx, "x", &error) && error);
    json_object_put(error);
    expect(!unlink(path));
}
static void local_override(const char *home) {
    char path[F_PATH]; struct setup_ctx ctx;
    memset(&ctx, 0, sizeof(ctx)); ctx.command = "remote-install-agent";
    expect(!f_path(path, sizeof(path), home, "fleet") && !mkdir(path, 0700));
    expect(!f_path(path, sizeof(path), home, "fleet/agent-recipes.json"));
    builtin_lookup(&ctx);
    local_lookup(&ctx, path);
    local_unsafe(&ctx, path);
}
static void location_rules(const char *exe, const char *other) {
    expect(agent_location_name("cursor-agent") && agent_location_name("a.b_c"));
    expect(!agent_location_name(".x") && !agent_location_name("-x") && !agent_location_name("a/b"));
    expect(agent_location_valid("claude", exe) && !agent_location_valid("claude", other) && !agent_location_valid("claude", "claude"));
    expect(!chmod(exe, 0775) && !agent_location_valid("claude", exe));
    expect(!chmod(exe, 0757) && !agent_location_valid("claude", exe));
    expect(!chmod(exe, 0644) && !agent_location_valid("claude", exe));
    expect(!chmod(exe, 0755));
}
static void location_records(const char *home, const char *exe) {
    char dir[F_PATH], record[F_PATH], text[F_PATH + 2], *found;
    expect(!f_path(dir, sizeof(dir), home, "agents") && !mkdir(dir, 0700));
    expect(!f_path(dir, sizeof(dir), home, "agents/locations") && !mkdir(dir, 0700));
    expect(!f_path(record, sizeof(record), dir, "claude"));
    expect(!agent_location_recorded("claude"));
    snprintf(text, sizeof(text), "%s\n", exe);
    write_file(record, text, 0600);
    found = agent_location_recorded("claude");
    expect(found && !strcmp(found, exe));
    free(found);
    expect(!chmod(record, 0620) && !agent_location_recorded("claude"));
    expect(!chmod(record, 0600) && !chmod(exe, 0775) && !agent_location_recorded("claude"));
    expect(!chmod(exe, 0755));
}
static void locations(const char *home) {
    char bin[F_PATH], exe[F_PATH], other[F_PATH];
    expect(!f_path(bin, sizeof(bin), home, "bin") && !mkdir(bin, 0700));
    expect(!f_path(exe, sizeof(exe), bin, "claude") && !f_path(other, sizeof(other), bin, "claude-2"));
    write_file(exe, "#!/bin/sh\n", 0755);
    write_file(other, "#!/bin/sh\n", 0755);
    location_rules(exe, other);
    location_records(home, exe);
}
int main(void) {
    char temp[] = "/tmp/hydra-recipes-test.XXXXXX", base[F_PATH];
    expect(mkdtemp(temp) && realpath(temp, base));
    f_home = base;
    builtins();
    schema_members();
    schema_text();
    local_override(base);
    locations(base);
    expect(!f_remove_tree(base));
    puts("agent recipe tests passed");
    return 0;
}
