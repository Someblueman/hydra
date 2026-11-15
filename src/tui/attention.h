#ifndef HYDRA_TUI_ATTENTION_H
#define HYDRA_TUI_ATTENTION_H
/* The attention read (`hydra workflow attention-data`, or the fleet's) and
 * the per-user seen store (attention.c). Inbox (inbox.c) lists the items;
 * this module owns their parsing, wording and seen markers. Include it after
 * internal.h. */
#include <stdbool.h>
#include <stddef.h>
#include <time.h>

#define ATTENTION_TEXT 128U
#define ATTENTION_REASON 256U
#define ATTENTION_DIGEST 65U

struct attention_item {
    char source[ATTENTION_TEXT]; char kind[ATTENTION_TEXT]; char reason[ATTENTION_REASON];
    char project[ATTENTION_TEXT]; char host[ATTENTION_TEXT]; char task[ATTENTION_TEXT];
    char run[ATTENTION_TEXT]; char step[ATTENTION_TEXT]; char attempt[ATTENTION_TEXT];
    char head[ATTENTION_TEXT]; char instance[ATTENTION_TEXT]; char request[ATTENTION_TEXT];
    char binding[ATTENTION_TEXT]; char revision[ATTENTION_DIGEST]; char identity[ATTENTION_DIGEST];
    char freshness[ATTENTION_TEXT]; char route_kind[ATTENTION_TEXT]; char navigable[ATTENTION_TEXT];
    char label[ATTENTION_TEXT]; char detail[ATTENTION_REASON];
    bool unseen;
};
struct attention_seen { char identity[ATTENTION_DIGEST]; char revision[ATTENTION_DIGEST]; };
struct attention_store;
/* The last good snapshot. stale: the latest read failed, so these are its
 * last observation; have_good: a snapshot was ever accepted; read_at: when
 * the last read was started (the badge's pace while Inbox is not shown). */
struct native_attention {
    struct attention_item items[NATIVE_ATTENTION_ITEMS];
    struct attention_seen seen[NATIVE_ATTENTION_SEEN];
    struct attention_store *store;
    size_t count, seen_count;
    bool stale, have_good, loading, partial, truncated;
    unsigned need_count, seen_group_count, stale_count, unknown_count, expired_count, failure_count;
    time_t read_at, good_at;
    char error[TEXT], feedback[TEXT];
};
struct app;
struct native_review_identity;

/* What an item asks of the user. Decisions and failures stay listed after
 * they are marked seen: seen never approves, rejects or resolves. */
bool attention_is_failure(const struct attention_item *item);
bool attention_needs_decision(const struct attention_item *item);
bool attention_is_approval(const struct attention_item *item);
bool attention_eligible(const struct attention_item *item);
bool attention_in_seen_group(const struct attention_item *item);
bool attention_present(const char *value);
/* Plain-language wording: a title ("Check failed"), the subject (label,
 * task or run, step, host), why it is listed and what its seen marker means. */
const char *attention_item_title(const struct attention_item *item);
void attention_item_subject(const struct app *app, const struct attention_item *item, char *out, size_t size);
void attention_item_why(const struct attention_item *item, char *out, size_t size);
const char *attention_seen_status(const struct attention_item *item);
const char *attention_next_text(const struct app *app, const struct attention_item *item);
void attention_review_identity(const struct attention_item *item, struct native_review_identity *id);
/* s on an item: toggles its marker for this exact identity and revision,
 * recorded through `hydra workflow attention-seen`; feedback says what changed. */
void attention_toggle_seen(struct app *app, size_t item);
/* An item of the last good snapshot by identity and revision; SIZE_MAX when
 * it is gone or changed (an action confirmed meanwhile then sends nothing). */
size_t attention_find(const struct app *app, const char *identity, const char *revision);
/* The project has no workflow runs, so the read has nothing to find. */
bool attention_no_runs(const struct app *app);
#endif
