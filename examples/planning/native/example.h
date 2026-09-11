#ifndef HYDRA_PLAN_EXAMPLE_H
#define HYDRA_PLAN_EXAMPLE_H
#include "fleet/plan/plan.h"
#include "fleet/support/files.h"
#include "fleet/support/process.h"
#include <stdint.h>

/* All arguments are borrowed. JSON/text return values are owned by the caller
 * and released with json_object_put/free. Functions returning int use 0 for
 * success; failed parsing/I/O never supplies a usable value. */
char *ex_read(const char *path, size_t limit, size_t *size);
json_object *ex_json(const char *path, size_t limit);
int ex_path(char path[F_PATH], const char *environment, const char *name);
json_object *ex_input(const char *name);
int ex_hash(const void *bytes, size_t size, char digest[65]);
int ex_digest(json_object *value, bool slash_escape, char digest[65]);
int ex_write(const char *name, json_object *value);
bool ex_equal(json_object *a, json_object *b);
bool ex_text(json_object *object, const char *key, const char *text);
json_object *ex_strings(const char *const *values, size_t count);
/* Adds a new observation; borrows raw. */
int ex_observe(json_object *observations, const char *id, json_object *raw, bool slash_escape);
/* Evidence formatting only: callers independently decide valid/failed and own
 * all supplied JSON. case IDs are taken from observations. */
int ex_evidence(const char *output, const char *validator_key, const char *recipe_key,
                const char *identity, json_object *argv, json_object *requirements,
                json_object *obligations, json_object *observations, size_t failed,
                bool valid, bool slash_escape, json_object *limitations, const char *evidence);
int ex_manifest_check(void);
int ex_pattern_check(void);
int ex_staged_produce(void);
int ex_staged_check(bool first);
int ex_feature_evidence(void);
int ex_research_report(void);
int ex_research_check(void);
int ex_performance_measure(void);
int ex_performance_analyze(void);
int ex_performance_check(void);
extern const char *ex_program;
#endif
