#!/bin/sh
# Hydra state stays private below a directory that carries a default POSIX
# ACL. There the kernel ignores the umask and a new directory takes its group
# bits from the ACL, so umask 077 alone left HYDRA_HOME and its admission
# storage group-writable and admission refused them (the U10 loopback case on
# hosted CI). Hydra sets the directories it creates to 0700 explicitly; a
# shared home it did not create is still refused. Linux only: SKIP where
# default ACLs cannot be set.

test_count=0
pass_count=0
fail_count=0
root="$(cd "$(dirname "$0")/.." && pwd)"
HYDRA_BIN="$root/bin/hydra"
# shellcheck disable=SC1091
. "$root/tests/helpers.sh"

base="$(test_mktemp_dir)"
trap 'rm -rf "$base"' EXIT
trap 'exit 130' INT
trap 'exit 143' TERM
trap 'exit 129' HUP

# set_default_acl DIR: user::rwx group::rwx other::rwx as the inherited
# default ACL (the binary system.posix_acl_default format, version 2).
set_default_acl() {
    python3 -c '
import os, struct, sys
entries = ((0x01, 7), (0x04, 7), (0x20, 7))
value = struct.pack("<I", 2) + b"".join(struct.pack("<HHI", tag, perm, 0xFFFFFFFF) for tag, perm in entries)
os.setxattr(sys.argv[1], "system.posix_acl_default", value)
' "$1" 2>/dev/null
}

shared="$base/shared"
mkdir "$shared"
if [ "$(uname -s)" != Linux ] || ! command -v python3 >/dev/null 2>&1 || ! set_default_acl "$shared"; then
    echo "SKIP: default POSIX ACLs cannot be set here"
    exit 0
fi
mkdir "$shared/probe"
if [ -z "$(find "$shared/probe" -prune -perm -0020 -print)" ]; then
    echo "SKIP: this filesystem does not apply inherited default ACLs"
    exit 0
fi

# Print group- or other-writable entries, never following links.
writable() {
    find "$@" ! -type l \( -perm -0020 -o -perm -0002 \) -print 2>/dev/null
}

export HYDRA_HOME="$shared/home"
export HYDRA_NONINTERACTIVE=1
export HYDRA_SKIP_AI=1
export HYDRA_NO_SWITCH=1
export HYDRA_FLEET_BIN="${HYDRA_FLEET_BIN:?HYDRA_FLEET_BIN is required: run via make test-one T=acl_state}"

echo "Testing private directory creation below a default ACL..."

(
    # shellcheck disable=SC1091
    . "$root/lib/locks.sh"
    mkdir -m 755 "$shared/kept"
    hydra_private_mkdir "$shared/kept/a/b" "$shared/c"
)
assert_success $? "hydra_private_mkdir creates nested directories"
assert_equal "" "$(writable "$shared/kept/a" "$shared/c")" "every directory it creates is private"
assert_equal "" "$(find "$shared/kept" -prune ! -perm 755 -print)" "an existing parent keeps its mode"
: > "$shared/file"
(
    # shellcheck disable=SC1091
    . "$root/lib/locks.sh"
    hydra_private_mkdir "$shared/file" 2>/dev/null
)
assert_failure $? "a path that is a file is refused"

echo ""
echo "Testing Hydra state below a default ACL..."

"$HYDRA_BIN" admission request acl-probe project_acl 60 - > "$base/request.out" 2>&1
grep -q '"state":"reserved"' "$base/request.out"
assert_success $? "admission grants a request in a fresh home below a default ACL"
"$HYDRA_BIN" admission release acl-probe --confirmed >/dev/null 2>&1
assert_success $? "admission releases the request"

repo="$base/repo"
mkdir "$repo"
cd "$repo" || exit 1
git init -q
git config user.email test@example.invalid
git config user.name "Hydra test"
printf 'base\n' > README
git add README
git -c commit.gpgSign=false commit -qm base
"$HYDRA_BIN" init --no-agent --trust >/dev/null 2>&1
assert_success $? "init records host-local trust below a default ACL"
"$HYDRA_BIN" spawn --headless --no-agent acl-head > "$base/spawn.out" 2>&1
assert_success $? "headless spawn passes admission below a default ACL"
"$HYDRA_BIN" admission status --json > "$base/status.out" 2>&1
assert_success $? "admission status reads the home after a spawn"
assert_equal "" "$(find "$HYDRA_HOME" "$HYDRA_HOME/admission" "$HYDRA_HOME/locks" "$HYDRA_HOME/state" -prune \
    \( -perm -0020 -o -perm -0002 \) -print 2>&1)" "HYDRA_HOME, admission, lock and state directories are private"
"$HYDRA_BIN" kill acl-head >/dev/null 2>&1 || true

echo ""
echo "Testing that a shared home is still refused..."

export HYDRA_HOME="$shared/preexisting"
mkdir "$HYDRA_HOME"
chmod 775 "$HYDRA_HOME"
"$HYDRA_BIN" admission status --json > "$base/refused.out" 2>&1
assert_failure $? "a group-writable home Hydra did not create is refused"
grep -q 'HYDRA_HOME must be owned by you and not group- or world-writable' "$base/refused.out"
assert_success $? "the refusal names the failed precondition"
assert_equal "" "$(find "$HYDRA_HOME" -prune ! -perm 775 -print)" "the existing home keeps its mode"

echo ""
echo "============================================"
echo "Test Results:"
echo "Total:  $test_count"
echo "Passed: $pass_count"
echo "Failed: $fail_count"
[ "$fail_count" -eq 0 ]
