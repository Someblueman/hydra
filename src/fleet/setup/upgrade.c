/* Upgrades (setup core): an enrolled alias without setup state is upgraded
 * through a new setup record (state.upgrade), and a setup whose provision step
 * recorded another Hydra version than this one offers a runtime upgrade. The
 * runtime upgrade is a view over the recorded provision step only: nothing
 * here contacts the remote or writes state, so `setup status` and `setup
 * list` can report it read-only. provision.c plans and installs it. */
#include "fleet/setup/setup.h"
#include "fleet/support/files.h"
#include "fleet/support/json.h"
#include <stdlib.h>
#include <string.h>

/* ---- Runtime upgrade ---- */
/* Numeric comparison of dotted versions: <0, 0 or >0; 0 also when either
 * part is not numeric (the caller then reports "different"). */
static int version_compare(const char *left, const char *right) {
    while (*left || *right) {
        char *left_end, *right_end;
        unsigned long a = strtoul(left, &left_end, 10), b = strtoul(right, &right_end, 10);
        if (left_end == left || right_end == right) return 0;
        if (a != b) return a < b ? -1 : 1;
        left = *left_end == '.' ? left_end + 1 : left_end;
        right = *right_end == '.' ? right_end + 1 : right_end;
        if ((*left && (*left < '0' || *left > '9')) || (*right && (*right < '0' || *right > '9'))) return 0;
    }
    return 0;
}
static const char *relation(const char *from) {
    int order = version_compare(from, F_VERSION);
    return order < 0 ? "older" : order > 0 ? "newer" : "different";
}
json_object *setup_runtime_upgrade(struct setup_ctx *ctx) {
    json_object *detail = setup_state_detail(ctx, "provision"), *flight = f_field(detail, "upgrade"), *view;
    const char *from, *hydra, *prefix, *how; char summary[256];
    if (f_string(detail, "hydra")) {
        /* A finished install record; older records may lack hydra_version. */
        from = f_string(detail, "hydra_version") ? f_string(detail, "hydra_version") : f_string(ctx->state, "hydra_version");
        hydra = f_string(detail, "hydra"); prefix = f_string(detail, "prefix");
    } else if (json_object_is_type(flight, json_type_object)) {
        /* An approved upgrade that has not finished (in progress, unknown or failed). */
        from = f_string(flight, "from_version"); hydra = f_string(flight, "previous_hydra"); prefix = f_string(flight, "previous_prefix");
    } else return NULL;
    if (!from || !from[0] || strlen(from) >= 64 || !strcmp(from, F_VERSION) || !hydra) return NULL;
    how = relation(from);
    view = json_object_new_object();
    f_string_add(view, "from_version", from); f_string_add(view, "to_version", F_VERSION);
    f_string_add(view, "relation", how);
    f_string_add(view, "previous_hydra", hydra);
    if (prefix) f_string_add(view, "previous_prefix", prefix);
    else json_object_object_add(view, "previous_prefix", NULL);
    if (!strcmp(how, "older")) snprintf(summary, sizeof(summary), "older Hydra (%s) on this host; upgrade to " F_VERSION, from);
    else snprintf(summary, sizeof(summary), "%s Hydra (%s) on this host; install " F_VERSION " beside it",
                  !strcmp(how, "newer") ? "newer" : "another", from);
    f_string_add(view, "summary", summary);
    return view;
}
static bool upgrade_offered(struct setup_ctx *ctx) {
    const char *status = setup_state_status(ctx, "provision");
    json_object *view;
    bool offered;
    if (strcmp(status, "done") && strcmp(status, "skipped")) return false;
    view = setup_runtime_upgrade(ctx);
    offered = view != NULL;
    json_object_put(view);
    return offered;
}
const char *setup_step_view_status(struct setup_ctx *ctx, const char *step) {
    if (!strcmp(step, "provision") && upgrade_offered(ctx)) return "upgrade_available";
    return setup_state_status(ctx, step);
}

/* ---- Enrolled alias without setup state ---- */
static json_object *upgrade_conflict(struct setup_ctx *ctx, const struct f_remote *alias) {
    char recovery[1024];
    snprintf(recovery, sizeof(recovery),
             "alias %s already points at %s and setup never re-points an alias; rerun hydra remote setup %s without DEST or "
             "--ssh-config to upgrade it, or choose another NAME", alias->name, alias->target, alias->name);
    return setup_error(ctx, "alias_conflict", "NAME is an existing alias with a different destination or SSH config", recovery, NULL);
}
static void seed_from_alias(struct f_remote *remote, const struct f_remote *alias) {
    f_copy(remote->home, sizeof(remote->home), alias->home);
    f_copy(remote->principal, sizeof(remote->principal), alias->principal);
    f_copy(remote->project, sizeof(remote->project), alias->project);
    f_copy(remote->accepted_host_key, sizeof(remote->accepted_host_key), alias->accepted_host_key);
    remote->multiplex = alias->multiplex;
}
json_object *setup_open_enrolled(struct setup_ctx *ctx, const char *dest, const char *config, const struct f_remote *alias) {
    json_object *result, *upgrade;
    if ((dest && strcmp(dest, alias->target)) || (config && strcmp(config, alias->ssh_config))) return upgrade_conflict(ctx, alias);
    result = setup_state_open(ctx, alias->name, alias->target, alias->ssh_config[0] ? alias->ssh_config : NULL, SETUP_CREATE);
    if (result) return result;
    seed_from_alias(&ctx->remote, alias);
    upgrade = json_object_new_object();
    f_string_add(upgrade, "hydra", alias->hydra); f_string_add(upgrade, "target", alias->target);
    if (setup_state_set(ctx, "upgrade", upgrade))
        return setup_error(ctx, "state_unavailable", "cannot record the upgrade in setup state", NULL, NULL);
    return NULL;
}
