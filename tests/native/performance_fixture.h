#ifndef HYDRA_NATIVE_PERFORMANCE_FIXTURE_H
#define HYDRA_NATIVE_PERFORMANCE_FIXTURE_H
#include "outcome_support.h"
json_object *measurement_manifest(struct outcome_fixture *f);
json_object *measurement_rows(double multiplier);
void measurement_csv(const char *path, json_object *rows, bool reverse,
                     bool quoted);
#endif
