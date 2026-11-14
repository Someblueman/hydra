#define _XOPEN_SOURCE 700
#include "fleet/setup/setup.h"
#include "fleet/setup/assets.h"
#include "fleet/support/files.h"
#include "fleet/support/json.h"
#include "fleet/support/process.h"
#include "fleet/transport/bundle.h"
#include <stdlib.h>
#include <string.h>

/*
 * Approved provisioning (design §5). The plan binds the remote platform, the
 * exact local version, the helper's digest and source (pinned release asset
 * or unpinned --binary/local helper), the package digest and the exact prefix
 * HOME/.local/share/hydra/fleet/DIGEST from the preflight snapshot. Nothing on
 * the remote changes before approval; in_progress is persisted before
 * f_bootstrap; a resumed in_progress/outcome_unknown step only reconciles.
 * Existing installs and pins are never modified, and PATH is never changed.
 * A runtime upgrade (setup_runtime_upgrade) takes the same path: the plan adds
 * the old and new version and install, the previous install stays, and once
 * the new one confirms its exact version verify and the alias switch follow.
 */
#define PROVISION_SECONDS 120U
#define PROVISION_CHANGES "installs only into prefix; no PATH changes; other installs and pins untouched"

struct provision {
    struct f_platform platform;
    struct setup_binary binary;
    char platform_text[F_PLATFORM_TEXT], home[F_PATH], package[F_PATH], digest[65], prefix[F_PATH], plan_sha256[65];
};

static unsigned install_seconds(const struct setup_ctx *ctx) {
    return ctx->seconds > PROVISION_SECONDS ? ctx->seconds : PROVISION_SECONDS;
}
static json_object *state_error(const struct setup_ctx *ctx) {
    return setup_error(ctx, "state_unavailable", "cannot record setup progress", "check $HYDRA_HOME/fleet/setup ownership and permissions, then rerun", NULL);
}
/* Records done with detail (taken) and returns a separate copy as data. */
static json_object *finished(struct setup_ctx *ctx, json_object *detail) {
    json_object *copy = NULL;
    if (json_object_deep_copy(detail, &copy, NULL)) copy = NULL;
    if (!copy || setup_state_step(ctx, "provision", "done", detail)) { json_object_put(copy); return state_error(ctx); }
    return f_success(ctx->command, copy);
}
static json_object *copy_detail(struct setup_ctx *ctx) {
    json_object *copy = NULL, *detail = setup_state_detail(ctx, "provision");
    if (!detail || json_object_deep_copy(detail, &copy, NULL)) return json_object_new_object();
    return copy;
}

/* ---- Exact version of the remote install ---- */
static bool handshake_version(struct setup_ctx *ctx) {
    json_object *reply = f_observe(&ctx->remote, "handshake", ctx->seconds);
    const char *version = f_string(f_field(reply, "data"), "hydra_version");
    bool ok = json_object_get_boolean(f_field(reply, "ok")) && version && !strcmp(version, F_VERSION);
    json_object_put(reply);
    return ok;
}
static bool shell_version(struct setup_ctx *ctx) {
    char command[F_PATH + 64], *quoted = f_quote(ctx->remote.hydra); struct f_capture cap = {0}; bool ok = false;
    if (quoted && snprintf(command, sizeof(command), "%s --version", quoted) < (int)sizeof(command) &&
        !f_ssh(&ctx->remote, command, NULL, 0, ctx->seconds, false, &cap))
        ok = !cap.status && cap.out && !strcmp(cap.out, "Hydra version " F_VERSION "\n");
    free(quoted); f_capture_free(&cap);
    return ok;
}
static bool exact_version(struct setup_ctx *ctx) {
    return ctx->remote.hydra[0] == '/' && handshake_version(ctx) && shell_version(ctx);
}

