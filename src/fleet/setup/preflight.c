/* Read-only remote preflight (design §4). A fixed POSIX script runs through
 * strict SSH as `exec /bin/sh -s`; it needs no Hydra, never uses sudo and only
 * prints bounded key<TAB>value lines that are parsed into the frozen
 * `remote-preflight` schema 1: os, arch, home, path, umask, tools{...},
 * hydra{path,version}, pins[], agents[], plus requirements[]. */
#include "fleet/setup/setup.h"
#include "fleet/support/files.h"
#include "fleet/support/json.h"
#include "fleet/support/process.h"
#include <stdlib.h>
#include <string.h>

#define PREFLIGHT_LIMIT 65536U
#define PREFLIGHT_FREE_KB 51200L

static const char script[] =
    "LC_ALL=C; export LC_ALL\n"
    "kv() { printf '%s\\t%s\\n' \"$1\" \"$2\"; }\n"
    "# Executable regular files only: dash's command -v ignores the file mode.\n"
    "tool_path() {\n"
    "  old_ifs=$IFS; IFS=:; set -f\n"
    "  for dir in ${PATH-}; do\n"
    "    if [ -n \"$dir\" ] && [ -f \"$dir/$1\" ] && [ -x \"$dir/$1\" ]; then IFS=$old_ifs; set +f; printf '%s' \"$dir/$1\"; return 0; fi\n"
    "  done\n"
    "  IFS=$old_ifs; set +f; return 1\n"
    "}\n"
    "kv os \"$(uname -s 2>/dev/null)\"\n"
    "kv arch \"$(uname -m 2>/dev/null)\"\n"
    "kv home \"${HOME-}\"\n"
    "kv path \"${PATH-}\"\n"
    "kv umask \"$(umask)\"\n"
    "if [ -n \"${HOME-}\" ] && [ -d \"$HOME\" ] && [ -w \"$HOME\" ]; then kv home_writable yes; else kv home_writable no; fi\n"
    "kv free_kb \"$(df -Pk \"${HOME:-/}\" 2>/dev/null | awk 'NR==2 {print $4}')\"\n"
    "for tool in git tmux curl wget sha256sum shasum mktemp head tail node npm; do\n"
    "  kv \"tool.$tool\" \"$(tool_path \"$tool\")\"\n"
    "done\n"
    "if tmux_path=$(tool_path tmux); then kv tmux_version \"$(\"$tmux_path\" -V 2>/dev/null)\"; fi\n"
    "hydra_path=$(tool_path hydra)\n"
    "kv hydra.path \"$hydra_path\"\n"
    "case \"$hydra_path\" in /*) kv hydra.version \"$(\"$hydra_path\" --version 2>/dev/null | head -n 1)\" ;; esac\n"
    "if [ -d \"${HOME-}/.hydra\" ]; then kv hydra_home present; else kv hydra_home absent; fi\n"
    "for pin in \"${HOME-}\"/.local/share/hydra/fleet/*; do\n"
    "  if [ -x \"$pin/bin/hydra\" ]; then kv pin \"$pin\"; fi\n"
    "done\n"
    "for name in claude codex opencode cursor-agent agy pi; do\n"
    "  found=$(tool_path \"$name\")\n"
    "  case \"$found\" in /*) kv agent \"$name\t$found\tPATH\" ;; esac\n"
    "  for dir in \"$HOME/.local/bin\" \"$HOME/bin\" \"$HOME/.claude/local\" \"$HOME/.opencode/bin\" \"$HOME/.npm-global/bin\" \\\n"
    "      \"$HOME/.bun/bin\" \"$HOME/.volta/bin\" \"$HOME/.local/share/pnpm\" \"$HOME/.cargo/bin\" \"$HOME/.local/share/mise/shims\" \\\n"
    "      \"$HOME/.asdf/shims\" \"$HOME\"/.nvm/versions/node/*/bin /usr/local/bin /opt/homebrew/bin \\\n"
    "      /home/linuxbrew/.linuxbrew/bin /snap/bin; do\n"
    "    if [ -x \"$dir/$name\" ] && [ ! -d \"$dir/$name\" ]; then kv agent \"$name\t$dir/$name\t$dir\"; fi\n"
    "  done\n"
    "done\n"
    "exit 0\n";

static const char *const tool_names[] = {"git", "tmux", "curl", "wget", "sha256sum", "shasum", "mktemp", "head", "tail", "node", "npm", NULL};

