#define _POSIX_C_SOURCE 200809L
#include "actions.h"
#include "terminal.h"
#include "selection.h"
#include "text.h"
#include <errno.h>
#include <spawn.h>
#include <string.h>
#include <sys/wait.h>
extern char **environ;

/* Explicit shell CLI actions exposed by the native palette. */

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
    printf("\nPress Enter to return to Mission Control...");
    fflush(stdout);
    while (getchar() != '\n' && !feof(stdin)) { }
    clearerr(stdin);
    if (enter_raw(app) != 0) {
        app->running = false;
        return -1;
    }
    return WIFEXITED(status) ? WEXITSTATUS(status) : 1;
}

static int spawn_argv(char *const argv[]) {
    pid_t pid;
    int status = 0;
    if (posix_spawnp(&pid, argv[0], NULL, NULL, argv, environ) != 0) {
        perror("hydra action");
        return 1;
    }
    while (waitpid(pid, &status, 0) < 0) {
        if (errno != EINTR) return 1;
    }
    return WIFEXITED(status) ? WEXITSTATUS(status) : 1;
}

int prompt_text(struct app *app, const char *prompt, char *buffer, size_t size) {
    restore_terminal(app);
    printf("\n%s", prompt);
    fflush(stdout);
    if (fgets(buffer, (int)size, stdin) == NULL) buffer[0] = '\0';
    buffer[strcspn(buffer, "\r\n")] = '\0';
    if (enter_raw(app) != 0) {
        app->running = false;
        return -1;
    }
    return 0;
}

static void spawn_action(struct app *app) {
    char branch[TEXT] = "", profile[TEXT] = "", template[TEXT] = "", layout[TEXT] = "";
    char *argv[11];
    size_t count = 0U;
    if (prompt_text(app, "Branch to spawn: ", branch, sizeof(branch)) != 0 || branch[0] == '\0') return;
    if (prompt_text(app, "Profile (blank=project default, none=no agent): ", profile, sizeof(profile)) != 0) return;
    if (prompt_text(app, "Template (optional): ", template, sizeof(template)) != 0) return;
    if (prompt_text(app, "Layout (blank=default, dev, full): ", layout, sizeof(layout)) != 0) return;
    argv[count++] = (char *)app->hydra;
    argv[count++] = (char *)"spawn";
    argv[count++] = branch;
    if (strcmp(profile, "none") == 0) {
        argv[count++] = (char *)"--no-agent";
    } else if (profile[0] != '\0') {
        argv[count++] = (char *)"--profile";
        argv[count++] = profile;
    }
    if (template[0] != '\0') {
        argv[count++] = (char *)"--template";
        argv[count++] = template;
    }
    if (layout[0] != '\0') {
        argv[count++] = (char *)"--layout";
        argv[count++] = layout;
    }
    argv[count] = NULL;
    (void)run_argv(app, argv);
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

void kill_marked_action(struct app *app) {
    size_t index, killed = 0U, skipped = 0U;
    int failed = 0;
    restore_terminal(app);
    printf("\033[H\033[2J");
    fflush(stdout);
    for (index = 0U; index < app->marked_count; index++) {
        const struct head *head = head_for_branch(app, app->marked[index]);
        char *argv[] = {(char *)app->hydra, (char *)"kill", app->marked[index], NULL};
        if (head != NULL && app->current_session[0] != '\0' &&
            strcmp(head->session, app->current_session) == 0) {
            printf("Skipping current session: %s\n", head->branch);
            skipped++;
            continue;
        }
        if (spawn_argv(argv) == 0) killed++;
        else failed = 1;
    }
    printf("\nBulk kill complete: %zu command(s), %zu current skipped%s\n",
           killed, skipped, failed ? ", failures reported above" : "");
    printf("Press Enter to return to Mission Control...");
    fflush(stdout);
    while (getchar() != '\n' && !feof(stdin)) { }
    clearerr(stdin);
    app->marked_count = 0U;
    if (enter_raw(app) != 0) app->running = false;
}

enum palette_scope { ACTION_GLOBAL, ACTION_HEAD, ACTION_COMPARE, ACTION_SPAWN };
struct palette_action {
    const char *name;
    const char *command;
    const char *subcommand;
    enum palette_scope scope;
};
/* Order preserves first-substring-match selection. Arguments are literal argv
 * entries; only the two interactive actions require additional prompting. */
static const struct palette_action palette[] = {
    {"switch", "switch", NULL, ACTION_HEAD},
    {"kill", "kill", NULL, ACTION_HEAD},
    {"regenerate", "regenerate", NULL, ACTION_GLOBAL},
    {"spawn", "spawn", NULL, ACTION_SPAWN},
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

void execute_palette(struct app *app, const char *query) {
    size_t index, count = 0U;
    const struct palette_action *chosen = NULL;
    struct head *head = selected_head(app);
    char other[TEXT] = "";
    char *argv[6]; /* executable, command, subcommand, head, comparison, NULL */
    if (query[0] == '\0') {
        copy_text(app->notice, sizeof(app->notice), "action search canceled");
        return;
    }
    for (index = 0U; index < sizeof(palette) / sizeof(palette[0]); index++) {
        if (strstr(palette[index].name, query) != NULL) { chosen = &palette[index]; break; }
    }
    if (chosen == NULL) { copy_text(app->notice, sizeof(app->notice), "no explicit local action matched"); return; }
    if (chosen->scope == ACTION_SPAWN) { spawn_action(app); return; }
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

/* Fleet actions carry host, project and observed instance to the public CLI. */
void fleet_action(struct app *app, bool attach) {
    struct head *head = selected_head(app);
    char confirm[TEXT] = "";
    char *argv[12];
    if (head == NULL || head->remote_host[0] == '\0') return;
    if (!attach && (prompt_text(app, "Interrupt remote head? Type yes: ", confirm, sizeof(confirm)) != 0 || strcmp(confirm, "yes") != 0)) return;
    argv[0] = (char *)app->hydra; argv[1] = (char *)"fleet";
    argv[2] = (char *)(attach ? "attach" : "cancel"); argv[3] = head->remote_host;
    argv[4] = (char *)"--project"; argv[5] = head->remote_project;
    argv[6] = (char *)"--instance"; argv[7] = head->instance;
    argv[8] = (char *)"--"; argv[9] = head->remote_branch; argv[10] = NULL;
    (void)run_argv(app, argv);
}
