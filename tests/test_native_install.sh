#!/bin/sh
# Offline/source native artifact installation, verification, and rollback safety.

set -u

test_count=0
pass_count=0
fail_count=0
repo_root="$(cd "$(dirname "$0")/.." && pwd)"
test_root="$(mktemp -d)"
package="$test_root/package"

# shellcheck source=helpers.sh
# shellcheck disable=SC1091
. "$(dirname "$0")/helpers.sh"

cleanup() {
    rm -rf "$test_root"
}
trap cleanup EXIT HUP INT TERM

assert_file() {
    test_count=$((test_count + 1))
    if [ -f "$1" ]; then
        pass_count=$((pass_count + 1)); echo "[PASS] $2"
    else
        fail_count=$((fail_count + 1)); echo "[FAIL] $2"; echo "  Missing: $1"
    fi
}

file_hash() {
    if command -v sha256sum >/dev/null 2>&1; then
        sha256sum "$1" | awk '{print $1}'
    else
        shasum -a 256 "$1" | awk '{print $1}'
    fi
}

echo "Running native install tests..."
echo "==============================="

HYDRA_ALLOW_DIRTY_PACKAGE=1 HYDRA_CORE_PACKAGE_DIR="$package" sh "$repo_root/scripts/package-core.sh" >/dev/null
HYDRA_ALLOW_DIRTY_PACKAGE=1 HYDRA_TUI_PACKAGE_DIR="$package" sh "$repo_root/scripts/package-tui.sh" >/dev/null
prefix="$test_root/offline-prefix"
home="$test_root/home"
mkdir -p "$home"
HOME="$home" PREFIX="$prefix" HYDRA_INSTALL_CORE=required HYDRA_INSTALL_TUI=required \
    HYDRA_CORE_ARTIFACT="$package/hydra-core" HYDRA_TUI_ARTIFACT="$package/hydra-tui" \
    sh "$repo_root/install.sh" > "$test_root/install.out" 2>&1
assert_success $? "offline native installation succeeds"
assert_file "$prefix/libexec/hydra/hydra-core" "offline install places native helper"
assert_file "$prefix/libexec/hydra/hydra-core.sha256" "offline install records checksum"
assert_file "$prefix/libexec/hydra/hydra-core.platform" "offline install records platform"
assert_file "$prefix/libexec/hydra/hydra-core.dependencies" "offline install records dependency declaration"
assert_file "$prefix/libexec/hydra/hydra-core.source" "offline install records qualified source identity"
assert_equal "1" "$("$prefix/libexec/hydra/hydra-core" --protocol-version)" "installed core protocol handshake"
assert_file "$prefix/libexec/hydra/hydra-tui" "offline install places native TUI"
assert_file "$prefix/libexec/hydra/hydra-tui.sha256" "offline install records native TUI checksum"
assert_file "$prefix/libexec/hydra/hydra-tui.platform" "offline install records native TUI platform"
assert_file "$prefix/libexec/hydra/hydra-tui.dependencies" "offline install records native TUI dependencies"
assert_file "$prefix/libexec/hydra/hydra-tui.source" "offline install records native TUI source identity"
assert_equal "2" "$("$prefix/libexec/hydra/hydra-tui" --protocol-version)" "installed TUI protocol handshake"
HOME="$home" HYDRA_HOME="$home/.hydra" "$prefix/bin/hydra" tui --capabilities --json > "$test_root/tui-capabilities.json"
if grep -Fq '"native":true' "$test_root/tui-capabilities.json"; then
    assert_success 0 "installed CLI discovers adjacent native TUI"
else
    assert_success 1 "installed CLI discovers adjacent native TUI"
fi
mkdir -p "$home/.hydra/state/v2/projects"
printf '2\n' > "$home/.hydra/state/v2/schema-version"
HOME="$home" HYDRA_HOME="$home/.hydra" "$prefix/bin/hydra" snapshot --native \
    > "$test_root/installed-snapshot.out" 2> "$test_root/installed-snapshot.err"
assert_success $? "installed CLI discovers adjacent native helper"
assert_equal "" "$(sed -n '1p' "$test_root/installed-snapshot.err")" "installed native helper needs no fallback"
installed_hash="$(file_hash "$prefix/libexec/hydra/hydra-core")"
installed_tui_hash="$(file_hash "$prefix/libexec/hydra/hydra-tui")"

bad="$test_root/bad"
mkdir -p "$bad"
cp "$package/hydra-core" "$bad/hydra-core"
cp "$package/hydra-core.platform" "$bad/hydra-core.platform"
cp "$package/hydra-core.dependencies" "$bad/hydra-core.dependencies"
cp "$package/hydra-core.source" "$bad/hydra-core.source"
printf '0000  hydra-core\n' > "$bad/hydra-core.sha256"
HOME="$home" PREFIX="$prefix" HYDRA_INSTALL_CORE=required \
    HYDRA_CORE_ARTIFACT="$bad/hydra-core" sh "$repo_root/install.sh" > "$test_root/bad.out" 2>&1
assert_failure $? "checksum mismatch rejects offline artifact"
assert_equal "$installed_hash" "$(file_hash "$prefix/libexec/hydra/hydra-core")" "checksum failure preserves installed core"

