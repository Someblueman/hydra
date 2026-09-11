#ifndef HYDRA_TEST_FIXTURE_H
#define HYDRA_TEST_FIXTURE_H
#include "fleet/plan/plan.h"
#include "fleet/support/files.h"
#include "fleet/support/json.h"
#include "fleet/support/process.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Test fixture constructors return owned JSON; fx_field returns a borrowed value. */
/* Inline so each fixture translation unit exposes its fail-fast preconditions
 * to the static analyzer as well as the compiler. */
static inline void fx_require(int condition, const char *message) {
    if (!condition) {
        fprintf(stderr, "fixture check failed: %s\n", message);
        exit(1);
    }
}
json_object *fx_read(const char *path);
json_object *fx_field(json_object *object, const char *name);
void fx_save(const char *path, json_object *object);
void fx_path(char out[4096], const char *base, const char *tail);
int fx_plan(int argc, char **argv);
int fx_events(int argc, char **argv);
int fx_reuse(int argc, char **argv);
int fx_v3(int argc, char **argv);
#endif
