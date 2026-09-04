#!/bin/sh
# Fresh-prefix install, verification, and uninstall tests
# Must not create branches or worktrees in the Hydra source repository.

test_count=0
pass_count=0
fail_count=0

# shellcheck disable=SC1091
. "$(dirname "$0")/helpers.sh"

REPO_ROOT="$(cd "$(dirname "$0")/.." && pwd)"
HYDRA_SRC="$REPO_ROOT/bin/hydra"
ORIGINAL_HOME="${HOME:-}"
export HYDRA_INSTALL_CORE=never
export HYDRA_INSTALL_TUI=never

assert_contains() {
    text="$1"
    pattern="$2"
    message="$3"

    test_count=$((test_count + 1))
    if echo "$text" | grep -q "$pattern"; then
        pass_count=$((pass_count + 1))
        echo "[PASS] $message"
    else
        fail_count=$((fail_count + 1))
        echo "[FAIL] $message"
        echo "  Text does not contain: '$pattern'"
        echo "  Actual text: '$text'"
    fi
}

assert_file() {
    path="$1"
    message="$2"
    test_count=$((test_count + 1))
    if [ -f "$path" ]; then
        pass_count=$((pass_count + 1))
        echo "[PASS] $message"
    else
        fail_count=$((fail_count + 1))
        echo "[FAIL] $message"
        echo "  Missing: $path"
    fi
}

assert_no_file() {
    path="$1"
    message="$2"
    test_count=$((test_count + 1))
    if [ ! -e "$path" ]; then
        pass_count=$((pass_count + 1))
        echo "[PASS] $message"
    else
        fail_count=$((fail_count + 1))
        echo "[FAIL] $message"
        echo "  Still exists: $path"
    fi
}

snapshot_source_repo() {
    (cd "$REPO_ROOT" && git worktree list && git branch --list)
}

echo "Running install/uninstall PREFIX tests..."
echo "========================================"

SRC_BEFORE="$(snapshot_source_repo)"

# --- install.sh to a writable prefix ---
echo "Testing install.sh to a writable PREFIX..."
home1="$(mktemp -d)"
prefix1="$(mktemp -d)"
export HOME="$home1"
unset HYDRA_ROOT
unset HYDRA_HOME

output="$(
    cd "$REPO_ROOT" || exit 1
    PREFIX="$prefix1" sh ./install.sh 2>&1
)"
exit_code=$?
assert_success "$exit_code" "install.sh should succeed on a writable PREFIX"
assert_contains "$output" "Hydra version" "install.sh should run hydra version"
assert_contains "$output" "Binary: $prefix1/bin/hydra" "install.sh should report binary path"
assert_contains "$output" "Libraries: $prefix1/lib/hydra" "install.sh should report library path"
assert_file "$prefix1/bin/hydra" "install.sh installs the binary"
assert_file "$prefix1/lib/hydra/git.sh" "install.sh installs libraries"

ver_out="$(HOME="$home1" HYDRA_ROOT='' HYDRA_HOME='' "$prefix1/bin/hydra" version 2>&1)"
assert_success $? "installed hydra version should succeed"
assert_contains "$ver_out" "Hydra version 2.0.0" "installed hydra reports 2.0.0"

doc_out="$(cd "$home1" && HOME="$home1" HYDRA_HOME="$home1/.hydra" HYDRA_ROOT='' "$prefix1/bin/hydra" doctor 2>&1)"
assert_success $? "installed hydra doctor should succeed"
assert_contains "$doc_out" "PREFIX install" "doctor should detect PREFIX layout"
assert_contains "$doc_out" "$prefix1/lib/hydra" "doctor should name installed libraries"

# Replace the installed binary with an older version marker, then reinstall into
# the same prefix. User state lives outside PREFIX and must survive the upgrade.
sed 's/HYDRA_VERSION="2.0.0"/HYDRA_VERSION="1.9.0"/' "$prefix1/bin/hydra" > "$prefix1/bin/hydra.upgrade"
mv "$prefix1/bin/hydra.upgrade" "$prefix1/bin/hydra"
chmod +x "$prefix1/bin/hydra"
mkdir -p "$home1/.hydra"
printf '%s\n' 'preserve-upgrade-state' > "$home1/.hydra/upgrade-marker"
(
    cd "$REPO_ROOT" || exit 1
    PREFIX="$prefix1" HYDRA_INSTALL_CORE=never sh ./install.sh 2>&1
) >/dev/null
assert_success $? "install.sh should upgrade an existing PREFIX"
upgrade_ver="$(HOME="$home1" HYDRA_ROOT='' HYDRA_HOME='' "$prefix1/bin/hydra" version 2>&1)"
assert_contains "$upgrade_ver" "Hydra version 2.0.0" "upgrade replaces the older binary"
assert_equal "preserve-upgrade-state" "$(sed -n '1p' "$home1/.hydra/upgrade-marker")" "upgrade preserves HYDRA_HOME state"

