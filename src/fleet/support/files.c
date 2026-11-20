#include "fleet/support/files.h"
#include "fleet/support/process.h"
#include "fleet/fleet.h"
#include <dirent.h>
#include <ctype.h>
#include <errno.h>
#include <fcntl.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>

char *f_hex_read(const char *path) {
    struct stat st; FILE *fp; char *hex; size_t i; static const char digits[] = "0123456789abcdef";
    if (lstat(path, &st) || !S_ISREG(st.st_mode) || st.st_size < 0 || st.st_size > (long)F_LIMIT / 2) return NULL;
    fp = fopen(path, "rb"); if (!fp) return NULL;
    hex = malloc((size_t)st.st_size * 2 + 1); if (!hex) { fclose(fp); return NULL; }
    for (i = 0; i < (size_t)st.st_size; i++) {
        int byte = fgetc(fp); if (byte == EOF) { free(hex); fclose(fp); return NULL; }
        hex[i*2] = digits[(unsigned)byte >> 4]; hex[i*2+1] = digits[(unsigned)byte & 15];
    }
    hex[i*2] = '\0'; fclose(fp); return hex;
}
static int nibble(char value) {
    if (value >= '0' && value <= '9') return value - '0';
    if (value >= 'a' && value <= 'f') return value - 'a' + 10;
    return -1;
}
int f_hex_write(const char *path, const char *hex, unsigned mode) {
    size_t length; char *bytes; size_t i; int status;
    if (!hex || (length = strlen(hex)) > F_LIMIT || length % 2) return -1;
    bytes = malloc(length / 2 + 1); if (!bytes) return -1;
    for (i = 0; i < length; i += 2) {
        int high = nibble(hex[i]), low = nibble(hex[i+1]);
        if (high < 0 || low < 0) { free(bytes); return -1; }
        bytes[i/2] = (char)(high * 16 + low);
    }
    status = f_write(path, bytes, length / 2, false); free(bytes);
    if (!status && chmod(path, (mode_t)mode)) status = -1;
    return status;
}
int f_remove_tree(const char *path) {
    struct stat st; DIR *dir; struct dirent *entry; int status = 0;
    if (lstat(path, &st)) return -1;
    if (!S_ISDIR(st.st_mode)) return unlink(path);
    dir = opendir(path); if (!dir) return -1;
    while ((entry = readdir(dir))) {
        char child[F_PATH];
        if (!strcmp(entry->d_name, ".") || !strcmp(entry->d_name, "..")) continue;
        if (f_path(child, sizeof(child), path, entry->d_name) || f_remove_tree(child)) status = -1;
    }
    closedir(dir); if (rmdir(path)) status = -1; return status;
}
int f_hash(const char *path, char digest[65]) { return f_sha256_path(path, digest); }

int f_copy(char *dst, size_t size, const char *value) {
    int n = snprintf(dst, size, "%s", value ? value : "");
    return n >= 0 && (size_t)n < size ? 0 : -1;
}
int f_format(char *dst, size_t size, const char *format, ...) {
    va_list args;
    int n;
    va_start(args, format);
    n = vsnprintf(dst, size, format, args);
    va_end(args);
    return n >= 0 && (size_t)n < size ? 0 : -1;
}
int f_path(char *dst, size_t size, const char *a, const char *b) {
    int n = snprintf(dst, size, "%s/%s", a, b);
    return n >= 0 && (size_t)n < size ? 0 : -1;
}
int f_mkdirs(const char *path) {
    char copy[F_PATH]; char *p;
    if (f_copy(copy, sizeof(copy), path)) return -1;
    for (p = copy + 1; ; p++) {
        if (*p == '/' || *p == '\0') {
            char c = *p; struct stat st;
            *p = '\0';
            if (mkdir(copy, 0700) && errno != EEXIST) return -1;
            if (stat(copy, &st) || !S_ISDIR(st.st_mode)) return -1;
            *p = c;
            if (!c) break;
        }
    }
    return 0;
}
char *f_read(const char *path, size_t limit) {
    FILE *fp = path ? fopen(path, "rb") : stdin;
    char *data = NULL; size_t size;
    if (!fp) return NULL;
    data = malloc(limit + 1);
    if (!data) { if (path) fclose(fp); return NULL; }
    size = fread(data, 1, limit, fp);
    if (ferror(fp) || (size == limit && fgetc(fp) != EOF)) { free(data); data = NULL; }
    else data[size] = '\0';
    if (path) fclose(fp);
    return data;
}
/* The temporary file: NAME, a dot and six characters, as mkstemp names it,
 * created new and private (0600) in the open directory. The characters only
 * have to differ between writers; O_EXCL decides. */
