#include "slug.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

int main(int argc, char **argv) {
    char output[65]; unsigned limit = 64; int i = 1;
    if (i < argc && !strcmp(argv[i], "--limit")) {
        char *end; unsigned long value;
        if (++i >= argc) return 2;
        value = strtoul(argv[i++], &end, 10);
        if (*end || value < 1 || value > 64) return 2;
        limit = (unsigned)value;
    }
    if (i >= argc) { fputs("usage: catalog-slug [--limit 1..64] <text>...\n", stderr); return 2; }
    for (; i < argc; i++) {
        if (slugify(argv[i], output, limit + 1)) { fputs("slug does not fit\n", stderr); return 2; }
        puts(output);
    }
    return 0;
}
