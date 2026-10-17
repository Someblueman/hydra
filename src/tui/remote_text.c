#define _POSIX_C_SOURCE 200809L
#ifdef __APPLE__
#define _DARWIN_C_SOURCE
#endif
#include "internal.h"
#include "fleet_budget.h"
/* Words for remote rows. A remote head's status is what its host last
 * reported, and only while that report is fresh: otherwise the status says
 * so (stale, disconnected, unknown) instead of showing a desired state that
 * nothing confirms. Ages are the observation's, never a guess. */

const char *head_where(const struct head *h) {
    return head_remote(h) ? h->remote_host : "local";
}

static const struct host_observation *head_host(const struct app *app, const struct head *h) {
    return host_for_name(app, h->remote_host);
}

static bool host_answering(const struct host_observation *host) {
    return host && !strcmp(host->state, "responded");
}

/* The status column of a remote head. */
const char *remote_head_status(const struct app *app, const struct head *h) {
    const struct host_observation *host = head_host(app, h);
    if (app->remote_stale) return "stale";
    if (!host) return "unknown";
    if (h->retained || !host_answering(host)) return "disconnected";
    if (!strcmp(h->freshness, "stale")) return "stale";
    if (!strcmp(h->freshness, "unknown")) return "unknown";
    return h->desired[0] ? h->desired : "unknown";
}

enum tv_style remote_head_tone(const struct app *app, const struct head *h) {
    const char *status = remote_head_status(app, h);
    if (!strcmp(status, "stale") || !strcmp(status, "disconnected") || !strcmp(status, "unknown")) return TV_WARNING;
    return TV_BASE;
}

/* "fresh, 3s old" / "stale, 120s old (host disconnected)" / "unknown". */
void remote_observation_text(const struct app *app, const struct head *h, char *out, size_t size) {
    const struct host_observation *host = head_host(app, h);
    const char *age = h->age[0] && strcmp(h->age, "-") ? h->age : NULL;
    if (app->remote_stale) { snprintf(out, size, "stale, last good observation %lds ago", remote_age(app)); return; }
    if (h->retained) {
        if (age) snprintf(out, size, "retained from an observation %ss old; host disconnected", age);
        else copy_text(out, size, "retained from an earlier observation of unknown age; host disconnected");
        return;
    }
    if (!host) { copy_text(out, size, "unknown; no host observation"); return; }
    if (!host_answering(host)) { copy_text(out, size, "unknown; the host did not answer"); return; }
    if (age) snprintf(out, size, "%s, %ss old", h->freshness, age);
    else copy_text(out, size, h->freshness);
}

/* One line for a host row or its detail. */
void host_state_text(const struct app *app, const struct host_observation *host, char *out, size_t size) {
    const char *sep = dot(app);
    bool age = host->age[0] && strcmp(host->age, "-");
    if (app->remote_stale) { snprintf(out, size, "observation stale%slast good %lds ago", sep, remote_age(app)); return; }
    if (!host_answering(host)) {
        out[0] = '\0';
        if (host->error[0] && strcmp(host->error, "-")) text_append(out, size, "%s%s", host->error, sep);
        if (age) text_append(out, size, "last seen %ss ago", host->age);
        else text_append(out, size, "never observed");
        if (host->retained) text_append(out, size, "%s%u retained row%s", sep, host->heads, host->heads == 1 ? "" : "s");
        return;
    }
    snprintf(out, size, "%s%s%s", host->connection, sep, host->freshness);
    if (age) text_append(out, size, "%s%ss old", sep, host->age);
}

/* The agent of a remote head is not in the fleet list; say so. */
const char *remote_agent_name(const struct head *h) {
    if (!h->profile[0] || !strcmp(h->profile, "-")) return "not observed";
    return native_agent_name(h);
}

const char *task_status(const struct app *app, const struct task_observation *t) {
    if (app->remote_stale || t->retained) return "stale";
    if (!strcmp(t->freshness, "unknown")) return "unknown";
    if (!strcmp(t->freshness, "stale")) return "stale";
    if (!strcmp(t->state, "waiting_approval")) return "needs approval";
    if (!strcmp(t->state, "outcome_unknown")) return "unknown";
    return t->state;
}

enum tv_style task_tone(const struct app *app, const struct task_observation *t) {
    const char *status = task_status(app, t);
    if (!strcmp(status, "stale") || !strcmp(status, "unknown") || !strcmp(t->state, "failed") ||
        !strcmp(t->state, "waiting_approval") || !strcmp(t->state, "outcome_unknown")) return TV_WARNING;
    if (!strcmp(t->state, "running")) return TV_SUCCESS;
    return TV_BASE;
}

/* The remote source's freshness for the footer; false when nothing remote
 * is known and nothing failed. */
bool remote_footer_text(const struct app *app, char *out, size_t size, enum tv_style *tone) {
    long limit = 2L * HYDRA_TUI_REFRESH_SECONDS + HYDRA_FLEET_TUI_CAPTURE_BUDGET_MS / 1000L;
    if (app->remote_stale) {
        snprintf(out, size, "Remote: STALE, last good observation %lds ago%s%s", remote_age(app),
                 app->remote_error[0] ? "; " : "", app->remote_error);
        *tone = TV_WARNING;
        return true;
    }
    if (!app->remote_known || !app->model.host_count) return false;
    if (remote_age(app) > limit) { snprintf(out, size, "Remote updated %lds ago", remote_age(app)); *tone = TV_BASE; return true; }
    snprintf(out, size, "Remote live");
    *tone = TV_MUTED;
    return true;
}

/* r: ask for a fresh remote observation now, instead of waiting for the
 * next cycle. Nothing is mutated and nothing is assumed about the host. */
void remote_retry(struct app *app, const char *host) {
    native_observations_cancel(app, 5);
    app->remote_next = 0;
    if (host && host[0]) snprintf(app->notice, sizeof(app->notice), "Reconnecting to %.100s: requesting a fresh observation", host);
    else copy_text(app->notice, sizeof(app->notice), "Requesting a fresh remote observation");
    native_observations_tick(app, true);
}
