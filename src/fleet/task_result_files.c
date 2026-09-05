#include "task.h"
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

/* Snapshot selected bytes once, then hash exactly those bytes, not a later read. */
int task_result_files(json_object *files, const char *root, const char *relative, const char *scratch, size_t *remaining) {
    char path[F_PATH], hash[65], *hex = NULL; json_object *file; int status = -1; size_t size;
    if (json_object_array_length(files) >= 256 || f_path(path, sizeof(path), scratch, "file")) return -1;
    if (task_file_copy(root, relative, path) || f_hash(path, hash) || !(hex = f_hex_read(path))) goto done;
    size = strlen(hex) / 2; if (size > *remaining) goto done;
    file = json_object_new_object(); f_string_add(file, "path", relative); f_string_add(file, "sha256", hash);
    f_string_add(file, "hex", hex); json_object_object_add(file, "bytes", json_object_new_int64((int64_t)size));
    json_object_array_add(files, file); *remaining -= size; status = 0;
done:
    free(hex); unlink(path); return status;
}
