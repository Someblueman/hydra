#define _POSIX_C_SOURCE 200809L
#ifdef __APPLE__
#define _DARWIN_C_SOURCE
#endif
#include "internal.h"
/* This machine on Hosts: the tmux and Hydra it runs and the agents `hydra
 * agent list` finds, read when Hosts opens. The three reads run one after
 * another as children of the loop's tick (never on a key), each bounded;
 * one that fails leaves its value unknown. */
#define MACHINE_READ_BUDGET_MS 10000L
enum { READ_TMUX, READ_HYDRA, READ_AGENTS, READS };

void hosts_machine_request(struct app *app) {
    struct native_hosts *h = hosts_state(app);
    if (h && !app->fleet) h->machine.pending = true;
}

/* The first line of text, without its line break. */
static void first_line(const char *text, char *out, size_t size) {
    snprintf(out, size, "%.*s", (int)strcspn(text, "\r\n"), text);
}

/* "tmux 3.4" and "Hydra version 3.0.0" name their version last. */
static void version_of(const char *text, char *out, size_t size) {
    char line[160];
    const char *space;
    first_line(text, line, sizeof(line));
    space = strrchr(line, ' ');
    copy_text(out, size, space ? space + 1 : line);
}

/* PROFILE AVAILABLE ...: every profile but the plain shell, and whether its
 * executable was found. */
static void agents_of(struct hosts_machine *m, char *text) {
    char *line, *save = NULL;
    m->agent_count = 0;
    for (line = strtok_r(text, "\n", &save); line && m->agent_count < HOSTS_AGENTS; line = strtok_r(NULL, "\n", &save)) {
        char name[32], available[16];
        if (sscanf(line, "%31s %15s", name, available) != 2 || !strcmp(name, "PROFILE") || !strcmp(name, "none")) continue;
        copy_text(m->agents[m->agent_count].name, sizeof(m->agents[0].name), name);
        m->agents[m->agent_count++].available = !strcmp(available, "yes");
    }
    m->agents_read = true;
}

static void read_finish(struct hosts_machine *m) {
    size_t length;
    int status;
    char *text = setup_capture_finish(&m->job, &length, &status);
    if (text && !status) {
        if (m->step == READ_TMUX) version_of(text, m->tmux, sizeof(m->tmux));
        else if (m->step == READ_HYDRA) version_of(text, m->hydra, sizeof(m->hydra));
        else agents_of(m, text);
    }
    free(text);
    m->step++;
}

static void read_start(struct app *app, struct hosts_machine *m) {
    char *tmux[] = {(char *)"tmux", (char *)"-V", NULL};
    char *hydra[] = {(char *)app->hydra, (char *)"--version", NULL};
    char *agents[] = {(char *)app->hydra, (char *)"agent", (char *)"list", NULL};
    char **argv = m->step == READ_TMUX ? tmux : m->step == READ_HYDRA ? hydra : agents;
    if (!setup_capture_start(&m->job, argv, MACHINE_READ_BUDGET_MS)) m->step++;
}

void hosts_machine_tick(struct app *app) {
    struct hosts_machine *m = app->hosts ? &app->hosts->machine : NULL;
    if (!m) return;
    if (m->job.active) {
        if (!setup_capture_step(&m->job)) return;
        read_finish(m);
    } else if (m->pending) {
        m->pending = false;
        m->step = READ_TMUX;
    } else if (m->step >= READS) return;
    while (m->step < READS && !m->job.active) read_start(app, m);
    if (m->step >= READS) m->read = true;
}

void hosts_machine_stop(struct app *app) {
    struct hosts_machine *m = app->hosts ? &app->hosts->machine : NULL;
    if (!m || !m->job.active) return;
    /* Only reads run here; nothing is left half done by stopping one. */
    if (!m->job.reaped) {
        (void)kill(-m->job.pid, SIGKILL); (void)kill(m->job.pid, SIGKILL);
        while (waitpid(m->job.pid, NULL, 0) < 0 && errno == EINTR) { }
    }
    setup_capture_release(&m->job);
}

void hosts_machine_heads(const struct app *app, size_t *heads, size_t *running, size_t *gone) {
    size_t i;
    *heads = *running = *gone = 0;
    for (i = 0; i < app->model.head_count; i++) {
        const struct head *h = &app->model.heads[i];
        if (head_remote(h)) continue;
        (*heads)++;
        if (!strcmp(display_status(h), "LIVE")) (*running)++;
        if (head_terminal_gone(app, h)) (*gone)++;
    }
}

size_t hosts_machine_agents(const struct app *app) {
    const struct hosts_machine *m = app->hosts ? &app->hosts->machine : NULL;
    size_t i, count = 0;
    for (i = 0; m && i < m->agent_count; i++) count += m->agents[i].available;
    return count;
}
