#ifndef HYDRA_FLEET_TRANSPORT_PLATFORM_H
#define HYDRA_FLEET_TRANSPORT_PLATFORM_H
/*
 * Fleet helper platforms: an operating system ("linux" or "darwin") and an
 * architecture ("x86_64" or "aarch64"), written "os-arch" in text, asset
 * tables and --platform, and {"os":...,"arch":...} in install packages.
 * uname spellings (Linux, Darwin, amd64, arm64) normalize to these names.
 */
#include "fleet/fleet.h"
#include <json-c/json.h>

struct f_platform { char os[8], arch[8]; };
#define F_PLATFORM_TEXT 16

/* Normalizes uname-style names. 0, or -1 for an unsupported platform. */
int f_platform_set(struct f_platform *platform, const char *os, const char *arch);
/* Parses "os-arch" (normalized or uname spelling). 0 or -1. */
int f_platform_parse(struct f_platform *platform, const char *text);
/* The running host, from uname(3). 0 or -1. */
int f_platform_local(struct f_platform *platform);
/* Reads a 64-bit little-endian ELF (Linux) or Mach-O (Darwin) executable
 * header. 0, or -1 for unreadable, other formats and other machines. */
int f_platform_binary(const char *path, struct f_platform *platform);
bool f_platform_equal(const struct f_platform *left, const struct f_platform *right);
/* "os-arch"; out holds F_PLATFORM_TEXT bytes. */
void f_platform_text(const struct f_platform *platform, char out[F_PLATFORM_TEXT]);
/* Caller-owned {"os","arch"} object. */
json_object *f_platform_json(const struct f_platform *platform);
/* The optional package "platform" member (borrowed): 1 absent, 0 parsed,
 * -1 present but malformed or unsupported. */
int f_package_platform(json_object *package, struct f_platform *platform);
#endif
