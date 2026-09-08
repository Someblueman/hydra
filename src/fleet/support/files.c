#include "fleet/support/files.h"
#include "fleet/support/process.h"
#include "fleet/fleet.h"
#include <dirent.h>
#include <ctype.h>
#include <errno.h>
#include <fcntl.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
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
int f_hash(const char *path, char digest[65]) {
    struct f_capture cap = {0}; char *argv[] = {(char *)"shasum", (char *)"-a", (char *)"256", (char *)path, NULL};
    size_t i; int status = -1;
    if (f_run(argv, NULL, 0, 30, &cap)) goto done;
    if (cap.status == 127) {
        char *gnu[] = {(char *)"sha256sum", (char *)path, NULL};
        f_capture_free(&cap); if (f_run(gnu, NULL, 0, 30, &cap)) goto done;
    }
    if (cap.status || strlen(cap.out) < 64) goto done;
    for (i = 0; i < 64; i++) if (nibble(cap.out[i]) < 0) goto done;
    memcpy(digest, cap.out, 64); digest[64] = '\0'; status = 0;
done:
    f_capture_free(&cap); return status;
}

int f_copy(char *dst, size_t size, const char *value) {
    int n = snprintf(dst, size, "%s", value ? value : "");
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
int f_write(const char *path, const char *data, size_t size, bool replace) {
    char tmp[F_PATH]; int fd, status = -1;
    if (snprintf(tmp, sizeof(tmp), "%s.XXXXXX", path) >= (int)sizeof(tmp)) return -1;
    fd = mkstemp(tmp);
    if (fd < 0) return -1;
    while (size) {
        ssize_t n = write(fd, data, size);
        if (n < 0 && errno == EINTR) continue;
        if (n <= 0) goto done;
        size -= (size_t)n; data += n;
    }
    if (fsync(fd)) goto done;
    if (replace ? rename(tmp, path) == 0 : link(tmp, path) == 0) status = 0;
done:
    close(fd); unlink(tmp); return status;
}
bool f_name(const char *s) {
    if (!s || !isalnum((unsigned char)*s)) return false;
    for (; *s; s++) if (!isalnum((unsigned char)*s) && !strchr("_.-", *s)) return false;
    return true;
}
