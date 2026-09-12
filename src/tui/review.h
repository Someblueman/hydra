#ifndef HYDRA_TUI_REVIEW_H
#define HYDRA_TUI_REVIEW_H

/* Value copy of the selected attention identity. No borrowed row pointers
 * survive a snapshot replacement or an asynchronous capture. */
struct native_review_identity {
    char source[128], kind[128], project[128], host[128], task[128];
    char run[128], step[128], attempt[128];
    char head[128], instance[128], request[128];
    char revision[65], identity[65], binding[65];
};

/* These functions borrow app and identity. app owns the review and capture;
 * destroy releases both, including a still-running read-only subprocess. */
void native_review_destroy(struct app *app);
void native_review_sync(struct app *app, const struct native_review_identity *identity, bool current);
void native_review_open(struct app *app, const struct native_review_identity *identity);
void native_review_tick(struct app *app);
bool native_review_key(struct app *app, char key);
bool native_review_render(struct app *app);
bool native_review_active(const struct app *app);

#endif
