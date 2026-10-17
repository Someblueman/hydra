#define _POSIX_C_SOURCE 200809L
#ifdef __APPLE__
#define _DARWIN_C_SOURCE
#endif
#include "internal.h"
/* One workspace across local and remote execution (U11). The composed model
 * is built from two observations that arrive separately: the local snapshot
 * (`hydra tui --data`) and the remote one (`hydra tui --remote-data`, the
 * fleet stream). Remote rows carry their host's connectivity and observation
 * age; when a host stops answering its rows are kept as the last observation,
 * marked retained, and never authorize an action. */

bool head_remote(const struct head *h) {
    return h != NULL && h->remote_host[0] != '\0';
}

const struct host_observation *host_for_name(const struct app *app, const char *name) {
    size_t i;
    if (!name || !name[0]) return NULL;
    for (i = 0; i < app->model.host_count; i++) if (!strcmp(app->model.hosts[i].name, name)) return &app->model.hosts[i];
    return NULL;
}

static struct remote_memory *remote_memory(struct app *app, const char *host, bool create) {
    size_t i;
    for (i = 0; i < app->remote_host_count; i++) if (!strcmp(app->remote_hosts[i].host, host)) return &app->remote_hosts[i];
    if (!create || app->remote_host_count >= sizeof(app->remote_hosts) / sizeof(app->remote_hosts[0])) return NULL;
    memset(&app->remote_hosts[app->remote_host_count], 0, sizeof(app->remote_hosts[0]));
    copy_text(app->remote_hosts[app->remote_host_count].host, sizeof(app->remote_hosts[0].host), host);
    return &app->remote_hosts[app->remote_host_count++];
}

static void age_text(time_t now, time_t good_at, char *out, size_t size) {
    if (!good_at || now < good_at) { copy_text(out, size, "-"); return; }
    snprintf(out, size, "%lld", (long long)(now - good_at));
}

static bool host_has_rows(const struct model *m, const char *host) {
    size_t i;
    for (i = 0; i < m->head_count; i++) if (!strcmp(m->heads[i].remote_host, host)) return true;
    for (i = 0; i < m->task_count; i++) if (!strcmp(m->tasks[i].host, host)) return true;
    return false;
}

/* The rows of a host that no longer answers are the previous observation's,
 * kept so ongoing work neither vanishes nor looks finished. */
static void retain_host(struct app *app, struct model *next, const struct model *prev, struct host_observation *host, time_t now) {
    const struct remote_memory *memory = remote_memory(app, host->name, false);
    const char *freshness = memory && memory->good_at ? "stale" : "unknown";
    char age[40];
    size_t i, retained = 0;
    age_text(now, memory ? memory->good_at : 0, age, sizeof(age));
    if (prev && !host_has_rows(next, host->name)) {
        for (i = 0; i < prev->head_count && next->head_count < MAX_HEADS; i++) {
            struct head *h;
            if (strcmp(prev->heads[i].remote_host, host->name)) continue;
            h = &next->heads[next->head_count++];
            *h = prev->heads[i];
            h->retained = true;
            copy_text(h->freshness, sizeof(h->freshness), freshness);
            copy_text(h->age, sizeof(h->age), age);
            retained++;
        }
        for (i = 0; i < prev->task_count && next->task_count < MAX_TASKS; i++) {
            struct task_observation *t;
            if (strcmp(prev->tasks[i].host, host->name)) continue;
            t = &next->tasks[next->task_count++];
            *t = prev->tasks[i];
            t->retained = true;
            copy_text(t->freshness, sizeof(t->freshness), freshness);
            retained++;
        }
        host->heads = (unsigned)retained;
    }
    host->retained = retained > 0;
    if (!strcmp(host->freshness, "unknown") && memory && memory->good_at) {
        copy_text(host->freshness, sizeof(host->freshness), "stale");
        copy_text(host->age, sizeof(host->age), age);
    }
}

/* Heads of a host that answered carry that observation's freshness and age. */
static void label_host_rows(struct model *next, const struct host_observation *host) {
    size_t i;
    for (i = 0; i < next->head_count; i++) {
        struct head *h = &next->heads[i];
        if (h->retained || strcmp(h->remote_host, host->name)) continue;
        copy_text(h->freshness, sizeof(h->freshness), host->freshness);
        copy_text(h->age, sizeof(h->age), host->age);
    }
}

/* Takes ownership of next, a fleet-stream model. */
static void remote_accept(struct app *app, struct model *next) {
    time_t now = time(NULL);
    size_t i;
    for (i = 0; i < next->host_count; i++) {
        struct host_observation *host = &next->hosts[i];
        if (!strcmp(host->state, "responded")) {
            struct remote_memory *memory = remote_memory(app, host->name, true);
            if (memory) memory->good_at = now;
        } else retain_host(app, next, app->remote, host, now);
        label_host_rows(next, host);
    }
    free(app->remote);
    app->remote = next;
    app->remote_at = now;
    app->remote_stale = false;
    app->remote_known = true;
    app->remote_error[0] = '\0';
}

/* Takes ownership of next, a local-stream model. */
static void local_accept(struct app *app, struct model *next) {
    free(app->local);
    app->local = next;
}