/* ---- Preflight snapshot ---- */
static json_object *read_preflight(struct setup_ctx *ctx, struct provision *p) {
    json_object *detail = setup_state_detail(ctx, "preflight");
    const char *home = f_string(detail, "home");
    if (strcmp(setup_state_status(ctx, "preflight"), "done") || !home || home[0] != '/' || f_copy(p->home, sizeof(p->home), home))
        return setup_error(ctx, "prerequisite_missing", "provisioning needs a completed preflight snapshot",
                           "run hydra remote preflight NAME first", NULL);
    if (f_platform_set(&p->platform, f_string(detail, "os"), f_string(detail, "arch")))
        return setup_error(ctx, "platform_unsupported", "the remote is not linux or darwin on x86_64 or aarch64",
                           "install Hydra on this remote manually (hydra fleet package and hydra fleet bootstrap), then add it with hydra remote add", NULL);
    f_platform_text(&p->platform, p->platform_text);
    return NULL;
}
static json_object *reused_detail(const char *summary, const char *hydra, const char *how) {
    json_object *detail = json_object_new_object();
    f_string_add(detail, "summary", summary); f_string_add(detail, "hydra", hydra);
    f_string_add(detail, "hydra_version", F_VERSION);
    json_object_object_add(detail, "reused", json_object_new_boolean(true));
    f_string_add(detail, "reuse", how);
    return detail;
}
/* Preflight marks a same-version install that passed its handshake as
 * reusable; its raw `hydra --version` line is accepted as well. */
static bool reuse_candidate(json_object *hydra) {
    const char *version = f_string(hydra, "version");
    if (json_object_get_boolean(f_field(hydra, "reusable"))) return true;
    return version && (!strcmp(version, F_VERSION) || !strcmp(version, "Hydra version " F_VERSION));
}
/* An existing same-version Hydra that answers the exact version checks is
 * used as is: nothing on the remote changes, so no approval is needed. */
static json_object *reuse_existing(struct setup_ctx *ctx) {
    json_object *hydra = f_field(setup_state_detail(ctx, "preflight"), "hydra");
    const char *path = f_string(hydra, "path"); char saved[F_PATH], summary[F_PATH + 64];
    if (!path || path[0] != '/' || !reuse_candidate(hydra)) return NULL;
    memcpy(saved, ctx->remote.hydra, sizeof(saved));
    if (f_copy(ctx->remote.hydra, sizeof(ctx->remote.hydra), path) || !exact_version(ctx)) {
        memcpy(ctx->remote.hydra, saved, sizeof(saved));
        return NULL;
    }
    snprintf(summary, sizeof(summary), "reusing Hydra %s at %s", F_VERSION, path);
    return finished(ctx, reused_detail(summary, path, "existing"));
}

/* ---- Package ---- */
static int local_root(char root[F_PATH]) {
    const char *bin = getenv("HYDRA_BIN_DIR"); char parent[F_PATH];
    return !bin || bin[0] != '/' || f_path(parent, sizeof(parent), bin, "..") || !realpath(parent, root) ? -1 : 0;
}
static json_object *build_package(const struct setup_ctx *ctx, struct provision *p) {
    char root[F_PATH], dir[F_PATH], fleet[F_PATH]; json_object *result; const char *text;
    if (local_root(root)) return setup_error(ctx, "prerequisite_missing", "cannot locate this Hydra installation to package it", "run the command through bin/hydra so HYDRA_BIN_DIR is set", NULL);
    result = f_package_installation(root, p->binary.path, &p->platform);
    if (!json_object_get_boolean(f_field(result, "ok"))) {
        json_object *error = setup_error(ctx, f_string(f_field(result, "error"), "code"), f_string(f_field(result, "error"), "message"), NULL, NULL);
        json_object_put(result); return error;
    }
    text = json_object_to_json_string_ext(f_field(result, "data"), JSON_C_TO_STRING_PLAIN);
    if (setup_private_dir("packages", dir) || setup_cache_store(dir, ".json", text, strlen(text), p->digest, p->package) ||
        f_path(fleet, sizeof(fleet), p->home, ".local/share/hydra/fleet") || f_path(p->prefix, sizeof(p->prefix), fleet, p->digest)) {
        json_object_put(result);
        return setup_error(ctx, "state_unavailable", "cannot store the install package privately", "check $HYDRA_HOME/fleet ownership and permissions", NULL);
    }
    json_object_put(result);
    return NULL;
}
static bool listed_pin(struct setup_ctx *ctx, const char *digest) {
    json_object *pins = f_field(setup_state_detail(ctx, "preflight"), "pins"); size_t i;
    for (i = 0; json_object_is_type(pins, json_type_array) && i < json_object_array_length(pins); i++) {
        json_object *pin = json_object_array_get_idx(pins, i);
        const char *text = f_text(pin) ? f_text(pin) : f_string(pin, "path");
        size_t n = text ? strlen(text) : 0;
        if (n >= 64 && !strcmp(text + n - 64, digest)) return true;
    }
    return false;
}
/* An identical package already pinned at the exact prefix is adopted after a
 * read-only install-check. */
