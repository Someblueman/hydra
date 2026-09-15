#define _POSIX_C_SOURCE 200809L
#ifdef __APPLE__
#define _DARWIN_C_SOURCE
#endif
#include "internal.h"
extern char **environ;

/* Explicit shell CLI actions. Read-only inspections and removals run with their
 * output captured so the control centre stays on screen; interactive commands
 * such as switch or dashboard still take over the terminal. */

static int run_argv(struct app *app, char *const argv[]) {
    pid_t pid;
    int status = 0;
    restore_terminal(app);
    printf("\033[H\033[2J");
    fflush(stdout);
    if (posix_spawnp(&pid, app->hydra, NULL, NULL, argv, environ) != 0) {
        perror("hydra action");
        status = 1;
    } else if (waitpid(pid, &status, 0) < 0) {
        status = 1;
    }
    printf("\nPress Enter to return to Hydra...");
    fflush(stdout);
    while (getchar() != '\n' && !feof(stdin)) { }
    clearerr(stdin);
    if (enter_raw(app) != 0) {
        app->running = false;
        return -1;
    }
    return WIFEXITED(status) ? WEXITSTATUS(status) : 1;
}


void show_result(struct app *app, const char *title, const char *text) {
    copy_text(app->result_title, sizeof(app->result_title), title);
    copy_text(app->result_text, sizeof(app->result_text), text);
    app->result_scroll = 0;
    app->result_open = true;
    app->help = false;
}

void group_marked_action(struct app *app) {
    char group[TEXT] = "";
    char *argv[MAX_HEADS + 5U];
    size_t index, count = 0U;
    if (prompt_text(app, "Group for selected heads: ", group, sizeof(group)) != 0 || group[0] == '\0') return;
    argv[count++] = (char *)app->hydra;
    argv[count++] = (char *)"group";
    argv[count++] = (char *)"create";
    argv[count++] = group;
    for (index = 0U; index < app->marked_count; index++) argv[count++] = app->marked[index];
    argv[count] = NULL;
    if (run_argv(app, argv) == 0) app->marked_count = 0U;
}

static const char *first_line(const char *text, char *out, size_t size) {
    const char *end;
    while (*text == '\n' || *text == ' ') text++;
    end = strchr(text, '\n');
    snprintf(out, size, "%.*s", (int)(end ? end - text : (long)strlen(text)), text);
    return out;
}

struct removal {
    size_t count, removed, skipped, failed, dirty;
    char output[8192], transcript[8192];
};

/* Marked heads, or the selected head. Returns false with a notice when there is nothing to remove. */
static bool removal_targets(struct app *app, char targets[][TEXT], size_t *count) {
    struct head *selected = selected_head(app);
    size_t index;
    *count = 0U;
    if (app->marked_count) {
        for (index = 0U; index < app->marked_count && *count < MAX_HEADS; index++) copy_text(targets[(*count)++], TEXT, app->marked[index]);
        return true;
    }
    if (selected) { copy_text(targets[(*count)++], TEXT, selected->branch); return true; }
    copy_text(app->notice, sizeof(app->notice), "Select a head to remove, or mark several with Space");
    return false;
}

static void removal_names(char targets[][TEXT], size_t count, char *names, size_t size) {
    size_t index;
    names[0] = '\0';
    for (index = 0U; index < count && strlen(names) < 400U; index++)
        text_append(names, size, "%s%.100s", index ? ", " : "", targets[index]);
    if (index < count) text_append(names, size, ", +%zu more", count - index);
}

static bool removal_confirmed(struct app *app, size_t count, const char *names) {
    char question[1024], answer[TEXT] = "";
    snprintf(question, sizeof(question), "Remove %zu head%s (%s)? This closes the terminal and deletes the worktree; the branch is kept and heads with uncommitted changes are refused. y/N: ",
             count, count == 1U ? "" : "s", names);
    if (prompt_text(app, question, answer, sizeof(answer)) == 0 && (!strcasecmp(answer, "y") || !strcasecmp(answer, "yes"))) return true;
    copy_text(app->notice, sizeof(app->notice), "Removal cancelled; nothing changed");
    return false;
}

