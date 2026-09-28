#include "fleet/setup/assets.h"
#include "fleet/support/files.h"
#include "fleet/support/json.h"
#include "fleet/support/process.h"
#include "fleet/task/task.h"
#include <errno.h>
#include <fcntl.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#define ASSET_TABLE_LIMIT (1024U * 1024U)
#define ASSET_SIZE_LIMIT (16L * 1024L * 1024L)

/* ---- fleet-assets.tsv ---- */
static bool version_text(const char *value) {
    size_t n = strlen(value);
    return n && n < 32 && strspn(value, "0123456789.") == n && value[0] != '.' && value[n - 1] != '.' && !strstr(value, "..");
}
static bool sha_text(const char *value) {
    return strlen(value) == 64 && strspn(value, "0123456789abcdef") == 64;
}
/* Splits one row in place into exactly four tab-separated fields. */
static bool split_row(char *line, char *fields[4]) {
    size_t i;
    for (i = 0; i < 3; i++) {
        char *tab = strchr(line, '\t');
        if (!tab) return false;
        fields[i] = line;
        *tab = '\0';
        line = tab + 1;
    }
    fields[3] = line;
    return !strchr(line, '\t');
}
static bool row_valid(char *const fields[4]) {
    struct f_platform platform; char text[F_PLATFORM_TEXT], expected[128];
    if (!version_text(fields[0]) || f_platform_parse(&platform, fields[1]) || !sha_text(fields[2])) return false;
    f_platform_text(&platform, text);
    if (strcmp(text, fields[1])) return false;
    snprintf(expected, sizeof(expected), "hydra-fleet-%s-%s", fields[0], fields[1]);
    return !strcmp(expected, fields[3]);
}
struct asset_query { const char *version, *platform; char *sha256, *filename; int found; };
/* One non-header line: 0 ignored or recorded, -1 invalid table. */
static int table_line(char *line, struct asset_query *query) {
    char *fields[4];
    if (!line[0] || line[0] == '#') return 0;
    if (!split_row(line, fields) || !row_valid(fields)) return -1;
    if (strcmp(fields[0], query->version) || strcmp(fields[1], query->platform)) return 0;
    if (query->found++) return -1;
    memcpy(query->sha256, fields[2], 65);
    snprintf(query->filename, 128, "%s", fields[3]);
    return 0;
}
int setup_asset_row(const char *table, const char *version, const char *platform, char sha256[65], char filename[128]) {
    struct asset_query query = {version, platform, sha256, filename, 0};
    char *text = table ? f_read(table, ASSET_TABLE_LIMIT) : NULL, *line, *next; int status = 0;
    if (!text) return -1;
    next = strchr(text, '\n');
    if (!next || (size_t)(next - text) != strlen(SETUP_ASSET_HEADER) || strncmp(text, SETUP_ASSET_HEADER, strlen(SETUP_ASSET_HEADER))) status = -1;
    for (line = next ? next + 1 : NULL; !status && line && *line; line = next ? next + 1 : NULL) {
        next = strchr(line, '\n');
        if (next) *next = '\0';
        status = table_line(line, &query);
    }
    free(text);
    if (status) return -1;
    return query.found ? 0 : 1;
}
static bool regular(const char *path) {
    struct stat st;
    return !stat(path, &st) && S_ISREG(st.st_mode);
}
/* First existing regular file among bin-relative candidates. */
static int bin_relative(char path[F_PATH], const char *const *candidates) {
    const char *bin = getenv("HYDRA_BIN_DIR");
    for (; bin && bin[0] == '/' && *candidates; candidates++)
        if (!f_path(path, F_PATH, bin, *candidates) && regular(path)) return 0;
    return -1;
}
int setup_asset_table(char path[F_PATH]) {
    static const char *const candidates[] = {"../share/hydra/fleet-assets.tsv", "../release/fleet-assets.tsv", NULL};
    const char *file = getenv("HYDRA_FLEET_ASSETS_FILE");
    if (file && file[0]) return f_copy(path, F_PATH, file) || !regular(path) ? -1 : 0;
    return bin_relative(path, candidates);
}

