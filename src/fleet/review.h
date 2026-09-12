#ifndef HYDRA_REVIEW_H
#define HYDRA_REVIEW_H

#include "fleet/fleet.h"
#include "fleet/support/json.h"

/* Read-only review of one immutable workflow candidate. All arguments
 * are borrowed.  references is an optional array of explicit objects with
 * kind (transcript, log or pr) and locator. Local files receive bounded data
 * previews; URLs are never fetched. The returned envelope belongs to the caller. */
json_object *review_candidate(const char *project, const char *run, const char *step, const char *attempt,
                              const char *revision, json_object *references);

/* Emit a review envelope without invoking an action route. */
int review_emit(json_object *review);
json_object *review_cli(int argc, char **argv);
/* Emit exactly one shared 13-field projection, including error envelopes.
 * Returns the command status; no returned JSON remains to be emitted. */
int review_workflow_data_cli(int argc, char **argv);

#endif