/* Remove one head through the shell CLI, recording the outcome. */
static void remove_one(struct app *app, char *target, size_t index, struct removal *r) {
    const struct head *head = head_for_branch(app, target);
    char *argv[] = {(char *)app->hydra, (char *)"kill", target, (char *)"--protect-untracked", NULL};
    int status;
    if (head != NULL && app->current_session[0] != '\0' && strcmp(head->session, app->current_session) == 0) {
        text_append(r->transcript, sizeof(r->transcript), "== %s\nSkipped: this is the terminal you are using right now.\n\n", target);
        r->skipped++;
        return;
    }
    snprintf(app->notice, sizeof(app->notice), "Removing %.200s (%zu of %zu)...", target, index + 1U, r->count);
    status = run_captured(app, argv, r->output, sizeof(r->output), 60000L);
    text_append(r->transcript, sizeof(r->transcript), "== %s (exit %d)\n%s\n", target, status, r->output);
    if (status == 0) { r->removed++; return; }
    r->failed++;
    if (strstr(r->output, "uncommitted") || strstr(r->output, "Refusing")) r->dirty++;
}

static void removal_single_notice(struct app *app, const char *target, const struct removal *r) {
    char line[TEXT];
    if (r->skipped) {
        snprintf(app->notice, sizeof(app->notice), "Skipped %.120s: this is the terminal you are using", target);
        return;
    }
    first_line(r->output, line, sizeof(line));
    snprintf(app->notice, sizeof(app->notice), "Removed %.120s%s%.100s", target, line[0] ? ": " : "", line);
}

static void removal_notice(struct app *app, char targets[][TEXT], const struct removal *r) {
    if (r->failed) {
        snprintf(app->notice, sizeof(app->notice), "Removed %zu of %zu; %zu unsuccessful; %zu skipped%s", r->removed, r->count, r->failed, r->skipped,
                 r->dirty ? " (uncommitted changes; see output)" : " (see output)");
        return;
    }
    if (r->count == 1U) {
        removal_single_notice(app, targets[0], r);
        return;
    }
    snprintf(app->notice, sizeof(app->notice), "Removed %zu head%s%s%s", r->removed, r->removed == 1U ? "" : "s",
             r->skipped ? "; skipped the current session" : "", r->skipped ? "" : "");
}

static void removal_report(struct app *app, char targets[][TEXT], const struct removal *r) {
    removal_notice(app, targets, r);
    if (r->failed) show_result(app, "REMOVAL OUTPUT", r->transcript);
    copy_text(app->result_text, sizeof(app->result_text), r->transcript);
    if (!r->failed) copy_text(app->result_title, sizeof(app->result_title), "REMOVAL OUTPUT");
}

/* Remove marked heads, or the selected head, after an in-app confirmation.
 * The shell CLI keeps its dirty-worktree refusal; the UI never adds --force. */
void remove_heads_action(struct app *app) {
    char targets[MAX_HEADS][TEXT], names[512];
    struct removal r;
    size_t index;
    memset(&r, 0, sizeof(r));
    refresh_current_session(app);
    if (!removal_targets(app, targets, &r.count)) return;
    removal_names(targets, r.count, names, sizeof(names));
    if (!removal_confirmed(app, r.count, names)) return;
    for (index = 0U; index < r.count; index++) remove_one(app, targets[index], index, &r);
    app->marked_count = 0U;
    removal_report(app, targets, &r);
    native_observations_tick(app, true);
    retarget_selection(app);
}

void kill_marked_action(struct app *app) { remove_heads_action(app); }

/* Run the recorded inspection command for the selected recovery finding. */
void recovery_check_action(struct app *app) {
    const struct recovery *item;
    char command[TEXT], *argv[16], *save = NULL, *word, output[8192], title[TEXT + 64], detail[1024];
    size_t count = 0U;
    if (app->recovery_selected >= app->model.recovery_count) return;
    item = &app->model.recovery[app->recovery_selected];
    copy_text(command, sizeof(command), item->action);
    word = strtok_r(command, " ", &save);
    if (!word) { copy_text(app->notice, sizeof(app->notice), "No check is recorded for this finding"); return; }
    argv[count++] = (char *)app->hydra;
    if (strcmp(word, "hydra") != 0) argv[count++] = word;
    while ((word = strtok_r(NULL, " ", &save)) != NULL && count < 15U) argv[count++] = word;
    argv[count] = NULL;
    recovery_explain(item, title, sizeof(title), detail, sizeof(detail));
    snprintf(app->notice, sizeof(app->notice), "Running %s...", item->action);
    (void)run_captured(app, argv, output, sizeof(output), 30000L);
    snprintf(app->notice, sizeof(app->notice), "Check finished: %s", item->action);
    show_result(app, title, output[0] ? output : "The check produced no output.");
}