/* ---- Parsing ---- */
static bool key_text(const char *key) {
    return key[0] && strspn(key, "abcdefghijklmnopqrstuvwxyz0123456789_.-") == strlen(key);
}
static bool value_text(const char *value) {
    for (; *value; value++) if ((unsigned char)*value < 0x20 && *value != '\t') return false;
    return true;
}
static void add_agent(json_object *agents, char *value) {
    char *save = NULL, *name = strtok_r(value, "\t", &save), *path = strtok_r(NULL, "\t", &save), *source = strtok_r(NULL, "\t", &save);
    json_object *row; size_t i;
    if (!name || !path || !source || path[0] != '/') return;
    for (i = 0; i < json_object_array_length(agents); i++) {
        row = json_object_array_get_idx(agents, i);
        if (strcmp(f_string(row, "path"), path)) continue;
        if (!strcmp(source, "PATH")) json_object_object_add(row, "on_path", json_object_new_boolean(true));
        return;
    }
    row = json_object_new_object();
    f_string_add(row, "executable", name); f_string_add(row, "path", path);
    f_string_add(row, "source", source);
    json_object_object_add(row, "on_path", json_object_new_boolean(!strcmp(source, "PATH")));
    json_object_array_add(agents, row);
}
static void add_value(json_object *snapshot, const char *key, char *value) {
    json_object *tools = f_field(snapshot, "tools"), *hydra = f_field(snapshot, "hydra");
    if (!strncmp(key, "tool.", 5)) {
        if (json_object_object_get_ex(tools, key + 5, NULL) && value[0] == '/') f_string_add(tools, key + 5, value);
    } else if (!strcmp(key, "hydra.path") || !strcmp(key, "hydra.version")) {
        if (value[0]) f_string_add(hydra, key + 6, value);
    } else if (!strcmp(key, "pin")) {
        if (value[0] == '/') json_object_array_add(f_field(snapshot, "pins"), json_object_new_string(value));
    } else if (!strcmp(key, "agent")) add_agent(f_field(snapshot, "agents"), value);
    else if (!f_field(snapshot, key)) f_string_add(snapshot, key, value);
}
static json_object *snapshot_new(void) {
    json_object *snapshot = json_object_new_object(), *tools = json_object_new_object(), *hydra = json_object_new_object();
    size_t i;
    f_string_add(snapshot, "schema", "remote-preflight");
    json_object_object_add(snapshot, "schema_version", json_object_new_int(1));
    for (i = 0; tool_names[i]; i++) json_object_object_add(tools, tool_names[i], NULL);
    json_object_object_add(hydra, "path", NULL); json_object_object_add(hydra, "version", NULL);
    json_object_object_add(snapshot, "tools", tools); json_object_object_add(snapshot, "hydra", hydra);
    json_object_object_add(snapshot, "pins", json_object_new_array());
    json_object_object_add(snapshot, "agents", json_object_new_array());
    return snapshot;
}
/* Parses bounded key<TAB>value output; NULL when it is not preflight output. */
static json_object *parse_output(char *text) {
    json_object *snapshot = snapshot_new(); char *line, *save = NULL; size_t lines = 0;
    static const char *const required[] = {"os", "arch", "home", "path", "umask", NULL};
    for (line = strtok_r(text, "\n", &save); line; line = strtok_r(NULL, "\n", &save)) {
        char *tab = strchr(line, '\t');
        if (!tab || ++lines > 4096) continue;
        *tab = '\0';
        if (key_text(line) && value_text(tab + 1)) add_value(snapshot, line, tab + 1);
    }
    for (lines = 0; required[lines]; lines++)
        if (!f_string(snapshot, required[lines])) { json_object_put(snapshot); return NULL; }
    return snapshot;
}

