#define _POSIX_C_SOURCE 200809L
#ifdef __APPLE__
#define _DARWIN_C_SOURCE
#endif
#include "internal.h"
extern char **environ;

/* Explicit shell CLI actions. Read-only inspections and removals run with their
 * output captured so the control centre stays on screen; interactive commands
 * such as switch or dashboard still take over the terminal. */

/* Runs a command that needs the terminal: the control centre steps aside,
 * the command's output stays up until Enter, then the screen is repainted.
 * Only switch and dashboard still do this; everything else is captured. */
static int run_argv(struct app *app, char *const argv[]) {
    struct terminal_loan loan;
    pid_t pid;
    int status = 0;
    if (!terminal_lend(app, &loan, false)) return -1;
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
    if (!terminal_return(app, &loan)) return -1;
    return WIFEXITED(status) ? WEXITSTATUS(status) : 1;
}


void show_result(struct app *app, const char *title, const char *text) {
    copy_text(app->result_title, sizeof(app->result_title), title);
    copy_text(app->result_text, sizeof(app->result_text), text);
    app->result_scroll = 0;
    app->result_open = true;
    app->help = false;
}

/* An action that did not finish is kept for the session with its output, so
 * it can be read again after the notice is gone (oldest dropped first). */
void action_failed(struct app *app, const char *title, const char *output) {
    struct failed_action *f;
    if (app->failed_count == FAILED_ACTIONS) {
        memmove(&app->failed[0], &app->failed[1], sizeof(app->failed[0]) * (FAILED_ACTIONS - 1U));
        app->failed_count--;
    }
    f = &app->failed[app->failed_count++];
    copy_text(f->title, sizeof(f->title), title);
    copy_text(f->output, sizeof(f->output), output && output[0] ? output : "The action produced no output.");
    f->at = time(NULL);
}

/* The output of the actions that failed this session, newest first. */
void actions_failed_show(struct app *app) {
    char text[8192] = "";
    size_t i;
    if (!app->failed_count) { toast(app, KT_TOAST_INFO, "No action has failed in this session"); return; }
    for (i = app->failed_count; i-- > 0;)
        text_append(text, sizeof(text), "== %s\n%s\n\n", app->failed[i].title, app->failed[i].output);
    show_result(app, "Your failed actions", text);
}

/* A short outcome over the frame; it never takes input. Without the loop
 * (deterministic rendering, direct calls) it is the status-line notice. */
void toast(struct app *app, enum kt_toast_kind kind, const char *text) {
    if (!app->cmds || !app->program) { copy_text(app->notice, sizeof(app->notice), text); return; }
    if (!app->toasts_ready) { kt_toasts_init(&app->toasts, TOAST_TAG); app->toasts.width = 60; app->toasts_ready = true; }
    kt_toasts_push(&app->toasts, app->cmds, kind, text);
    app->notice[0] = '\0';
}

static bool group_name(struct app *app, char *group, size_t size) {
    struct kt_textinput input;
    struct kt_field field;
    char body[160];
    kt_textinput_init(&input, group, size);
    memset(&field, 0, sizeof(field));
    field.kind = KT_FIELD_TEXT; field.label = "Group name"; field.input = &input;
    snprintf(body, sizeof(body), "Puts the %zu marked head%s in one group, so they can be followed together.",
             app->marked_count, app->marked_count == 1U ? "" : "s");
    return dialog_form_body(app, "Group marked heads", body, &field, 1) == 0 && group[0];
}