/* Order preserves first-substring-match selection after exact and prefix
 * matches. Arguments are literal argv entries. */
enum palette_scope { ACTION_GLOBAL, ACTION_HEAD, ACTION_COMPARE, ACTION_SPAWN, ACTION_REMOVE };
struct palette_action {
    const char *name;
    const char *command;
    const char *subcommand;
    enum palette_scope scope;
};

static const struct palette_action palette[] = {
    {"switch", "switch", NULL, ACTION_HEAD},
    {"kill", "kill", NULL, ACTION_REMOVE},
    {"remove", "kill", NULL, ACTION_REMOVE},
    {"regenerate", "regenerate", NULL, ACTION_GLOBAL},
    {"spawn", "spawn", NULL, ACTION_SPAWN},
    {"new task", "spawn", NULL, ACTION_SPAWN},
    {"status", "status", NULL, ACTION_GLOBAL},
    {"claims", "claim", "list", ACTION_GLOBAL},
    {"collisions", "collision", NULL, ACTION_COMPARE},
    {"scopes", "scope", "show", ACTION_HEAD},
    {"queue", "queue", NULL, ACTION_GLOBAL},
    {"resources", "resource", "status", ACTION_HEAD},
    {"git diff", "diff", NULL, ACTION_HEAD},
    {"approvals", "gate", "status", ACTION_HEAD},
    {"recovery inspect", "doctor", NULL, ACTION_GLOBAL},
    {"dashboard", "dashboard", NULL, ACTION_GLOBAL}
};

static const struct palette_action *match_palette(const char *query) {
    size_t index, total = sizeof(palette) / sizeof(palette[0]);
    for (index = 0U; index < total; index++) if (!strcmp(palette[index].name, query)) return &palette[index];
    for (index = 0U; index < total; index++) if (!strncmp(palette[index].name, query, strlen(query))) return &palette[index];
    for (index = 0U; index < total; index++) if (strstr(palette[index].name, query) != NULL) return &palette[index];
    return NULL;
}

void execute_palette(struct app *app, const char *query) {
    size_t count = 0U;
    const struct palette_action *chosen;
    struct head *head = selected_head(app);
    char other[TEXT] = "";
    char *argv[6]; /* executable, command, subcommand, head, comparison, NULL */
    if (query[0] == '\0') {
        copy_text(app->notice, sizeof(app->notice), "action search canceled");
        return;
    }
    chosen = match_palette(query);
    if (chosen == NULL) { copy_text(app->notice, sizeof(app->notice), "no explicit local action matched"); return; }
    if (chosen->scope == ACTION_SPAWN) { new_task_action(app); return; }
    if (chosen->scope == ACTION_REMOVE) { remove_heads_action(app); return; }
    argv[count++] = (char *)app->hydra;
    argv[count++] = (char *)chosen->command;
    if (chosen->subcommand != NULL) argv[count++] = (char *)chosen->subcommand;
    if (chosen->scope == ACTION_HEAD || chosen->scope == ACTION_COMPARE) {
        if (head == NULL) { copy_text(app->notice, sizeof(app->notice), "select a head for that action"); return; }
        argv[count++] = head->branch;
    }
    if (chosen->scope == ACTION_COMPARE) {
        if (prompt_text(app, "Compare with head: ", other, sizeof(other)) != 0 || other[0] == '\0') return;
        argv[count++] = other;
    }
    argv[count] = NULL;
    (void)run_argv(app, argv);
}

/* Fleet cancellation carries host, project and observed instance to the public CLI. */
void fleet_action(struct app *app) {
    struct head *head = selected_head(app);
    char confirm[TEXT] = "";
    char *argv[12];
    if (head == NULL || head->remote_host[0] == '\0') return;
    if (prompt_text(app, "Interrupt remote head? Type yes: ", confirm, sizeof(confirm)) != 0 || strcmp(confirm, "yes") != 0) return;
    argv[0] = (char *)app->hydra; argv[1] = (char *)"fleet";
    argv[2] = (char *)"cancel"; argv[3] = head->remote_host;
    argv[4] = (char *)"--project"; argv[5] = head->remote_project;
    argv[6] = (char *)"--instance"; argv[7] = head->instance;
    argv[8] = (char *)"--"; argv[9] = head->remote_branch; argv[10] = NULL;
    (void)run_argv(app, argv);
}
