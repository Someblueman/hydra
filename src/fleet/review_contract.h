#ifndef HYDRA_REVIEW_CONTRACT_H
#define HYDRA_REVIEW_CONTRACT_H

#include "fleet/fleet.h"
#include "fleet/support/json.h"

/* Borrowed paths. The scalar and JSON results are caller-owned (free/put).
 * Historical validation reads retained records only; it grants no live action
 * authority. data receives the validated retained manifest, including when
 * subsequent evidence or compiled-plan checks fail. */
char *review_scalar(const char *directory, const char *name);
/* Removes Git configuration injected through the environment from this
 * process (GIT_CONFIG_COUNT and its pairs, GIT_CONFIG_PARAMETERS), so review's
 * Git children neither see it nor refuse for it; then succeeds when no
 * repository or configuration-file redirect remains. */
bool review_git_hermetic(void);
json_object *review_retained(const char *run, const char *project, json_object **data);
void review_inventory(json_object *out, json_object *selected, json_object *data, const char *attempt,
                      bool expired);

#endif