void group_marked_action(struct app *app) {
    char group[TEXT] = "", output[8192];
    char *argv[MAX_HEADS + 5U];
    size_t index, count = 0U;
    if (!group_name(app, group, sizeof(group))) return;
    argv[count++] = (char *)app->hydra;
    argv[count++] = (char *)"group";
    argv[count++] = (char *)"create";
    argv[count++] = group;
    for (index = 0U; index < app->marked_count; index++) argv[count++] = app->marked[index];
    argv[count] = NULL;
    if (run_captured(app, argv, output, sizeof(output), 30000L) == 0) {
        app->marked_count = 0U;
        toast(app, KT_TOAST_SUCCESS, "Grouped the marked heads");
        return;
    }
    action_failed(app, "Grouping the marked heads did not finish", output);
    show_result(app, "Grouping did not finish", output[0] ? output : "The command produced no output.");
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
    bool gone[MAX_HEADS];
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
    if (selected && head_remote(selected)) {
        copy_text(app->notice, sizeof(app->notice), "Remote heads are removed on their host; x interrupts the agent instead");
        return false;
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
    char body[1536] = "";
    if (count == 1U)
        text_append(body, sizeof(body), "Remove %s?\n\nCloses its terminal and deletes its worktree. The branch %s is "
                    "kept. A head with uncommitted or untracked changes is refused, never forced.", names, names);
    else
        text_append(body, sizeof(body), "Remove %zu heads (%s)?\n\nCloses their terminals and deletes their worktrees. "
                    "The branches are kept. A head with uncommitted or untracked changes is refused, never forced.", count, names);
    if (dialog_confirm(app, count == 1U ? "Remove head" : "Remove heads", body, "Remove", true)) return true;
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
    if (status == 0) { r->removed++; r->gone[index] = true; return; }
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
    if (r->failed) action_failed(app, app->notice, r->transcript);
    else if (app->notice[0]) { char done[TEXT]; copy_text(done, sizeof(done), app->notice); toast(app, KT_TOAST_SUCCESS, done); }
    if (r->failed) show_result(app, "REMOVAL OUTPUT", r->transcript);
    copy_text(app->result_text, sizeof(app->result_text), r->transcript);
    if (!r->failed) copy_text(app->result_title, sizeof(app->result_title), "REMOVAL OUTPUT");
}

/* When the selected head was removed, focus lands on the nearest head that
 * survives (after it, else before it) at once; the next snapshot drops the
 * removed rows. A refused or skipped head keeps its selection. */
static bool removed_target(const char *branch, char targets[][TEXT], const struct removal *r) {
    size_t i;
    for (i = 0U; i < r->count; i++) if (r->gone[i] && !strcmp(targets[i], branch)) return true;
    return false;
}

static void select_survivor(struct app *app, char targets[][TEXT], const struct removal *r) {
    size_t i, from = app->selected;
    if (app->run_row || app->task_row || from >= app->model.head_count ||
        !removed_target(app->model.heads[from].branch, targets, r)) return;
    for (i = from; i < app->model.head_count; i++)
        if (!removed_target(app->model.heads[i].branch, targets, r) && head_visible(app, &app->model.heads[i])) { app->selected = i; return; }
    for (i = from; i-- > 0U;)
        if (!removed_target(app->model.heads[i].branch, targets, r) && head_visible(app, &app->model.heads[i])) { app->selected = i; return; }
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
    select_survivor(app, targets, &r);
    retarget_selection(app);
}

/* x removes the selected or marked heads; Inbox removes leftover worktrees itself. */
void remove_key_action(struct app *app) { remove_heads_action(app); }

/* The command palette (:): every binding that applies where the user is,
 * then the CLI commands below, searched by name as one types (kettle's
 * palette). Read-only commands run captured and open in the output viewer;
 * only switch and dashboard need the terminal, and their labels say so. */
enum palette_scope { ACTION_GLOBAL, ACTION_HEAD, ACTION_COMPARE, ACTION_SPAWN, ACTION_REMOVE, ACTION_IMPORT,
                     ACTION_FAILED, ACTION_THEME, ACTION_LEAVE, ACTION_LEAVE_HEAD, ACTION_INTERRUPT, ACTION_LAND };
struct palette_command {
    const char *name, *label, *command, *subcommand;
    enum palette_scope scope;
};

static const struct palette_command palette_commands[] = {
    {"diff", "Show diff of the selected head", "diff", NULL, ACTION_HEAD},
    {"claims", "Claims", "claim", "list", ACTION_GLOBAL},
    {"scopes", "Scopes of the selected head", "scope", "show", ACTION_HEAD},
    {"queue", "Queue", "queue", NULL, ACTION_GLOBAL},
    {"resources", "Resources of the selected head", "resource", "status", ACTION_HEAD},
    {"approvals", "Approval gates of the selected head", "gate", "status", ACTION_HEAD},
    {"collisions", "Collisions with another head", "collision", NULL, ACTION_COMPARE},
    {"status", "Status", "status", NULL, ACTION_GLOBAL},
    {"doctor", "Recovery inspection (doctor)", "doctor", NULL, ACTION_GLOBAL},
    {"regenerate", "Regenerate", "regenerate", NULL, ACTION_GLOBAL},
    {"spawn", "New task (spawn)", NULL, NULL, ACTION_SPAWN},
    {"kill", "Remove heads (kill)", NULL, NULL, ACTION_REMOVE},
    {"import", "Import plan files", NULL, NULL, ACTION_IMPORT},
    {"land", "Land worker: merge the verified result of the selected head's run", NULL, NULL, ACTION_LAND},
    {"failed", "Your failed actions", NULL, NULL, ACTION_FAILED},
    {"theme", "Theme: terminal, dark, light", NULL, NULL, ACTION_THEME},
    {"switch", "Switch to the selected head (leaves Hydra)", "switch", NULL, ACTION_LEAVE_HEAD},
    {"dashboard", "Dashboard (leaves Hydra)", "dashboard", NULL, ACTION_LEAVE},
    {"interrupt", "Interrupt remote head", NULL, NULL, ACTION_INTERRUPT}
};
#define PALETTE_COMMANDS (sizeof(palette_commands) / sizeof(palette_commands[0]))
#define PALETTE_DIALOG 3U

static void palette_theme(struct app *app) {
    char text[64];
    app->theme = (app->theme + 1) % 3;
    snprintf(text, sizeof(text), "Theme: %s%s", theme_name(app->theme), app->no_color ? " (NO_COLOR)" : "");
    toast(app, KT_TOAST_INFO, text);
}

/* Runs a CLI command with its output captured into the viewer. */
static void palette_capture(struct app *app, const struct palette_command *chosen, char *const argv[]) {
    char output[8192], title[TEXT];
    int status;
    snprintf(app->notice, sizeof(app->notice), "Running %s...", chosen->name);
    status = run_captured(app, argv, output, sizeof(output), 30000L);
    app->notice[0] = '\0';
    snprintf(title, sizeof(title), status ? "%s (exit %d)" : "%s", chosen->label, status);
    if (status) action_failed(app, title, output);
    show_result(app, title, output[0] ? output : "The command produced no output.");
}

static bool palette_local(struct app *app, const struct palette_command *chosen) {
    switch (chosen->scope) {
    case ACTION_SPAWN: new_task_action(app); return true;
    case ACTION_REMOVE: remove_heads_action(app); return true;
    case ACTION_IMPORT: plan_action_import(app); return true;
    case ACTION_FAILED: actions_failed_show(app); return true;
    case ACTION_THEME: palette_theme(app); return true;
    case ACTION_LAND: work_land(app); return true;
    default: return false;
    }
}

/* A remote head's own commands run on its host: removing it interrupts its
 * agent there, and head commands are refused here. True when handled. */
static bool palette_remote(struct app *app, const struct palette_command *chosen, const struct head *head, bool head_needed) {
    bool remote = head && head_remote(head);
    if (chosen->scope == ACTION_INTERRUPT || (chosen->scope == ACTION_REMOVE && remote)) {
        if (remote) fleet_action(app);
        else copy_text(app->notice, sizeof(app->notice), "Select a remote head to interrupt");
        return true;
    }
    if (!head_needed || !remote) return false;
    copy_text(app->notice, sizeof(app->notice), "That command runs on the head's host; a attaches, x interrupts");
    return true;
}

static void palette_command(struct app *app, const struct palette_command *chosen) {
    struct head *head = selected_head(app);
    char other[TEXT] = "";
    char *argv[6]; /* executable, command, subcommand, head, comparison, NULL */
    size_t count = 0U;
    bool head_needed = chosen->scope == ACTION_HEAD || chosen->scope == ACTION_COMPARE || chosen->scope == ACTION_LEAVE_HEAD;
    if (palette_remote(app, chosen, head, head_needed) || palette_local(app, chosen)) return;
    if (head_needed && head == NULL) { copy_text(app->notice, sizeof(app->notice), "Select a head for that command"); return; }
    argv[count++] = (char *)app->hydra;
    argv[count++] = (char *)chosen->command;
    if (chosen->subcommand != NULL) argv[count++] = (char *)chosen->subcommand;
    if (head_needed) argv[count++] = head->branch;
    if (chosen->scope == ACTION_COMPARE) {
        if (prompt_text(app, "Compare with head: ", other, sizeof(other)) != 0 || other[0] == '\0') return;
        argv[count++] = other;
    }
    argv[count] = NULL;
    if (chosen->scope == ACTION_LEAVE || chosen->scope == ACTION_LEAVE_HEAD) (void)run_argv(app, argv);
    else palette_capture(app, chosen, argv);
}

static bool palette_final(int result) { return result == KT_PALETTE_CHOSEN || result == KT_PALETTE_CANCELLED; }

void palette_open(struct app *app) {
    static struct kt_binding commands[PALETTE_COMMANDS];
    struct kt_binding rows[KEY_ACTIONS];
    struct kt_help_source sources[2];
    struct kt_palette palette;
    struct kt_modal modal;
    const struct kt_binding *chosen;
    size_t i;
    uint32_t chain = key_applying(app, rows);
    rows[KEY_PALETTE].scope = 0;
    for (i = 0; i < PALETTE_COMMANDS; i++) {
        memset(&commands[i], 0, sizeof(commands[i]));
        commands[i].name = palette_commands[i].name; commands[i].label = palette_commands[i].label;
        commands[i].action = KEY_ACTIONS + (uint32_t)i; commands[i].scope = 1U;
    }
    sources[0] = (struct kt_help_source){rows, KEY_ACTIONS, chain};
    sources[1] = (struct kt_help_source){commands, PALETTE_COMMANDS, 1U};
    kt_palette_init(&palette, "Commands");
    (void)kt_palette_set(&palette, sources, 2);
    modal = kt_palette_modal(&palette, PALETTE_DIALOG, app->cols - 4 < 72 ? app->cols - 4 : 72);
    if (dialog_run(app, &modal, palette_final) != KT_PALETTE_CHOSEN || !(chosen = kt_palette_chosen(&palette))) return;
    if (chosen->action < KEY_ACTIONS) key_perform(app, (enum key_action)chosen->action);
    else palette_command(app, &palette_commands[chosen->action - KEY_ACTIONS]);
}

/* Fleet interruption carries host, project and observed instance to the
 * public CLI; only a fresh observation of an answering host authorizes it. */
void fleet_action(struct app *app) {
    struct head *head = selected_head(app);
    char body[512], refusal[TEXT];
    char *argv[12];
    if (head == NULL || head->remote_host[0] == '\0') return;
    if (remote_action_refusal(app, head, refusal, sizeof(refusal))) { copy_text(app->notice, sizeof(app->notice), refusal); return; }
    snprintf(body, sizeof(body), "Interrupt %.100s on %.60s?\n\nThis sends Ctrl-C to its agent; the worktree and its "
             "files stay on the host.", head->remote_branch[0] ? head->remote_branch : head->branch, head->remote_host);
    if (!dialog_confirm(app, "Interrupt remote head", body, "Interrupt", true)) {
        copy_text(app->notice, sizeof(app->notice), "Interrupt cancelled; nothing was sent");
        return;
    }
    argv[0] = (char *)app->hydra; argv[1] = (char *)"fleet";
    argv[2] = (char *)"cancel"; argv[3] = head->remote_host;
    argv[4] = (char *)"--project"; argv[5] = head->remote_project;
    argv[6] = (char *)"--instance"; argv[7] = head->instance;
    argv[8] = (char *)"--"; argv[9] = head->remote_branch; argv[10] = NULL;
    {
        char output[4096];
        if (run_captured(app, argv, output, sizeof(output), 30000L) == 0) { toast(app, KT_TOAST_SUCCESS, "Interrupt sent to the remote head"); return; }
        action_failed(app, "Interrupting a remote head did not finish", output);
        show_result(app, "Interrupt did not finish", output[0] ? output : "The command produced no output.");
    }
}
