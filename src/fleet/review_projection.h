#ifndef HYDRA_REVIEW_PROJECTION_H
#define HYDRA_REVIEW_PROJECTION_H
#include "fleet/fleet.h"
#include <json-c/json.h>

/* Borrowed computed envelope and ORIGINAL validated selected attention row. Selection
 * carries revision_sha256 and identity_sha256 (requested, never inferred).
 * Writes a complete bounded HYDRA_REVIEW v1 stream; performs no state checks,
 * reference fetching, or actions. Use review_selection_parse for untrusted argv.
 * Returns nonzero without output if invalid. */
int review_projection(json_object *envelope, json_object *selection);

/* Exactly 13 positional fields: KIND PROJECT HOST TASK RUN STEP ATTEMPT HEAD
 * INSTANCE REQUEST BINDING REVISION_SHA256 IDENTITY_SHA256. Source is fixed by
 * the command. Caller owns returned row; NULL rejects malformed/hash-mismatched
 * input. This validates selection identity, never current readiness. */
json_object *review_selection_parse(const char *source, int argc, char **argv);

/* Borrowed explicitly supplied references, at most 16 transcript/log/pr
 * locators. Caller owns returned array (json_object_put); NULL means invalid.
 * Only regular absolute local files get a bounded preview. URLs stay unopened. */
json_object *review_references(json_object *references);
#endif