bad_tui="$test_root/bad-tui"
mkdir -p "$bad_tui"
cp "$package/hydra-tui" "$bad_tui/hydra-tui"
cp "$package/hydra-tui.platform" "$bad_tui/hydra-tui.platform"
cp "$package/hydra-tui.dependencies" "$bad_tui/hydra-tui.dependencies"
cp "$package/hydra-tui.source" "$bad_tui/hydra-tui.source"
printf '0000  hydra-tui\n' > "$bad_tui/hydra-tui.sha256"
HOME="$home" PREFIX="$prefix" HYDRA_INSTALL_CORE=never HYDRA_INSTALL_TUI=required \
    HYDRA_TUI_ARTIFACT="$bad_tui/hydra-tui" sh "$repo_root/install.sh" > "$test_root/bad-tui.out" 2>&1
assert_failure $? "checksum mismatch rejects offline native TUI artifact"
assert_equal "$installed_tui_hash" "$(file_hash "$prefix/libexec/hydra/hydra-tui")" "checksum failure preserves installed native TUI"

fake="$test_root/version-skew"
mkdir -p "$fake"
# shellcheck disable=SC2016
printf '%s\n' '#!/bin/sh' 'case "$1" in --protocol-version) echo 1 ;; --version) echo "Hydra core 0.0.0 protocol 1" ;; esac' > "$fake/hydra-core"
chmod +x "$fake/hydra-core"
printf '%s  hydra-core\n' "$(file_hash "$fake/hydra-core")" > "$fake/hydra-core.sha256"
printf '%s %s\n' "$(uname -s)" "$(uname -m)" > "$fake/hydra-core.platform"
printf 'shell fixture\n' > "$fake/hydra-core.dependencies"
printf 'fixture-source\n' > "$fake/hydra-core.source"
HOME="$home" PREFIX="$prefix" HYDRA_INSTALL_CORE=required \
    HYDRA_CORE_ARTIFACT="$fake/hydra-core" sh "$repo_root/install.sh" > "$test_root/skew.out" 2>&1
assert_failure $? "version-skewed native artifact is rejected"
assert_equal "$installed_hash" "$(file_hash "$prefix/libexec/hydra/hydra-core")" "handshake failure preserves installed core"

HOME="$home" PREFIX="$prefix" sh "$repo_root/uninstall.sh" --purge >/dev/null 2>&1
if [ ! -e "$prefix/libexec/hydra" ]; then
    assert_success 0 "uninstall removes native helper and metadata"
else
    assert_success 1 "uninstall removes native helper and metadata"
fi

source_prefix="$test_root/source-prefix"
source_home="$test_root/source-home"
mkdir -p "$source_home"
HOME="$source_home" PREFIX="$source_prefix" HYDRA_INSTALL_CORE=required HYDRA_BUILD_CORE=1 \
    HYDRA_INSTALL_TUI=required HYDRA_BUILD_TUI=1 \
    sh "$repo_root/install.sh" > "$test_root/source.out" 2>&1
assert_success $? "source build and native installation succeeds"
assert_file "$source_prefix/libexec/hydra/hydra-core" "source workflow installs native helper"
assert_file "$source_prefix/libexec/hydra/hydra-tui" "source workflow installs native TUI"
HOME="$source_home" PREFIX="$source_prefix" sh "$repo_root/uninstall.sh" --purge >/dev/null 2>&1

archive_checkout="$test_root/source-archive"
archive_prefix="$test_root/archive-prefix"
archive_home="$test_root/archive-home"
mkdir -p "$archive_checkout" "$archive_home"
cp "$repo_root/Makefile" "$repo_root/install.sh" "$archive_checkout/"
cp -R "$repo_root/bin" "$repo_root/lib" "$repo_root/src" "$archive_checkout/"
HOME="$archive_home" PREFIX="$archive_prefix" HYDRA_INSTALL_CORE=required HYDRA_BUILD_CORE=1 \
    sh "$archive_checkout/install.sh" > "$test_root/archive-source.out" 2>&1
assert_success $? "source archive auto-builds native TUI without Git metadata"
assert_file "$archive_prefix/libexec/hydra/hydra-tui" "auto install builds and installs native TUI when a compiler is available"
assert_equal "hydra-2.0.0-source-tree" \
    "$(sed -n '1p' "$archive_prefix/libexec/hydra/hydra-core.source")" \
    "source archive records explicit non-commit provenance"
assert_equal "hydra-2.0.0-source-tree" \
    "$(sed -n '1p' "$archive_prefix/libexec/hydra/hydra-tui.source")" \
    "source archive records explicit native TUI provenance"

missing_prefix="$test_root/missing-prefix"
HOME="$test_root" PREFIX="$missing_prefix" HYDRA_INSTALL_CORE=required \
    HYDRA_CORE_ARTIFACT="$test_root/does-not-exist" sh "$repo_root/install.sh" > "$test_root/missing.out" 2>&1
assert_failure $? "required mode fails when offline artifact is absent"

printf '\nTests: %s, Passed: %s, Failed: %s\n' "$test_count" "$pass_count" "$fail_count"
[ "$fail_count" -eq 0 ]
