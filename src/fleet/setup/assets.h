#ifndef HYDRA_FLEET_SETUP_ASSETS_H
#define HYDRA_FLEET_SETUP_ASSETS_H
/*
 * Fleet helper binaries for remote provisioning (design §5), private to the
 * provisioning workstream (assets.c, provision.c).
 *
 * Trust anchor: the local fleet-assets.tsv table, found at
 * $HYDRA_FLEET_ASSETS_FILE, else $HYDRA_BIN_DIR/../share/hydra/fleet-assets.tsv
 * (installed prefix), else $HYDRA_BIN_DIR/../release/fleet-assets.tsv (source
 * tree). Its first line is exactly "version\tplatform\tsha256\tfilename";
 * blank lines and lines starting with '#' are ignored; every row has four
 * tab-separated fields: a dotted numeric version, a normalized platform such
 * as linux-x86_64, a lowercase SHA-256, and hydra-fleet-VERSION-PLATFORM. A
 * malformed row or a duplicate (version, platform) invalidates the table.
 *
 * Downloads come from $HYDRA_FLEET_ASSET_BASE (https/http URL, file:// URL or
 * absolute directory; default the GitHub release for this version) via
 * `curl -fL` into a private temporary file, are verified against the table
 * digest, and only then move to $HYDRA_HOME/fleet/assets/SHA256 (0600 in a
 * 0700 directory). A mismatching download is deleted, never cached.
 */
#include "fleet/setup/setup.h"
#include "fleet/transport/platform.h"

#define SETUP_ASSET_RELEASES "https://github.com/Someblueman/hydra/releases/download"
#define SETUP_ASSET_HEADER "version\tplatform\tsha256\tfilename"

/* Chosen helper. source is "release-asset" (pinned), "binary" (--binary) or
 * "local-helper" (this host's helper for an identical remote platform). */
struct setup_binary {
    char path[F_PATH], sha256[65], source[16], name[128];
    bool pinned;
};

/* 0 row found (sha256 and filename filled), 1 no row, -1 unreadable or invalid table. */
int setup_asset_row(const char *table, const char *version, const char *platform, char sha256[65], char filename[128]);
/* The table path by the lookup order above; -1 when none exists. */
int setup_asset_table(char path[F_PATH]);
/* $HYDRA_HOME/fleet/NAME as a private directory owned by the user; 0 or -1. */
int setup_private_dir(const char *name, char path[F_PATH]);
/* Stores data as DIR/SHA256SUFFIX (0600) through a private temporary file and
 * reports the digest and final path; 0 or -1. */
int setup_cache_store(const char *dir, const char *suffix, const char *data, size_t size, char digest[65], char path[F_PATH]);
/* True when this host has exactly platform. */
bool setup_local_platform(const struct f_platform *platform);
/* This host's own hydra-fleet ($HYDRA_FLEET_BIN, else next to HYDRA_BIN_DIR)
 * when this host and its helper are both for platform: 0 with path, or -1. */
int setup_local_helper(const struct f_platform *platform, char path[F_PATH]);
/* Chooses the helper for platform: --binary FILE (unpinned; its ELF/Mach-O
 * header must match), else the pinned release asset for this version, else
 * this host's own helper when the platforms are identical (unpinned). NULL on
 * success; otherwise a caller-owned envelope (asset_unavailable,
 * asset_download_failed, hash_mismatch, platform_mismatch). */
json_object *setup_asset_resolve(const struct setup_ctx *ctx, const char *binary, const struct f_platform *platform,
                                 struct setup_binary *out);
#endif
