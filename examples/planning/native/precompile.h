#ifndef HYDRA_EXAMPLE_PRECOMPILE_H
#define HYDRA_EXAMPLE_PRECOMPILE_H
#include "fleet/plan/plan.h"
#include "fleet/support/files.h"
#include "fleet/support/process.h"

/* Inputs are borrowed. Returned JSON/text is caller-owned (json_object_put/free).
 * The manifest owns both the parsed value and the exact bytes being admitted. */
struct manifest { json_object *value; char *raw; size_t size; };
int precompile_error(const char *message);
json_object *precompile_read(const char *path, size_t limit, char **raw, size_t *size);
int precompile_hash(const char *bytes, size_t size, char digest[65]);
bool precompile_text_is(json_object *object, const char *key, const char *expected);
int staged_validate(const struct manifest *manifest, const char *run_id);
json_object *precompile_lower(const struct manifest *manifest, bool staged);
#endif
