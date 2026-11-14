#ifndef HYDRA_FLEET_SHA256_H
#define HYDRA_FLEET_SHA256_H
#include <stddef.h>
#include <stdint.h>

/* Incremental SHA-256; digests are 64 lowercase hex characters plus NUL. */
struct f_sha256 {
    uint32_t state[8];
    uint64_t bytes;
    unsigned char buffer[64];
    size_t used;
};
void f_sha256_init(struct f_sha256 *s);
void f_sha256_update(struct f_sha256 *s, const void *data, size_t size);
void f_sha256_final(struct f_sha256 *s, char digest[65]);
void f_sha256_text(const void *data, size_t size, char digest[65]);
/* The digest of a regular file read without following a final symlink; -1 on error. */
int f_sha256_file(const char *path, char digest[65]);

#endif
