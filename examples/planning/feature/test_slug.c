#include "slug.h"
#include <assert.h>
#include <string.h>

int main(void) {
    const char *inputs[] = {"Hello, WORLD!", " a---b___c ", "", "---", "ABC123", "caf\303\251 noir", "a\tb\nc"};
    const char *expected[] = {"hello-world", "a-b-c", "", "", "abc123", "caf-noir", "a-b-c"};
    char output[128], guarded[] = {'A', 'B', 'C', 'D'}, huge[10000]; size_t i;
    for (i = 0; i < sizeof(inputs)/sizeof(inputs[0]); i++) {
        assert(!slugify(inputs[i], output, sizeof(output))); assert(!strcmp(output, expected[i]));
    }
    assert(!slugify("abc", output, 4) && !strcmp(output, "abc"));
    assert(slugify("abc", output, 3) == -1 && output[0] == '\0');
    assert(!slugify("---", guarded + 1, 1) && guarded[1] == '\0');
    assert(guarded[0] == 'A' && guarded[2] == 'C' && guarded[3] == 'D');
    assert(slugify("x", guarded + 1, 1) == -1 && guarded[1] == '\0');
    assert(guarded[0] == 'A' && guarded[2] == 'C' && guarded[3] == 'D');
    guarded[1] = 'B'; assert(slugify("x", guarded + 1, 0) == -1 && guarded[1] == 'B');
    assert(slugify("x", NULL, 1) == -1);
    output[0] = 'X';
    assert(slugify(NULL, output, sizeof(output)) == -1 && output[0] == '\0');
    memset(huge, '-', sizeof(huge)-1); huge[sizeof(huge)-1] = '\0';
    assert(!slugify(huge, output, 1) && output[0] == '\0');
    memset(huge, 'a', sizeof(huge)-1);
    assert(slugify(huge, output, sizeof(output)) == -1 && output[0] == '\0');
    return 0;
}