static json_object *reuse_pin(struct setup_ctx *ctx, const struct provision *p) {
    json_object *reply; char summary[F_PATH + 64];
    if (!listed_pin(ctx, p->digest)) return NULL;
    reply = f_bootstrap_reconcile(&ctx->remote, p->package, p->digest, p->prefix, ctx->seconds);
    if (!json_object_get_boolean(f_field(reply, "ok")) || !exact_version(ctx)) { json_object_put(reply); return NULL; }
    json_object_put(reply);
    snprintf(summary, sizeof(summary), "reusing pinned Hydra %s at %s", F_VERSION, p->prefix);
    return finished(ctx, reused_detail(summary, ctx->remote.hydra, "pin"));
}

/* ---- Runtime upgrade ---- */
static bool upgrading(struct setup_ctx *ctx) {
    json_object *view = setup_runtime_upgrade(ctx);
    bool found = view != NULL;
    json_object_put(view);
    return found;
}
/* The plan's "upgrade" member, or NULL for a first install. The alias switch
 * is planned only while the alias still uses the previous install. */
static json_object *upgrade_json(struct setup_ctx *ctx, const struct provision *p) {
    json_object *view = setup_runtime_upgrade(ctx), *upgrade, *change; struct f_remote alias; char hydra[F_PATH];
    const char *previous = f_string(view, "previous_hydra");
    if (!view) return NULL;
    upgrade = json_object_new_object();
    f_string_add(upgrade, "from_version", f_string(view, "from_version")); f_string_add(upgrade, "to_version", F_VERSION);
    f_string_add(upgrade, "previous_hydra", previous);
    json_object_object_add(upgrade, "previous_prefix", json_object_get(f_field(view, "previous_prefix")));
    f_string_add(upgrade, "previous_install", "stays installed and is not removed; delete it yourself once no alias uses it");
    if (!f_remote_load(ctx->name, &alias) && !strcmp(alias.hydra, previous) && !strcmp(alias.target, ctx->remote.target) &&
        !strcmp(alias.ssh_config, ctx->remote.ssh_config) && !f_path(hydra, sizeof(hydra), p->prefix, "bin/hydra")) {
        change = json_object_new_object();
        f_string_add(change, "name", ctx->name); f_string_add(change, "old_hydra", previous); f_string_add(change, "new_hydra", hydra);
        f_string_add(change, "when", "after the new install answers the fleet handshake with exactly " F_VERSION "; no other alias field changes");
        json_object_object_add(upgrade, "alias", change);
    }
    json_object_put(view);
    return upgrade;
}
/* Before the upgraded install is recorded done: verify and alias of a setup
 * that had finished them run again (an interruption leaves provision in
 * progress, and its reconciliation reopens them again). */
static int reopen(struct setup_ctx *ctx) {
    static const char *const steps[][2] = {
        {"verify", "re-verify the upgraded Hydra " F_VERSION}, {"alias", "switch the alias to the upgraded Hydra " F_VERSION}
    };
    size_t i;
    for (i = 0; i < sizeof(steps) / sizeof(steps[0]); i++) {
        const char *status = setup_state_status(ctx, steps[i][0]);
        json_object *detail;
        if (strcmp(status, "done") && strcmp(status, "skipped")) continue;
        detail = json_object_new_object();
        f_string_add(detail, "summary", steps[i][1]);
        if (setup_state_step(ctx, steps[i][0], "pending", detail)) return -1;
    }
    return 0;
}
/* After an upgraded install (and on a rerun after an interruption): re-verify
 * and switch the alias when those are the next steps. */