static void compose_recovery(struct model *m, const struct model *part) {
    size_t i;
    for (i = 0; part && i < part->recovery_count && m->recovery_count < MAX_RECOVERY; i++)
        m->recovery[m->recovery_count++] = part->recovery[i];
}

/* Local heads first, then remote heads; hosts and tasks are remote only. */
static void compose_model(struct app *app) {
    struct model *m = &app->model;
    const struct model *local = app->local, *remote = app->remote;
    size_t i;
    m->protocol = MODEL_UNION;
    m->head_count = m->recovery_count = m->host_count = m->task_count = 0;
    for (i = 0; local && i < local->head_count && m->head_count < MAX_HEADS; i++) m->heads[m->head_count++] = local->heads[i];
    for (i = 0; remote && i < remote->head_count && m->head_count < MAX_HEADS; i++) m->heads[m->head_count++] = remote->heads[i];
    compose_recovery(m, local);
    compose_recovery(m, remote);
    if (remote) {
        for (i = 0; i < remote->host_count; i++) m->hosts[m->host_count++] = remote->hosts[i];
        for (i = 0; i < remote->task_count; i++) m->tasks[m->task_count++] = remote->tasks[i];
    }
}

static bool same_head(const struct head *a, const struct head *b) {
    return (a->head_id[0] ? !strcmp(a->head_id, b->head_id) : !strcmp(a->branch, b->branch)) &&
        !strcmp(a->remote_host, b->remote_host) && !strcmp(a->remote_project, b->remote_project);
}

/* Selection follows identity, not position, across a replaced model. */
static void retarget_after_accept(struct app *app, const struct head *previous, bool had_head,
                                  const struct task_observation *task, bool had_task) {
    size_t i;
    if (had_head) {
        for (i = 0; i < app->model.head_count; i++) if (same_head(previous, &app->model.heads[i])) { app->selected = i; break; }
        if (i == app->model.head_count) app->selected = app->model.head_count;
    }
    if (had_task) {
        for (i = 0; i < app->model.task_count; i++)
            if (!strcmp(task->host, app->model.tasks[i].host) && !strcmp(task->task_id, app->model.tasks[i].task_id)) break;
        app->task_selected = i;
        if (i == app->model.task_count) app->task_row = false;
    } else if (app->model.task_count && app->task_selected >= app->model.task_count) app->task_selected = 0;
    if (app->selected >= app->model.head_count && app->model.head_count > 0U) app->selected = app->model.head_count - 1U;
}

/* Accepts one observation (ownership of next passes here) and recomposes. */
void model_accept(struct app *app, struct model *next) {
    struct head previous;
    struct task_observation task;
    bool had_head = app->selected < app->model.head_count, had_task = app->task_selected < app->model.task_count;
    if (had_head) previous = app->model.heads[app->selected];
    if (had_task) task = app->model.tasks[app->task_selected];
    if (next->protocol == MODEL_FLEET) remote_accept(app, next);
    else local_accept(app, next);
    compose_model(app);
    retarget_after_accept(app, &previous, had_head, &task, had_task);
}

void model_release(struct app *app) {
    free(app->local); free(app->remote);
    app->local = app->remote = NULL;
}

/* A failed remote observation keeps the rows as the last observation. */
void remote_failed(struct app *app, const char *error) {
    if (app->remote_known) app->remote_stale = true;
    copy_text(app->remote_error, sizeof(app->remote_error), error);
}

long remote_age(const struct app *app) {
    long age = app->remote_at ? (long)(time(NULL) - app->remote_at) : 0L;
    return age < 0 ? 0L : age;
}

/* Why the selected remote head cannot be acted on now, or NULL. Only a
 * fresh observation from a host that answers authorizes attach or interrupt. */
const char *remote_action_refusal(const struct app *app, const struct head *h, char *out, size_t size) {
    const struct host_observation *host;
    if (!head_remote(h)) return NULL;
    if (app->remote_stale) {
        snprintf(out, size, "Remote target is stale: the last good observation is %lds old; r retries before acting", remote_age(app));
        return out;
    }
    host = host_for_name(app, h->remote_host);
    if (!host || strcmp(host->state, "responded") || h->retained) {
        snprintf(out, size, "Remote target is stale: host %.60s is disconnected and this row is its last observation; r retries", h->remote_host);
        return out;
    }
    if (strcmp(host->connection, "reachable") || strcmp(host->freshness, "fresh") || !h->remote_project[0] || !h->remote_branch[0]) {
        copy_text(out, size, "Remote target is stale or lacks an exact interactive identity; refresh first");
        return out;
    }
    return NULL;
}

void remote_counts(const struct app *app, struct remote_counts *n) {
    size_t i;
    memset(n, 0, sizeof(*n));
    for (i = 0; i < app->model.head_count; i++) if (head_remote(&app->model.heads[i])) {
        n->heads++;
        if (app->model.heads[i].retained) n->retained++;
    }
    n->tasks = app->model.task_count;
    n->hosts = app->model.host_count;
    for (i = 0; i < app->model.host_count; i++) if (strcmp(app->model.hosts[i].state, "responded")) n->hosts_down++;
}
