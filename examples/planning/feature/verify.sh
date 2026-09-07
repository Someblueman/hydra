#!/bin/sh
set -eu
verify_root="$(mktemp -d)"
trap 'rm -rf "$verify_root"' EXIT
trap 'exit 130' INT
trap 'exit 143' TERM
trap 'exit 129' HUP
printf 'Makefile\nmain.c\nslug.c\nslug.h\n' > "$verify_root/expected-members"
tar -tf "$HYDRA_WORKFLOW_INPUTS_DIR/subject" | LC_ALL=C sort > "$verify_root/members"
cmp "$verify_root/expected-members" "$verify_root/members"
cp test_slug.c "$verify_root/test_slug.c"
tar -xf "$HYDRA_WORKFLOW_INPUTS_DIR/subject" -C "$verify_root"
(
    cd "$verify_root"
    make
    cc -std=c99 -Wall -Wextra -Werror -pedantic test_slug.c slug.c -o test-slug
    ./test-slug
    test "$(./catalog-slug ' Hello, WORLD! ' 'a__b')" = "$(printf 'hello-world\na-b')"
    test "$(./catalog-slug --limit 3 ABC)" = abc
    if ./catalog-slug --limit 2 ABC; then exit 1; else test "$?" -eq 2; fi
    if ./catalog-slug --limit 0 ABC; then exit 1; else test "$?" -eq 2; fi
    if ./catalog-slug; then exit 1; else test "$?" -eq 2; fi
)
digest="$(shasum -a 256 "$HYDRA_WORKFLOW_INPUTS_DIR/subject" | cut -d ' ' -f 1)"
printf '{"schema_version":1,"verdict":"pass","subject_sha256":"%s","requirements":["normalization","bounds","cli"],"evidence":"Built sealed source archive with strict C99; independent normalization, exact-capacity, null/zero-capacity, guard-byte and 10000-byte tests; exercised CLI output and exit codes."}\n' "$digest" > "$HYDRA_WORKFLOW_OUTPUTS_DIR/verification.json"