static json_object *finish_upgrade(struct setup_ctx *ctx, json_object *result) {
    char step[80]; int round; json_object *next;
    if (!json_object_get_boolean(f_field(result, "ok")) || strcmp(setup_state_status(ctx, "provision"), "done") ||
        !f_field(setup_state_detail(ctx, "provision"), "upgrade")) return result;
    for (round = 0; round < 2 && setup_first_open(ctx, step) && (!strcmp(step, "verify") || !strcmp(step, "alias")); round++) {
        next = !strcmp(step, "verify") ? setup_step_verify(ctx) : setup_step_alias(ctx);
        if (!json_object_get_boolean(f_field(next, "ok"))) { json_object_put(result); return next; }
        json_object_put(next);
    }
    return result;
}

/* ---- Plan, approval and installation ---- */
static json_object *binary_json(const struct setup_binary *binary) {
    json_object *value = json_object_new_object();
    f_string_add(value, "sha256", binary->sha256); f_string_add(value, "source", binary->source);
    f_string_add(value, "name", binary->name);
    f_string_add(value, "trust", binary->pinned ? "pinned" : "unpinned");
    return value;
}
static json_object *plan_json(struct setup_ctx *ctx, const struct provision *p) {
    json_object *plan = json_object_new_object(), *upgrade = upgrade_json(ctx, p);
    f_string_add(plan, "host", ctx->remote.target);
    f_string_add(plan, "platform", p->platform_text);
    f_string_add(plan, "hydra_version", F_VERSION);
    json_object_object_add(plan, "binary", binary_json(&p->binary));
    f_string_add(plan, "package_sha256", p->digest);
    f_string_add(plan, "prefix", p->prefix);
    f_string_add(plan, "changes", PROVISION_CHANGES);
    if (upgrade) json_object_object_add(plan, "upgrade", upgrade);
    return plan;
}
static json_object *gate(struct setup_ctx *ctx, json_object *plan, const char *binary, const char *approve) {
    const char *with_binary[] = {"provision", ctx->name, "--binary", binary, NULL}, *plain[] = {"provision", ctx->name, NULL};
    return setup_plan_gate(ctx, "provision", plan, approve, binary ? with_binary : plain);
}
static json_object *progress_detail(const struct provision *p, json_object *plan) {
    json_object *detail = json_object_new_object(), *upgrade = NULL; char summary[F_PATH + 64];
    snprintf(summary, sizeof(summary), "installing Hydra %s into %s", F_VERSION, p->prefix);
    f_string_add(detail, "summary", summary);
    f_string_add(detail, "package", p->package); f_string_add(detail, "package_sha256", p->digest);
    f_string_add(detail, "prefix", p->prefix); f_string_add(detail, "platform", p->platform_text);
    f_string_add(detail, "plan_sha256", p->plan_sha256);
    json_object_object_add(detail, "binary", binary_json(&p->binary));
    if (f_field(plan, "upgrade") && !json_object_deep_copy(f_field(plan, "upgrade"), &upgrade, NULL)) json_object_object_add(detail, "upgrade", upgrade);
    json_object_object_add(detail, "plan", plan);
    return detail;
}
/* Confirms the exact version of a confirmed install and records done. */
static json_object *complete(struct setup_ctx *ctx) {
    json_object *detail, *upgrade; char summary[F_PATH * 2 + 96]; const char *previous; int used;
    if (!exact_version(ctx)) {
        if (setup_state_step(ctx, "provision", "outcome_unknown", NULL)) return state_error(ctx);
        return setup_error(ctx, "outcome_unknown", "the installed Hydra did not confirm the exact local version",
                           "check connectivity, then rerun hydra remote provision NAME; it reconciles the recorded prefix and never installs twice", NULL);
    }
    detail = copy_detail(ctx);
    json_object_object_del(detail, "plan");
    upgrade = f_field(detail, "upgrade");
    if (upgrade && reopen(ctx)) { json_object_put(detail); return state_error(ctx); }
    previous = f_string(upgrade, "previous_prefix") ? f_string(upgrade, "previous_prefix") : f_string(upgrade, "previous_hydra");
    used = snprintf(summary, sizeof(summary), "Hydra %s installed at %s", F_VERSION, f_string(detail, "prefix") ? f_string(detail, "prefix") : ctx->remote.hydra);
    if (upgrade && used > 0 && (size_t)used < sizeof(summary))
        snprintf(summary + used, sizeof(summary) - (size_t)used, "; Hydra %s stays at %s", f_string(upgrade, "from_version"), previous ? previous : "its path");
    f_string_add(detail, "summary", summary); f_string_add(detail, "hydra", ctx->remote.hydra);
    f_string_add(detail, "hydra_version", F_VERSION);
    return finished(ctx, detail);
}
static json_object *outcome(struct setup_ctx *ctx, json_object *reply) {
    const char *code = f_string(f_field(reply, "error"), "code"); json_object *error;
    if (json_object_get_boolean(f_field(reply, "ok"))) { json_object_put(reply); return complete(ctx); }
    if (!code || !strcmp(code, "outcome_unknown")) {
        json_object_put(reply);
        if (setup_state_step(ctx, "provision", "outcome_unknown", NULL)) return state_error(ctx);
        return setup_error(ctx, "outcome_unknown", "the install may have run but its result was lost",
                           "rerun hydra remote provision NAME; it reconciles the recorded prefix and never installs twice", NULL);
    }
    /* A definitive installer answer: nothing was installed at the prefix. */
    error = setup_error(ctx, code, f_string(f_field(reply, "error"), "message"),
                        !strcmp(code, "platform_mismatch") ? "rerun hydra remote preflight NAME, then provision a helper for the reported platform"
                                                          : "inspect the error, then rerun hydra remote provision NAME to review a fresh plan", NULL);
    json_object_put(reply);
    if (!setup_state_step(ctx, "provision", "failed", NULL)) return error;
    json_object_put(error);
    return state_error(ctx);
}
static json_object *install(struct setup_ctx *ctx, struct provision *p, json_object *plan) {
    if (setup_plan_hash(ctx, "provision", plan, NULL, p->plan_sha256)) { json_object_put(plan); return state_error(ctx); }
    /* Persisted before any remote side effect; a resume only reconciles. */
    if (setup_state_step(ctx, "provision", "in_progress", progress_detail(p, plan))) return state_error(ctx);
    return outcome(ctx, f_bootstrap(&ctx->remote, p->package, p->digest, p->prefix, install_seconds(ctx)));
}
static json_object *plan_and_install(struct setup_ctx *ctx, const char *binary, const char *approve) {
    struct provision p; json_object *result, *plan;
    /* An upgrade always installs (an existing identical prefix is verified,
     * never overwritten), so its alias switch and re-verification follow. */
    bool reuse = !binary && !upgrading(ctx);
    memset(&p, 0, sizeof(p));
    if ((result = read_preflight(ctx, &p))) return result;
    if (reuse && (result = reuse_existing(ctx))) return result;
    if ((result = setup_asset_resolve(ctx, binary, &p.platform, &p.binary)) || (result = build_package(ctx, &p))) return result;
    if (reuse && (result = reuse_pin(ctx, &p))) return result;
    plan = plan_json(ctx, &p);
    if ((result = gate(ctx, plan, binary, approve))) { json_object_put(plan); return result; }
    return install(ctx, &p, plan);
}

