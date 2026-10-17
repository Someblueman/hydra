#ifndef HYDRA_TUI_STATISTICS_COHORTS_H
#define HYDRA_TUI_STATISTICS_COHORTS_H
#include "internal.h"
/* The remote cohorts page of the statistics view (metric page HS_METRICS+1):
 * recorded task evidence grouped by location, host, placement, assignment
 * generation and provider. Measurements the protocol did not supply are
 * shown as unknown with the recorded reason, never as zero. */
#define STATISTICS_COHORT_PAGE (HS_METRICS + 1)
void statistics_cohorts_render(struct app *app, struct tv_canvas *c, struct tv_rect r);
/* Cycles the location filter: all, local, remote, then each recorded host. */
void statistics_location_cycle(struct statistics_view *v, int direction);
const char *statistics_location_label(const struct statistics_view *v);
/* Formats a remote measurement for a scope: unknown unless every eligible
 * step reported it; partial coverage shows the value with its n/N. */
void statistics_remote_text(enum hs_remote_count count, uint64_t value, size_t known, size_t eligible, char *out, size_t size);
#endif