/* ---- Private caches ---- */
int setup_private_dir(const char *name, char path[F_PATH]) {
    char home[F_PATH], fleet[F_PATH]; struct stat st; int home_fd = -1, fleet_fd = -1, dir_fd = -1, status = -1;
    if (f_mkdirs(f_home) || !realpath(f_home, home)) return -1;
    home_fd = open(home, O_RDONLY | O_DIRECTORY | O_NOFOLLOW);
    if (home_fd < 0 || fstat(home_fd, &st) || st.st_uid != geteuid() || (st.st_mode & 0022)) goto done;
    if ((fleet_fd = task_owned_directory(home_fd, "fleet", true)) < 0 || (dir_fd = task_owned_directory(fleet_fd, name, true)) < 0) goto done;
    if (fstat(dir_fd, &st) || (st.st_mode & 0077) || f_path(fleet, sizeof(fleet), home, "fleet") || f_path(path, F_PATH, fleet, name)) goto done;
    status = 0;
done:
    if (dir_fd >= 0) close(dir_fd);
    if (fleet_fd >= 0) close(fleet_fd);
    if (home_fd >= 0) close(home_fd);
    return status;
}
static int write_all(int fd, const char *data, size_t size) {
    while (size) {
        ssize_t n = write(fd, data, size);
        if (n < 0 && errno == EINTR) continue;
        if (n <= 0) return -1;
        size -= (size_t)n; data += n;
    }
    return 0;
}
static int temp_file(const char *dir, char temp[F_PATH]) {
    if (snprintf(temp, F_PATH, "%s/.partial.XXXXXX", dir) >= F_PATH) return -1;
    return mkstemp(temp);
}
/* Moves a verified private temporary file to DIR/NAME. */
static int publish(const char *temp, const char *dir, const char *name, char path[F_PATH]) {
    if (f_path(path, F_PATH, dir, name) || chmod(temp, 0600) || rename(temp, path)) return -1;
    return task_sync_dir(dir);
}
int setup_cache_store(const char *dir, const char *suffix, const char *data, size_t size, char digest[65], char path[F_PATH]) {
    char temp[F_PATH], name[80]; int fd = temp_file(dir, temp), status = -1;
    if (fd < 0) return -1;
    if (!write_all(fd, data, size) && !fsync(fd) && !close(fd)) {
        fd = -1;
        if (!f_hash(temp, digest) && snprintf(name, sizeof(name), "%s%s", digest, suffix) < (int)sizeof(name)) status = publish(temp, dir, name, path);
    }
    if (fd >= 0) close(fd);
    if (status) unlink(temp);
    return status;
}

/* ---- Helper sources ---- */
static json_object *asset_error(const struct setup_ctx *ctx, const char *code, const char *message, const char *recovery) {
    return setup_error(ctx, code, message, recovery, NULL);
}
static void describe(struct setup_binary *out, const char *path, const char *source, const char *name, bool pinned) {
    snprintf(out->path, sizeof(out->path), "%s", path);
    snprintf(out->source, sizeof(out->source), "%s", source);
    snprintf(out->name, sizeof(out->name), "%s", name);
    out->pinned = pinned;
}
static bool platform_of(const char *path, const struct f_platform *platform) {
    struct f_platform actual;
    return !f_platform_binary(path, &actual) && f_platform_equal(&actual, platform);
}
static json_object *use_binary(const struct setup_ctx *ctx, const char *binary, const struct f_platform *platform, struct setup_binary *out) {
    char text[F_PLATFORM_TEXT], message[160]; const char *slash = strrchr(binary, '/');
    f_platform_text(platform, text);
    if (!regular(binary)) return asset_error(ctx, "asset_unavailable", "--binary must name a regular file", "pass the path of a hydra-fleet executable built for the remote");
    if (!platform_of(binary, platform)) {
        snprintf(message, sizeof(message), "--binary is not a %s hydra-fleet executable", text);
        return asset_error(ctx, "platform_mismatch", message, "build the helper for the remote platform (make build-fleet-static ARCH=...) and pass it with --binary");
    }
    if (f_hash(binary, out->sha256)) return asset_error(ctx, "asset_unavailable", "cannot hash --binary", NULL);
    describe(out, binary, "binary", slash ? slash + 1 : binary, false);
    return NULL;
}
static json_object *unavailable(const struct setup_ctx *ctx, const struct f_platform *platform, const char *why) {
    char message[256], recovery[512];
    snprintf(message, sizeof(message), "no pinned hydra-fleet %s asset for %s-%s: %s", F_VERSION, platform->os, platform->arch, why);
    snprintf(recovery, sizeof(recovery),
             "development trees carry no pinned assets: run make build-fleet-static ARCH=%s (Docker, the same script as CI), "
             "then hydra remote provision %s --binary build/static/hydra-fleet-%s-%s", platform->arch, ctx->name, platform->os, platform->arch);
    return asset_error(ctx, "asset_unavailable", message, recovery);
}
static int local_helper(char path[F_PATH]) {
    static const char *const candidates[] = {"../build/hydra-fleet", "../libexec/hydra/hydra-fleet", NULL};
    const char *env = getenv("HYDRA_FLEET_BIN");
    if (env && env[0]) return f_copy(path, F_PATH, env) || !regular(path) ? -1 : 0;
    return bin_relative(path, candidates);
}
static json_object *use_local(const struct setup_ctx *ctx, const struct f_platform *platform, struct setup_binary *out) {
    struct f_platform local; char path[F_PATH];
    if (f_platform_local(&local) || !f_platform_equal(&local, platform)) return unavailable(ctx, platform, "the table has no row and this host's helper is for another platform");
    if (local_helper(path) || !platform_of(path, platform) || f_hash(path, out->sha256))
        return unavailable(ctx, platform, "the table has no row and this host's hydra-fleet helper was not found");
    describe(out, path, "local-helper", "hydra-fleet", false);
    return NULL;
}