/* ---- Resume ---- */
static bool package_present(const char *package, const char *digest) {
    char actual[65];
    return !f_hash(package, actual) && !strcmp(actual, digest);
}
/* The private package cache lost the recorded package: rebuild it from this
 * Hydra and the recorded helper source. Packaging is deterministic, so only
 * the same Hydra and helper reproduce the recorded digest; any other result
 * is ignored and reconciliation stays unconfirmed. package is updated. */
static void restore_package(struct setup_ctx *ctx, const char *binary, char package[F_PATH], const char *digest) {
    const char *source = f_string(f_field(setup_state_detail(ctx, "provision"), "binary"), "source");
    struct provision p; json_object *error;
    if (package_present(package, digest) || (!binary && source && !strcmp(source, "binary"))) return;
    memset(&p, 0, sizeof(p));
    error = read_preflight(ctx, &p);
    if (!error) error = setup_asset_resolve(ctx, binary, &p.platform, &p.binary);
    if (!error) error = build_package(ctx, &p);
    if (!error && !strcmp(p.digest, digest)) (void)f_copy(package, F_PATH, p.package);
    json_object_put(error);
}
/* 1 when the remote definitively has nothing at prefix, 0 otherwise. */
static int prefix_absent(struct setup_ctx *ctx, const char *prefix) {
    char command[F_PATH + 96], *quoted = f_quote(prefix); struct f_capture cap = {0}; int absent = 0;
    if (quoted && snprintf(command, sizeof(command), "if test -e %s; then echo present; else echo absent; fi", quoted) < (int)sizeof(command) &&
        !f_ssh(&ctx->remote, command, NULL, 0, ctx->seconds, false, &cap))
        absent = !cap.status && cap.out && !strcmp(cap.out, "absent\n");
    free(quoted); f_capture_free(&cap);
    return absent;
}
static json_object *unconfirmed(struct setup_ctx *ctx, const char *package, const char *digest, const char *prefix) {
    json_object *data;
    if (setup_state_step(ctx, "provision", "outcome_unknown", NULL)) return state_error(ctx);
    if (package_present(package, digest))
        return setup_error(ctx, "outcome_unknown", "cannot confirm the interrupted install; nothing was retried",
                           "check connectivity and the recorded prefix on the remote, then rerun hydra remote provision NAME to reconcile again", NULL);
    data = json_object_new_object();
    f_string_add(data, "prefix", prefix); f_string_add(data, "package_sha256", digest);
    return setup_error(ctx, "outcome_unknown",
                       "cannot confirm the interrupted install: its local package is gone and this Hydra cannot rebuild it; nothing was retried",
                       "rerun hydra remote provision NAME with the Hydra (and any --binary FILE) that planned it; otherwise remove data.prefix on "
                       "the remote once no alias uses it and rerun hydra remote provision NAME, which then finds nothing there and plans afresh", data);
}
static json_object *reconcile(struct setup_ctx *ctx, const char *binary) {
    json_object *detail = setup_state_detail(ctx, "provision"), *reply;
    const char *package = f_string(detail, "package"), *digest = f_string(detail, "package_sha256"), *prefix = f_string(detail, "prefix");
    char saved[3][F_PATH];
    if (!package || !digest || !prefix || f_copy(saved[0], F_PATH, package) || f_copy(saved[1], F_PATH, digest) || f_copy(saved[2], F_PATH, prefix))
        return setup_error(ctx, "state_invalid", "the interrupted provisioning record lacks its package or prefix",
                           "preserve $HYDRA_HOME/fleet/setup/NAME.json for inspection", NULL);
    restore_package(ctx, binary, saved[0], saved[1]);
    reply = f_bootstrap_reconcile(&ctx->remote, saved[0], saved[1], saved[2], ctx->seconds);
    if (json_object_get_boolean(f_field(reply, "ok"))) { json_object_put(reply); return complete(ctx); }
    json_object_put(reply);
    if (prefix_absent(ctx, saved[2])) {
        if (setup_state_step(ctx, "provision", "failed", NULL)) return state_error(ctx);
        return setup_error(ctx, "install_failed", "the interrupted install left nothing at the recorded prefix",
                           "rerun hydra remote provision NAME to review and approve a fresh plan", NULL);
    }
    return unconfirmed(ctx, saved[0], saved[1], saved[2]);
}

json_object *setup_step_provision(struct setup_ctx *ctx, const char *binary, const char *approve) {
    const char *status = setup_state_status(ctx, "provision");
    json_object *result;
    if (!strcmp(status, "in_progress") || !strcmp(status, "outcome_unknown")) result = reconcile(ctx, binary);
    else if (!strcmp(status, "done") && !upgrading(ctx)) result = f_success(ctx->command, copy_detail(ctx));
    else result = plan_and_install(ctx, binary, approve);
    return finish_upgrade(ctx, result);
}