/* ---- Requirements ---- */
struct requirement { const char *name, *status, *detail; bool blocking; };
static void require(json_object *rows, const struct requirement *r) {
    json_object *row = json_object_new_object();
    f_string_add(row, "name", r->name); f_string_add(row, "status", r->status);
    json_object_object_add(row, "blocking", json_object_new_boolean(r->blocking && !strcmp(r->status, "missing")));
    f_string_add(row, "detail", r->detail);
    json_object_array_add(rows, row);
}
static bool has_tool(json_object *snapshot, const char *tool) {
    return f_string(f_field(snapshot, "tools"), tool) != NULL;
}
static void require_tool(json_object *rows, const char *name, bool present, const char *missing) {
    struct requirement r = {name, present ? "ok" : "missing", present ? "found" : missing, true};
    require(rows, &r);
}
static bool supported_platform(const char *os, const char *arch) {
    return !strcmp(os, "Linux") && (!strcmp(arch, "x86_64") || !strcmp(arch, "amd64") || !strcmp(arch, "aarch64") || !strcmp(arch, "arm64"));
}
static void require_platform(json_object *rows, json_object *snapshot, bool binary) {
    const char *os = f_string(snapshot, "os"), *arch = f_string(snapshot, "arch");
    struct requirement r = {"platform", "ok", "Linux release binary available", true};
    if (!supported_platform(os, arch)) {
        r.status = binary ? "warning" : "missing";
        r.detail = binary ? "no pinned release binary for this platform; --binary will be installed unpinned"
                          : "no pinned release binary for this platform; rerun hydra remote setup with --binary FILE built for it";
    }
    require(rows, &r);
}
static void require_disk(json_object *rows, json_object *snapshot) {
    const char *text = f_string(snapshot, "free_kb"); char *end = NULL; long kb = text ? strtol(text, &end, 10) : -1;
    struct requirement r = {"disk", "ok", "at least 50 MB free in HOME", true};
    if (!text || !text[0] || (end && *end)) { r.status = "warning"; r.detail = "free space in HOME is unknown"; }
    else if (kb < PREFLIGHT_FREE_KB) { r.status = "missing"; r.detail = "less than 50 MB free in HOME"; }
    require(rows, &r);
}
static bool tmux_recent(const char *version) {
    const char *digit = version ? strpbrk(version, "0123456789") : NULL;
    return digit && strtol(digit, NULL, 10) >= 3;
}
static void require_warnings(json_object *rows, json_object *snapshot) {
    const char *umask_text = f_string(snapshot, "umask"); long mask = umask_text ? strtol(umask_text, NULL, 8) : 022;
    struct requirement tmux = {"tmux", "ok", "tmux 3.0 or newer", false};
    struct requirement curl = {"curl", "ok", "found", false};
    struct requirement umask_row = {"umask", "ok", umask_text ? umask_text : "", false};
    if (!has_tool(snapshot, "tmux") || !tmux_recent(f_string(snapshot, "tmux_version"))) {
        tmux.status = "warning"; tmux.detail = "tmux 3.0 or newer is needed to run heads (for example: sudo apt-get install tmux)";
    }
    if (!has_tool(snapshot, "curl")) { curl.status = "warning"; curl.detail = "curl is needed only for provider installers"; }
    if (!(mask & 020)) { umask_row.status = "warning"; umask_row.detail = "group-writable umask; see roadmap R1 before relying on shared state"; }
    require(rows, &tmux); require(rows, &curl); require(rows, &umask_row);
}
static json_object *requirements(json_object *snapshot, bool binary) {
    json_object *rows = json_object_new_array();
    bool home = f_string(snapshot, "home_writable") && !strcmp(f_string(snapshot, "home_writable"), "yes");
    require_platform(rows, snapshot, binary);
    require_tool(rows, "git", has_tool(snapshot, "git"), "install git (for example: sudo apt-get install git)");
    require_tool(rows, "sha256", has_tool(snapshot, "sha256sum") || has_tool(snapshot, "shasum"),
                 "install sha256sum or shasum (coreutils or perl)");
    require_tool(rows, "mktemp", has_tool(snapshot, "mktemp"), "install mktemp (coreutils)");
    require_tool(rows, "head_tail", has_tool(snapshot, "head") && has_tool(snapshot, "tail"), "install head and tail (coreutils)");
    require_tool(rows, "home", home, "HOME must be an existing directory you can write");
    require_disk(rows, snapshot);
    require_warnings(rows, snapshot);
    return rows;
}

/* ---- Existing Hydra ---- */
/* hydra_version reported by a strict handshake with hydra/home, or "". */
static void handshake_version(const struct setup_ctx *ctx, const char *hydra, const char *home, char version[64]) {
    struct f_remote remote = ctx->remote; json_object *reply; const char *reported;
    version[0] = '\0';
    if (f_copy(remote.hydra, sizeof(remote.hydra), hydra) || f_copy(remote.home, sizeof(remote.home), home)) return;
    reply = f_observe(&remote, "handshake", ctx->seconds);
    reported = f_string(f_field(reply, "data"), "hydra_version");
    if (json_object_get_boolean(f_field(reply, "ok")) && reported) f_copy(version, 64, reported);
    json_object_put(reply);
}
/* A same-version install that passes the strict handshake is reusable. */
static void existing_hydra(const struct setup_ctx *ctx, json_object *snapshot, json_object *rows) {
    json_object *hydra = f_field(snapshot, "hydra");
    const char *path = f_string(hydra, "path"), *version = f_string(hydra, "version");
    struct requirement r = {"hydra", "ok", "not installed; a private pinned install will be added", false};
    char reported[64] = "";
    if (path && version && !strcmp(version, "Hydra version " F_VERSION)) handshake_version(ctx, path, "", reported);
    if (!strcmp(reported, F_VERSION)) r.detail = "a compatible Hydra " F_VERSION " is installed and will be reused";
    else if (path) { r.status = "warning"; r.detail = "an existing Hydra install is kept untouched; a new pinned install will be added"; }
    json_object_object_add(hydra, "reusable", json_object_new_boolean(!strcmp(reported, F_VERSION)));
    require(rows, &r);
}
/* Upgrade mode: the install the alias uses today, checked with the alias's state directory. */
static void alias_hydra(const struct setup_ctx *ctx, json_object *upgrade, json_object *snapshot) {
    const char *path = f_string(upgrade, "hydra"); json_object *found; char reported[64];
    if (!path) return;
    handshake_version(ctx, path, ctx->remote.home, reported);
    found = json_object_new_object();
    f_string_add(found, "path", path);
    if (reported[0]) f_string_add(found, "version", reported);
    else json_object_object_add(found, "version", NULL);
    json_object_object_add(found, "reusable", json_object_new_boolean(!strcmp(reported, F_VERSION)));
    json_object_object_add(snapshot, "alias_hydra", found);
}

