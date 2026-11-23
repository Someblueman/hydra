/* The place of src/tui/snapshot.c in a test that drives the control centre's
 * loop over a model it built by hand: such a test must not read a state store
 * (the developer's own, from wherever the test runs), nor have a refresh
 * replace its model. Here a refresh cannot start, which the loop reports as a
 * failed observation and which leaves the model as it is. */
#include "../../src/tui/snapshot.h"
#include <stddef.h>

struct native_snapshot *snapshot_new(const char *hydra) { (void)hydra; return NULL; }
void snapshot_free(struct native_snapshot *s) { (void)s; }
void snapshot_begin(struct native_snapshot *s, const char *selected, long budget_ms) { (void)s; (void)selected; (void)budget_ms; }
bool snapshot_active(const struct native_snapshot *s) { (void)s; return false; }
bool snapshot_step(struct native_snapshot *s, FILE **rows, bool *timed_out) {
    (void)s; *rows = NULL; *timed_out = false;
    return false;
}
void snapshot_cancel(struct native_snapshot *s) { (void)s; }
char *snapshot_take_activity(struct native_snapshot *s) { (void)s; return NULL; }
const struct store_location *snapshot_where(const struct native_snapshot *s) { (void)s; return NULL; }
int snapshot_print(const char *hydra, FILE *out) { (void)hydra; (void)out; return 1; }
