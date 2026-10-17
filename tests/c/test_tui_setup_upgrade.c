/* Runtime upgrade of a finished remote setup in the native control centre:
 * the recorded `hydra remote setup status|list --json` and `hydra remote
 * provision --json` envelopes of a host that an older Hydra set up, the Hosts
 * row that offers the upgrade, and the next command the TUI may run. */
#define _POSIX_C_SOURCE 200809L
#ifdef __APPLE__
#define _DARWIN_C_SOURCE
#endif
#include "../../src/tui/internal.h"
#include "../../src/tui/setup_flow.h"

static int failures;
static const char *fixtures;

static void check(bool ok, const char *name) {
    printf("[%s] %s\n", ok ? "PASS" : "FAIL", name);
    if (!ok) failures++;
}

static bool load(const char *name, struct setup_envelope *e, int exit_status) {
    char path[4096], *text = malloc(1024 * 1024);
    size_t length;
    FILE *file;
    bool ok;
    snprintf(path, sizeof(path), "%s/%s", fixtures, name);
    if (!text || !(file = fopen(path, "r"))) { free(text); return false; }
    length = fread(text, 1, 1024 * 1024 - 1, file);
    fclose(file);
    while (length && (text[length - 1] == '\n' || text[length - 1] == '\r')) length--;
    ok = !setup_envelope_parse(e, text, length, exit_status);
    free(text);
    return ok;
}

static bool plan_row(const struct setup_envelope *e, const char *key, const char *value) {
    size_t i;
    for (i = 0; i < e->plan_count; i++)
        if (!strcmp(e->plan[i].key, key)) return !value || strstr(e->plan[i].value, value) != NULL;
    return false;
}

static void envelope_cases(struct setup_envelope *e) {
    char summary[160] = "";
    const struct setup_step *open;
    bool listed = load("list-upgrade.json", e, 0) && e->listed_count == 1 && !e->listed[0].complete &&
        !strcmp(e->listed[0].status, "upgrade_available") && !strcmp(e->listed[0].next_step, "provision");
    if (listed) setup_listed_summary(&e->listed[0], summary, sizeof(summary));
    check(listed && !strcmp(summary, "older Hydra (2.9.0) on this host; upgrade to 3.0.0"), "a finished setup on an older Hydra lists its upgrade");
    check(load("list-done.json", e, 0) && !e->listed[0].upgrade[0], "a current finished setup offers no upgrade");
    check(load("status-upgrade.json", e, 0) && (open = setup_open_step(e)) && !strcmp(open->id, "provision") &&
          !strcmp(open->status, "upgrade_available") && setup_screen_for(e) == SETUP_SCREEN_STEPS && !strcmp(e->next_step, "provision"),
          "status opens the provision step for the upgrade");
    setup_summary(e, summary, sizeof(summary));
    check(!strcmp(summary, "Install Hydra: upgrade available") && !strcmp(setup_status_label("upgrade_available"), "upgrade available") &&
          strcmp(setup_status_mark("upgrade_available", false), " "), "the upgrade has its own status label and mark");
    check(setup_argv_valid(e, "ovh") && e->argc == 5 && !strcmp(e->argv[2], "provision") && !setup_argv_valid(e, "other"),
          "the TUI runs only hydra remote provision for this host");
    check(load("provision-upgrade-approval.json", e, 3) && setup_screen_for(e) == SETUP_SCREEN_PLAN && !strcmp(e->plan_kind, "provision") &&
          plan_row(e, "upgrade.from_version", "2.9.0") && plan_row(e, "upgrade.to_version", "3.0.0") &&
          plan_row(e, "upgrade.previous_prefix", "0029") && plan_row(e, "upgrade.alias.new_hydra", "/bin/hydra") &&
          plan_row(e, "upgrade.previous_install", "stays installed"), "the upgrade plan shows both versions, the kept prefix and the alias switch");
    check(setup_argv_valid(e, "ovh") && !strcmp(e->argv[4], "--approve") && !strcmp(e->argv[5], e->plan_sha256),
          "approving the upgrade reruns provision with exactly the reviewed hash");
    copy_text(e->plan_sha256, sizeof(e->plan_sha256), "0000000000000000000000000000000000000000000000000000000000000000");
    check(!setup_argv_valid(e, "ovh"), "a changed upgrade plan is not approved");
}

/* An observed host whose setup offers an upgrade keeps a setup row (Enter
 * runs the CLI flow) that is neither edited nor removed from Hosts. */
static void hosts_cases(struct app *app, struct setup_envelope *e) {
    struct native_setup *s = app->setup;
    struct host_row rows[8];
    size_t count, record = 99, i, setup_rows = 0;
    app->model.host_count = 1;
    copy_text(app->model.hosts[0].name, sizeof(app->model.hosts[0].name), "ovh");
    s->record_count = 1;
    memset(&s->records[0], 0, sizeof(s->records[0]));
    copy_text(s->records[0].name, sizeof(s->records[0].name), "ovh");
    check(load("list-upgrade.json", e, 0), "list-upgrade.json");
    setup_listed_summary(&e->listed[0], s->records[0].summary, sizeof(s->records[0].summary));
    s->records[0].complete = e->listed[0].complete; s->records[0].upgrade = e->listed[0].upgrade[0] != '\0';
    s->records[0].remote_changed = true;
    count = hosts_rows(app, rows, 8);
    for (i = 0; i < count; i++) if (rows[i].kind == HOST_ROW_SETUP) { setup_rows++; app->host_selected = i; }
    check(setup_rows == 1, "the upgrade row is listed beside the observed host");
    check(!hosts_setup_selected(app, &record), "e and x do not apply to the upgrade row");
    s->records[0].complete = true; s->records[0].upgrade = false;
    count = hosts_rows(app, rows, 8);
    for (i = 0, setup_rows = 0; i < count; i++) setup_rows += rows[i].kind == HOST_ROW_SETUP;
    check(setup_rows == 0, "a current finished setup is listed once, as its host");
}

int main(int argc, char **argv) {
    struct app *app = calloc(1, sizeof(*app));
    struct setup_envelope *e = calloc(1, sizeof(*e));
    if (argc != 2 || !app || !e) { free(app); free(e); fputs("usage: test-tui-setup-upgrade FIXTURES\n", stderr); return 2; }
    fixtures = argv[1];
    envelope_cases(e);
    app->hydra = "hydra"; app->view = 6; app->no_color = true;
    if (!native_setup_state(app)) { free(app); free(e); return 1; }
    hosts_cases(app, e);
    native_setup_destroy(app);
    free(app); free(e);
    printf("%s: %d failure%s\n", failures ? "FAIL" : "PASS", failures, failures == 1 ? "" : "s");
    return failures ? 1 : 0;
}
