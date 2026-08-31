#ifndef LIBHYDRA_H
#define LIBHYDRA_H

#include <stddef.h>
#include <stdio.h>

#define HYDRA_CORE_VERSION "1.8.0"
#define HYDRA_PROTOCOL_VERSION 1

int hydra_json_write_string(FILE *out, const char *value);
int hydra_valid_id(const char *value);
int hydra_read_scalar(const char *path, char *out, size_t out_size);
int hydra_validate_state(const char *root, FILE *err);
int hydra_validate_events(const char *path, FILE *err);
int hydra_write_snapshot(const char *root, FILE *out, FILE *err);

#endif
