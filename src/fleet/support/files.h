#ifndef HYDRA_FLEET_SUPPORT_FILES_H
#define HYDRA_FLEET_SUPPORT_FILES_H
#include "fleet/fleet.h"
#include "fleet/support/sha256.h"

/* Strings are borrowed and output buffers caller-owned. Free f_read/f_hex_read results. */
int f_path(char *dst, size_t size, const char *a, const char *b);
int f_mkdirs(const char *path);
char *f_read(const char *path, size_t limit);
/* The one durable write. The bytes go to a new private (0600) file beside
 * the destination; only when every byte is written, synced and the file is
 * closed is it renamed over the destination (replace) or linked into place
 * (never over an existing file, errno EEXIST), and the directory is synced
 * last. On failure the temporary file is removed, the destination keeps what
 * it held and -1 is returned with errno of the step that failed. The one
 * failure that leaves the new bytes in place is a directory that could not
 * be synced after the rename or link. */
int f_write(const char *path, const char *data, size_t size, bool replace);
/* As f_write for NAME in an open directory, which the caller opened as it
 * trusts it (O_NOFOLLOW, ownership checks) and keeps. */
int f_write_at(int dir, const char *name, const char *data, size_t size, bool replace);
/* The two halves of f_write_at, for a writer that must look at the
 * destination once more between them. Stage leaves a synced, closed
 * temporary file named in temp (empty after failure); publish consumes it
 * whether it succeeds or not. */
#define F_TEMP_NAME 256
int f_stage_at(int dir, const char *name, const char *data, size_t size, char temp[F_TEMP_NAME]);
int f_publish_at(int dir, const char *temp, const char *name, bool replace);
/* Appends to a log its caller alone appends to (it holds the log's lock):
 * every byte written, synced first when asked, or the file is cut back to
 * the length it had and -1 is returned, so a line that could not be written
 * whole does not stay in the log as a torn one. */
int f_append(int fd, const char *data, size_t size, bool sync);
int f_copy(char *dst, size_t size, const char *value);
/* Formats into dst; -1 when it does not fit. A function, not snprintf at
 * each call site: with fortify on, macOS makes every snprintf a macro with a
 * branch of its own. */
#if defined(__GNUC__) || defined(__clang__)
int f_format(char *dst, size_t size, const char *format, ...) __attribute__((format(printf, 3, 4)));
#else
int f_format(char *dst, size_t size, const char *format, ...);
#endif
bool f_name(const char *value);
/* f_sha256_path: the digest of a file, following symlinks. */
int f_hash(const char *path, char digest[65]);
char *f_hex_read(const char *path);
int f_remove_tree(const char *path);
int f_hex_write(const char *path, const char *hex, unsigned mode);
#endif
