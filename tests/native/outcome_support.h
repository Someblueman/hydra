#ifndef HYDRA_NATIVE_OUTCOME_SUPPORT_H
#define HYDRA_NATIVE_OUTCOME_SUPPORT_H
#include "support.h"
struct outcome_fixture {
  char origin[F_PATH], folder[F_PATH], repo[F_PATH], hydra[F_PATH];
  char inputs[F_PATH], outputs[F_PATH], precompiler[F_PATH];
};
void outcome_setup(struct outcome_fixture *f, const char *example);
void outcome_finish(struct outcome_fixture *f, const char *message);
void outcome_status(char *const argv[], int expected);
void outcome_commit(void);
json_object *outcome_cli(struct outcome_fixture *f, const char *op,
                         const char *a, const char *b, const char *c,
                         int status);
void outcome_check(const char *command, int status);
#endif
