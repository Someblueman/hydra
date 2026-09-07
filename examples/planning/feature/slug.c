#include "slug.h"
/* Implementation is deliberately absent in the source fixture. */
int slugify(const char *input, char *output, size_t capacity) {
    (void)input;
    if (output && capacity) output[0] = '\0';
    return -1;
}
