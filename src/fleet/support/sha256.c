#define _POSIX_C_SOURCE 200809L
#ifdef __APPLE__
#define _DARWIN_C_SOURCE
#endif
#include "fleet/support/sha256.h"
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

/* SHA-256 (FIPS 180-4), so digests need no child process. */

static const uint32_t round_constants[64] = {
    0x428a2f98, 0x71374491, 0xb5c0fbcf, 0xe9b5dba5, 0x3956c25b, 0x59f111f1, 0x923f82a4, 0xab1c5ed5,
    0xd807aa98, 0x12835b01, 0x243185be, 0x550c7dc3, 0x72be5d74, 0x80deb1fe, 0x9bdc06a7, 0xc19bf174,
    0xe49b69c1, 0xefbe4786, 0x0fc19dc6, 0x240ca1cc, 0x2de92c6f, 0x4a7484aa, 0x5cb0a9dc, 0x76f988da,
    0x983e5152, 0xa831c66d, 0xb00327c8, 0xbf597fc7, 0xc6e00bf3, 0xd5a79147, 0x06ca6351, 0x14292967,
    0x27b70a85, 0x2e1b2138, 0x4d2c6dfc, 0x53380d13, 0x650a7354, 0x766a0abb, 0x81c2c92e, 0x92722c85,
    0xa2bfe8a1, 0xa81a664b, 0xc24b8b70, 0xc76c51a3, 0xd192e819, 0xd6990624, 0xf40e3585, 0x106aa070,
    0x19a4c116, 0x1e376c08, 0x2748774c, 0x34b0bcb5, 0x391c0cb3, 0x4ed8aa4a, 0x5b9cca4f, 0x682e6ff3,
    0x748f82ee, 0x78a5636f, 0x84c87814, 0x8cc70208, 0x90befffa, 0xa4506ceb, 0xbef9a3f7, 0xc67178f2,
};

static uint32_t rotate(uint32_t x, unsigned n) { return (x >> n) | (x << (32U - n)); }

static void block(struct f_sha256 *s, const unsigned char *p) {
    uint32_t w[64], v[8];
    for (unsigned i = 0; i < 16; i++)
        w[i] = (uint32_t)p[4 * i] << 24 | (uint32_t)p[4 * i + 1] << 16 | (uint32_t)p[4 * i + 2] << 8 | (uint32_t)p[4 * i + 3];
    for (unsigned i = 16; i < 64; i++) {
        uint32_t s0 = rotate(w[i - 15], 7) ^ rotate(w[i - 15], 18) ^ (w[i - 15] >> 3);
        uint32_t s1 = rotate(w[i - 2], 17) ^ rotate(w[i - 2], 19) ^ (w[i - 2] >> 10);
        w[i] = w[i - 16] + s0 + w[i - 7] + s1;
    }
    memcpy(v, s->state, sizeof(v));
    for (unsigned i = 0; i < 64; i++) {
        uint32_t t1 = v[7] + (rotate(v[4], 6) ^ rotate(v[4], 11) ^ rotate(v[4], 25)) + ((v[4] & v[5]) ^ (~v[4] & v[6])) + round_constants[i] + w[i];
        uint32_t t2 = (rotate(v[0], 2) ^ rotate(v[0], 13) ^ rotate(v[0], 22)) + ((v[0] & v[1]) ^ (v[0] & v[2]) ^ (v[1] & v[2]));
        memmove(v + 1, v, 7 * sizeof(v[0]));
        v[4] += t1;
        v[0] = t1 + t2;
    }
    for (unsigned i = 0; i < 8; i++) s->state[i] += v[i];
}

void f_sha256_init(struct f_sha256 *s) {
    static const uint32_t initial[8] = {0x6a09e667, 0xbb67ae85, 0x3c6ef372, 0xa54ff53a, 0x510e527f, 0x9b05688c, 0x1f83d9ab, 0x5be0cd19};
    memcpy(s->state, initial, sizeof(initial));
    s->bytes = 0;
    s->used = 0;
}

void f_sha256_update(struct f_sha256 *s, const void *data, size_t size) {
    const unsigned char *p = data;
    s->bytes += size;
    while (size) {
        size_t take = sizeof(s->buffer) - s->used < size ? sizeof(s->buffer) - s->used : size;
        memcpy(s->buffer + s->used, p, take);
        s->used += take; p += take; size -= take;
        if (s->used == sizeof(s->buffer)) { block(s, s->buffer); s->used = 0; }
    }
}

void f_sha256_final(struct f_sha256 *s, char digest[65]) {
    uint64_t bits = s->bytes * 8U;
    unsigned char length[8];
    static const unsigned char pad = 0x80, zero = 0;
    for (unsigned i = 0; i < 8; i++) length[i] = (unsigned char)(bits >> (56 - 8 * i));
    f_sha256_update(s, &pad, 1);
    while (s->used != 56) f_sha256_update(s, &zero, 1);
    f_sha256_update(s, length, 8);
    for (unsigned i = 0; i < 8; i++) (void)snprintf(digest + 8 * i, 9, "%08lx", (unsigned long)s->state[i]);
}

void f_sha256_text(const void *data, size_t size, char digest[65]) {
    struct f_sha256 s;
    f_sha256_init(&s);
    f_sha256_update(&s, data, size);
    f_sha256_final(&s, digest);
}

int f_sha256_file(const char *path, char digest[65]) {
    unsigned char buffer[65536];
    struct f_sha256 s;
    ssize_t n;
    int fd = open(path, O_RDONLY | O_NOFOLLOW);
    if (fd < 0) return -1;
    f_sha256_init(&s);
    while ((n = read(fd, buffer, sizeof(buffer))) != 0) {
        if (n < 0 && errno == EINTR) continue;
        if (n < 0) { close(fd); return -1; }
        f_sha256_update(&s, buffer, (size_t)n);
    }
    close(fd);
    f_sha256_final(&s, digest);
    return 0;
}