static int temp_create(int dir, const char *name, char temp[F_TEMP_NAME]) {
    static const char letters[] = "abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789";
    static uint64_t sequence;
    size_t length = strlen(name); int attempt;
    if (!length || length + 8 > F_TEMP_NAME) { errno = ENAMETOOLONG; return -1; }
    memcpy(temp, name, length); temp[length] = '.'; temp[length + 7] = '\0';
    for (attempt = 0; attempt < 128; attempt++) {
        struct timespec now = {0, 0}; uint64_t value; int i, fd;
        (void)clock_gettime(CLOCK_REALTIME, &now);
        value = ((uint64_t)now.tv_sec << 30) ^ (uint64_t)now.tv_nsec ^ ((uint64_t)getpid() << 44);
        value = (value + ++sequence) * UINT64_C(0x9E3779B97F4A7C15);
        value ^= value >> 31;
        for (i = 0; i < 6; i++, value /= 62) temp[length + 1 + i] = letters[value % 62];
        fd = openat(dir, temp, O_WRONLY | O_CREAT | O_EXCL | O_NOFOLLOW | O_CLOEXEC, 0600);
        if (fd >= 0 || errno != EEXIST) return fd;
    }
    return -1;
}
int f_stage_at(int dir, const char *name, const char *data, size_t size, char temp[F_TEMP_NAME]) {
    int fd = temp_create(dir, name, temp), saved;
    if (fd < 0) { temp[0] = '\0'; return -1; }
    while (size) {
        ssize_t n = write(fd, data, size);
        if (n < 0 && errno == EINTR) continue;
        if (n <= 0) { if (!n) errno = EIO; goto failed; }
        size -= (size_t)n; data += n;
    }
    if (fsync(fd)) goto failed;
    if (close(fd)) { fd = -1; goto failed; }
    return 0;
failed:
    saved = errno;
    if (fd >= 0) close(fd);
    (void)unlinkat(dir, temp, 0); temp[0] = '\0';
    errno = saved; return -1;
}
/* EINVAL: this file system has no directory sync to offer. */
static int sync_dir_fd(int dir) { return fsync(dir) && errno != EINVAL ? -1 : 0; }
int f_sync_dir(const char *path) {
    int dir = open(path, O_RDONLY | O_DIRECTORY | O_CLOEXEC), status, saved;
    if (dir < 0) return -1;
    status = sync_dir_fd(dir); saved = errno; close(dir); errno = saved;
    return status;
}
int f_publish_at(int dir, const char *temp, const char *name, bool replace) {
    int status = replace ? renameat(dir, temp, dir, name) : linkat(dir, temp, dir, name, 0), saved = errno;
    /* A renamed file has left its temporary name; a linked one still has it,
     * and a failed publication leaves only it. */
    if ((status || !replace) && unlinkat(dir, temp, 0) && !status) return -1;
    if (status) { errno = saved; return -1; }
    return sync_dir_fd(dir);
}
int f_write_at(int dir, const char *name, const char *data, size_t size, bool replace) {
    char temp[F_TEMP_NAME];
    return f_stage_at(dir, name, data, size, temp) || f_publish_at(dir, temp, name, replace) ? -1 : 0;
}
int f_write(const char *path, const char *data, size_t size, bool replace) {
    char directory[F_PATH]; const char *name = strrchr(path, '/'); int dir, status, saved;
    size_t length = name ? (size_t)(name - path) : 0;
    if (length >= sizeof(directory)) { errno = ENAMETOOLONG; return -1; }
    /* "name" lives in ".", "/name" in "/". */
    memcpy(directory, name ? path : ".", name ? length : 1);
    if (name && !length) directory[length++] = '/';
    directory[name ? length : 1] = '\0';
    dir = open(directory, O_RDONLY | O_DIRECTORY | O_CLOEXEC);
    if (dir < 0) return -1;
    status = f_write_at(dir, name ? name + 1 : path, data, size, replace);
    saved = errno; close(dir); errno = saved;
    return status;
}
int f_append(int fd, const char *data, size_t size, bool sync) {
    struct stat st; int saved;
    if (fstat(fd, &st)) return -1;
    while (size) {
        ssize_t n = write(fd, data, size);
        if (n < 0 && errno == EINTR) continue;
        if (n <= 0) { if (!n) errno = EIO; goto cut; }
        size -= (size_t)n; data += n;
    }
    if (!sync || !fsync(fd)) return 0;
cut:
    saved = errno; (void)!ftruncate(fd, st.st_size); errno = saved;
    return -1;
}
bool f_name(const char *s) {
    if (!s || !isalnum((unsigned char)*s)) return false;
    for (; *s; s++) if (!isalnum((unsigned char)*s) && !strchr("_.-", *s)) return false;
    return true;
}