/* ---- Pinned release assets ---- */
static int copy_file(const char *source, int fd) {
    char buffer[65536]; ssize_t n = 1; long total = 0; int in = open(source, O_RDONLY | O_CLOEXEC), status = 0;
    if (in < 0) return -1;
    while (!status && (n = read(in, buffer, sizeof(buffer))) != 0) {
        if (n < 0) status = errno == EINTR ? 0 : -1;
        else if ((total += n) > ASSET_SIZE_LIMIT || write_all(fd, buffer, (size_t)n)) status = -1;
    }
    close(in);
    return status;
}
static int curl_file(const char *url, const char *temp, unsigned seconds) {
    char connect[16], limit[24]; struct f_capture cap = {0}; int status;
    snprintf(connect, sizeof(connect), "%u", seconds);
    snprintf(limit, sizeof(limit), "%ld", ASSET_SIZE_LIMIT);
    char *argv[] = {"curl", "-fsSL", "--proto-redir", "=https", "--max-filesize", limit, "--connect-timeout", connect,
                    "-o", (char *)temp, (char *)url, NULL};
    status = f_run(argv, NULL, 0, 300, &cap) || cap.status ? -1 : 0;
    f_capture_free(&cap);
    return status;
}
/* Fetches BASE/FILENAME into the open temporary file. */
static int fetch(const char *filename, int fd, const char *temp, unsigned seconds) {
    const char *base = getenv("HYDRA_FLEET_ASSET_BASE"); char url[F_PATH], release[256];
    snprintf(release, sizeof(release), "%s/v%s", SETUP_ASSET_RELEASES, F_VERSION);
    if (!base || !base[0]) base = release;
    if (!strncmp(base, "file://", 7)) base += 7;
    if (f_path(url, sizeof(url), base, filename)) return -1;
    if (base[0] == '/') return copy_file(url, fd) || fsync(fd) ? -1 : 0;
    if (strncmp(base, "https://", 8) && strncmp(base, "http://", 7)) return -1;
    return curl_file(url, temp, seconds);
}
static bool cached(const char *path, const char *sha256) {
    char actual[65]; struct stat st;
    return !lstat(path, &st) && S_ISREG(st.st_mode) && st.st_uid == geteuid() && !f_hash(path, actual) && !strcmp(actual, sha256);
}
static json_object *download(const struct setup_ctx *ctx, const char *dir, const char *sha256, const char *filename, char path[F_PATH]) {
    char temp[F_PATH], actual[65]; int fd = temp_file(dir, temp), fetched;
    if (fd < 0) return asset_error(ctx, "asset_download_failed", "cannot create a private download file", "check $HYDRA_HOME/fleet/assets ownership and permissions");
    fetched = fetch(filename, fd, temp, ctx->seconds);
    close(fd);
    if (fetched) {
        unlink(temp);
        return asset_error(ctx, "asset_download_failed", "cannot download the pinned hydra-fleet release asset",
                           "check network access to the GitHub release, or download the asset yourself and set HYDRA_FLEET_ASSET_BASE to its directory; the pinned digest is still verified");
    }
    if (f_hash(temp, actual) || strcmp(actual, sha256)) {
        unlink(temp);
        return asset_error(ctx, "hash_mismatch", "the downloaded hydra-fleet asset does not match the digest pinned in fleet-assets.tsv; it was discarded",
                           "do not use this download; retry later or report the mismatch");
    }
    return publish(temp, dir, sha256, path) ? asset_error(ctx, "asset_download_failed", "cannot cache the verified asset", NULL) : NULL;
}
static json_object *use_release(const struct setup_ctx *ctx, const struct f_platform *platform, const char *sha256, const char *filename,
                                struct setup_binary *out) {
    char dir[F_PATH], path[F_PATH]; json_object *error;
    if (setup_private_dir("assets", dir) || f_path(path, sizeof(path), dir, sha256))
        return asset_error(ctx, "asset_download_failed", "cannot use the private asset cache", "check $HYDRA_HOME/fleet ownership and permissions");
    if (!cached(path, sha256) && (error = download(ctx, dir, sha256, filename, path))) return error;
    if (!platform_of(path, platform)) return asset_error(ctx, "platform_mismatch", "the pinned asset is not an executable for the remote platform", "report the fleet-assets.tsv row as invalid");
    memcpy(out->sha256, sha256, 65);
    describe(out, path, "release-asset", filename, true);
    return NULL;
}
json_object *setup_asset_resolve(const struct setup_ctx *ctx, const char *binary, const struct f_platform *platform,
                                 struct setup_binary *out) {
    char table[F_PATH], sha256[65], filename[128], text[F_PLATFORM_TEXT]; int row = 1;
    memset(out, 0, sizeof(*out));
    if (binary) return use_binary(ctx, binary, platform, out);
    f_platform_text(platform, text);
    if (!setup_asset_table(table)) row = setup_asset_row(table, F_VERSION, text, sha256, filename);
    if (row < 0) return asset_error(ctx, "asset_unavailable", "fleet-assets.tsv is unreadable or invalid", "reinstall Hydra, or fix HYDRA_FLEET_ASSETS_FILE");
    if (!row) return use_release(ctx, platform, sha256, filename, out);
    return use_local(ctx, platform, out);
}