un_out="$(
    cd "$REPO_ROOT" || exit 1
    PREFIX="$prefix1" sh ./uninstall.sh --purge 2>&1
)"
assert_success $? "uninstall.sh should succeed for the same PREFIX"
assert_contains "$un_out" "Binary removed from: $prefix1/bin/hydra" "uninstall reports binary path"
assert_contains "$un_out" "Libraries removed from: $prefix1/lib/hydra" "uninstall reports library path"
assert_no_file "$prefix1/bin/hydra" "uninstall.sh removes the binary"
assert_no_file "$prefix1/lib/hydra" "uninstall.sh removes the library directory"

rm -rf "$home1" "$prefix1"

# --- make install / make uninstall ---
echo "Testing make install to a writable PREFIX..."
home2="$(mktemp -d)"
prefix2="$(mktemp -d)"
export HOME="$home2"
unset HYDRA_ROOT
unset HYDRA_HOME

output="$(
    cd "$REPO_ROOT" || exit 1
    make install PREFIX="$prefix2" 2>&1
)"
exit_code=$?
assert_success "$exit_code" "make install should succeed on a writable PREFIX"
assert_contains "$output" "Hydra version" "make install should run hydra version"
assert_contains "$output" "Binary: $prefix2/bin/hydra" "make install should report binary path"
assert_contains "$output" "Libraries: $prefix2/lib/hydra" "make install should report library path"
assert_file "$prefix2/bin/hydra" "make install installs the binary"
assert_file "$prefix2/lib/hydra/git.sh" "make install installs libraries"

ver_out="$(HOME="$home2" HYDRA_ROOT='' HYDRA_HOME='' "$prefix2/bin/hydra" version 2>&1)"
assert_success $? "make-installed hydra version should succeed"
assert_contains "$ver_out" "Hydra version" "make-installed hydra prints version"

un_out="$(
    cd "$REPO_ROOT" || exit 1
    PREFIX="$prefix2" sh ./uninstall.sh --purge 2>&1
)"
assert_success $? "make-install prefix can be uninstalled"
assert_no_file "$prefix2/bin/hydra" "make uninstall path removes the binary"
assert_no_file "$prefix2/lib/hydra" "make uninstall path removes libraries"

rm -rf "$home2" "$prefix2"

# --- DESTDIR staging ---
echo "Testing make install DESTDIR staging..."
stage="$(mktemp -d)"
home3="$(mktemp -d)"
output="$(cd "$REPO_ROOT" && HOME="$home3" HYDRA_HOME='' HYDRA_ROOT='' make install PREFIX=/usr/local DESTDIR="$stage" 2>&1)"
assert_success $? "make install DESTDIR should succeed"
assert_file "$stage/usr/local/bin/hydra" "DESTDIR stages the binary"
assert_file "$stage/usr/local/lib/hydra/git.sh" "DESTDIR stages libraries"
ver_out="$(HOME="$home3" HYDRA_ROOT='' HYDRA_HOME='' "$stage/usr/local/bin/hydra" version 2>&1)"
assert_success $? "DESTDIR-staged hydra should discover sibling libraries"
HOME="$home3" PREFIX=/usr/local DESTDIR="$stage" sh "$REPO_ROOT/uninstall.sh" --purge >/dev/null 2>&1 || true
rm -rf "$stage" "$home3"

# --- non-writable prefix ---
echo "Testing non-writable PREFIX recovery..."
output="$(PREFIX="/proc/hydra-no-write-$$" sh "$REPO_ROOT/install.sh" 2>&1)"
exit_code=$?
assert_failure "$exit_code" "install.sh should fail on a non-writable PREFIX"
assert_contains "$output" "PREFIX is not writable" "non-writable PREFIX names the precondition"
assert_contains "$output" "Next:" "non-writable PREFIX names a recovery action"
assert_contains "$output" "HOME/.local" "recovery suggests PREFIX=\$HOME/.local"

# --- source tree unchanged ---
SRC_AFTER="$(snapshot_source_repo)"
test_count=$((test_count + 1))
if [ "$SRC_BEFORE" = "$SRC_AFTER" ]; then
    pass_count=$((pass_count + 1))
    echo "[PASS] source repository worktrees and branches are unchanged"
else
    fail_count=$((fail_count + 1))
    echo "[FAIL] source repository worktrees or branches changed"
    echo "  Before: $SRC_BEFORE"
    echo "  After: $SRC_AFTER"
fi

# Restore HOME so later hydra invocations do not use a deleted prefix
if [ -n "$ORIGINAL_HOME" ]; then
    HOME="$ORIGINAL_HOME"
    export HOME
fi
unset HYDRA_HOME

# Source hydra still works from checkout
src_ver="$("$HYDRA_SRC" version 2>&1)"
assert_contains "$src_ver" "Hydra version" "run-from-source hydra version still works"

echo ""
echo "Test Results:"
echo "  Total:  $test_count"
echo "  Passed: $pass_count"
echo "  Failed: $fail_count"

if [ "$fail_count" -eq 0 ]; then
    echo "All tests passed!"
    exit 0
else
    echo "Some tests failed!"
    exit 1
fi
