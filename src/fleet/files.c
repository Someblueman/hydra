#include "fleet.h"
#include <dirent.h>
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
