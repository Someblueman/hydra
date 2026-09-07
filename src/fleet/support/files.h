#ifndef HYDRA_FLEET_SUPPORT_FILES_H
#define HYDRA_FLEET_SUPPORT_FILES_H
#include "fleet/fleet.h"

/* Strings are borrowed and output buffers caller-owned. Free f_read/f_hex_read results. */
int f_path(char *dst, size_t size, const char *a, const char *b);
int f_mkdirs(const char *path);
char *f_read(const char *path, size_t limit);
int f_write(const char *path, const char *data, size_t size, bool replace);
int f_copy(char *dst, size_t size, const char *value);
bool f_name(const char *value);
int f_hash(const char *path, char digest[65]);
char *f_hex_read(const char *path);
int f_remove_tree(const char *path);
int f_hex_write(const char *path, const char *hex, unsigned mode);
#endif