/* ---- Step ---- */
static json_object *missing_list(json_object *rows, char *message, size_t size) {
    json_object *missing = json_object_new_array(); size_t i, used = 0;
    message[0] = '\0';
    for (i = 0; i < json_object_array_length(rows); i++) {
        json_object *row = json_object_array_get_idx(rows, i);
        if (!json_object_get_boolean(f_field(row, "blocking"))) continue;
        json_object_array_add(missing, json_object_new_string(f_string(row, "name")));
        if (used < size) used += (size_t)snprintf(message + used, size - used, "%s%s: %s", used ? "; " : "", f_string(row, "name"), f_string(row, "detail"));
    }
    return missing;
}
static size_t count_status(json_object *rows, const char *status) {
    size_t i, count = 0;
    for (i = 0; i < json_object_array_length(rows); i++)
        if (!strcmp(f_string(json_object_array_get_idx(rows, i), "status"), status)) count++;
    return count;
}
static json_object *finish(struct setup_ctx *ctx, json_object *snapshot) {
    json_object *rows = f_field(snapshot, "requirements"), *missing, *copy = NULL, *data; char message[1024], summary[256];
    snprintf(summary, sizeof(summary), "%s %s; %zu warning(s)", f_string(snapshot, "os"), f_string(snapshot, "arch"), count_status(rows, "warning"));
    f_string_add(snapshot, "summary", summary);
    missing = missing_list(rows, message, sizeof(message));
    if (json_object_deep_copy(snapshot, &copy, NULL)) copy = NULL;
    if (!copy || setup_state_step(ctx, "preflight", json_object_array_length(missing) ? "blocked" : "done", snapshot)) {
        json_object_put(copy); json_object_put(missing);
        return setup_error(ctx, "state_unavailable", "cannot record the preflight result", NULL, NULL);
    }
    if (!json_object_array_length(missing)) { json_object_put(missing); return f_success(ctx->command, copy); }
    data = json_object_new_object();
    json_object_object_add(data, "missing", missing); json_object_object_add(data, "preflight", copy);
    return setup_error(ctx, "prerequisite_missing", message,
                       "fix the listed prerequisites on the remote host (Hydra never uses sudo), then rerun hydra remote preflight NAME", data);
}
json_object *setup_step_preflight(struct setup_ctx *ctx) {
    struct f_capture cap = {0}; json_object *snapshot;
    if (f_ssh(&ctx->remote, "exec /bin/sh -s", script, strlen(script), ctx->seconds + 30, false, &cap)) {
        f_capture_free(&cap);
        return setup_error(ctx, "transport_failed", "cannot start SSH", NULL, NULL);
    }
    if (cap.status) {
        json_object *error = setup_error(ctx, f_transport_code(&cap), "the preflight script did not complete over strict SSH",
                                         "trust the host key first (hydra remote trust-key NAME) and check SSH authentication", NULL);
        f_capture_free(&cap);
        return error;
    }
    snapshot = cap.out_bytes <= PREFLIGHT_LIMIT && cap.out ? parse_output(cap.out) : NULL;
    f_capture_free(&cap);
    if (!snapshot) return setup_error(ctx, "invalid_response", "the remote preflight output was missing, oversized or malformed", NULL, NULL);
    json_object_object_add(snapshot, "requirements", requirements(snapshot, ctx->binary != NULL));
    existing_hydra(ctx, snapshot, f_field(snapshot, "requirements"));
    alias_hydra(ctx, setup_upgrade(ctx), snapshot);
    return finish(ctx, snapshot);
}
