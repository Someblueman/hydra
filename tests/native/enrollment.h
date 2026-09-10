#ifndef HYDRA_NATIVE_ENROLLMENT_H
#define HYDRA_NATIVE_ENROLLMENT_H
#include "support.h"
extern char ec_root[F_PATH], ec_home[F_PATH], ec_project[F_PATH],
    ec_counter[F_PATH], ec_cli_path[F_PATH], ec_fleet[F_PATH],
    ec_origin[F_PATH];
struct ec_package {
  char path[F_PATH], digest[65], prefix[F_PATH];
};
void ec_setup(void);
void ec_cleanup(void);
json_object *ec_cli(bool enroll, const char *const extra[], bool ok);
struct nt_child ec_start(bool enroll, const char *const extra[]);
void ec_qualify(char out[F_PATH], const char *fingerprint, const char *config,
                const char *const targets[]);
void ec_review(char out[F_PATH], char digest[65], const char *qualification,
               json_object *candidates, const struct ec_package *package,
               const char *project);
json_object *ec_apply(const char *intent, const char *digest);
void ec_one_review(char intent[F_PATH], char digest[65],
                   const struct ec_package *package);
void ec_status(json_object *result, const char *expected);
void ec_count(const char *name, const char *line, size_t count);
void ec_package_make(struct ec_package *package);
void ec_batch_case(void);
void ec_package_cases(void);
#define EC(enroll, ok, ...)                                                    \
  ec_cli(enroll, (const char *[]){__VA_ARGS__, NULL}, ok)
#endif
