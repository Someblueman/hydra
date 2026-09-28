#!/bin/sh
# Public installer modes and adjacent fleet-helper discovery in disposable prefixes.
set -u
test_count=0
pass_count=0
fail_count=0
root="$(cd "$(dirname "$0")/.." && pwd)"
fixture="$(mktemp -d)"
binary="${HYDRA_FLEET_BIN:-$root/build/hydra-fleet}"
package_binary="${HYDRA_TEST_PACKAGE_BINARY:-$binary}"
trap 'rm -rf "$fixture"' 0
trap 'exit 130' INT
trap 'exit 143' HUP TERM
[ -x "$binary" ] || { echo 'Build the fleet helper before this test' >&2; exit 1; }
# shellcheck disable=SC1091
. "$root/tests/helpers.sh"

# shellcheck source=/dev/null
. "$root/tests/headless_path.sh"
headless_path "$fixture/no-tmux"
source_tree="$fixture/source"
test_home="$fixture/home"
prefix="$fixture/installed"
mkdir -p "$source_tree/build" "$source_tree/scripts" "$source_tree/docs/licenses" "$source_tree/release" "$test_home"
cp "$root/install.sh" "$root/uninstall.sh" "$root/LICENSE" "$source_tree/"
cp "$root/release/fleet-assets.tsv" "$source_tree/release/"
cp -R "$root/bin" "$root/lib" "$source_tree/"
cp "$root/scripts/install-fleet.sh" "$source_tree/scripts/"
cp "$root/docs/licenses/json-c.txt" "$source_tree/docs/licenses/"
cp "$binary" "$source_tree/build/hydra-fleet"

install_fleet() {
    HOME="$test_home" HYDRA_HOME="$test_home/state" PREFIX="$1" DESTDIR='' \
        HYDRA_INSTALL_CORE=never HYDRA_INSTALL_TUI=never HYDRA_INSTALL_FLEET="$2" \
        sh "$source_tree/install.sh" > "$fixture/install.log" 2>&1
}

install_fleet "$prefix" required
assert_success $? "required fleet installation succeeds with the real helper"
cmp "$binary" "$prefix/libexec/hydra/hydra-fleet"
assert_success $? "installed fleet helper matches the built bytes"
cmp "$root/docs/licenses/json-c.txt" "$prefix/libexec/hydra/hydra-fleet.LICENSE"
assert_success $? "fleet installation includes the JSON-C license"
assert_equal 'Hydra fleet protocol 1' "$("$prefix/libexec/hydra/hydra-fleet" --version)" "installed fleet version handshake"
cmp "$root/LICENSE" "$prefix/share/licenses/hydra/LICENSE"
assert_success $? "installation includes the Hydra license"
cmp "$root/release/fleet-assets.tsv" "$prefix/share/hydra/fleet-assets.tsv"
assert_success $? "installation includes the pinned fleet asset digests"
(
    unset HYDRA_ROOT HYDRA_FLEET_BIN
    HOME="$test_home" HYDRA_HOME="$test_home/state" "$prefix/bin/hydra" fleet handshake --json
) > "$fixture/handshake.json" 2> "$fixture/handshake.err"
assert_success $? "installed shell discovers its adjacent fleet helper"
grep -q '"task_protocol":1' "$fixture/handshake.json"
assert_success $? "installed fleet exposes the task protocol"

# Controlled SSH boundary also exercises the bootstrap script with no tmux.
mkdir "$fixture/transport"
cat > "$fixture/transport/ssh" <<'SSH'
#!/bin/sh
while [ "$#" -gt 2 ]; do shift; done
exec /bin/sh -c "$2"
SSH
chmod +x "$fixture/transport/ssh"
transport_path="$fixture/transport:$PATH"
(
    unset HYDRA_ROOT HYDRA_FLEET_BIN
    export HOME="$test_home" HYDRA_HOME="$test_home/state" PATH="$fixture/transport:$PATH"
    "$prefix/bin/hydra" remote add bootstrap loopback --hydra "$prefix/bin/hydra" >/dev/null || exit 1
    "$prefix/bin/hydra" fleet package --source "$root" --binary "$package_binary" --output "$fixture/package" > "$fixture/package.json" || exit 1
    digest="$(sed -n 's/.*"sha256":"\([^"]*\)".*/\1/p' "$fixture/package.json")"
    "$prefix/bin/hydra" fleet bootstrap bootstrap --input "$fixture/package" --sha256 "$digest" > "$fixture/bootstrap.json" || exit 1
    "$prefix/bin/hydra" doctor > "$fixture/doctor.log"
)
assert_success $? "pinned bootstrap and doctor work without tmux"
grep -q '"ok":true' "$fixture/bootstrap.json"
assert_success $? "bootstrap qualifies the installed protocol without a terminal"

# Platform-bound packages from an installed prefix (lib/hydra, share/licenses).
local_os="$(uname -s | tr '[:upper:]' '[:lower:]')"
case "$(uname -m)" in arm64|aarch64) local_arch=aarch64 other_arch=x86_64 ;; *) local_arch=x86_64 other_arch=aarch64 ;; esac
package_cli() {
    (
        unset HYDRA_ROOT HYDRA_FLEET_BIN
        HOME="$test_home" HYDRA_HOME="$test_home/state" "$prefix/bin/hydra" fleet package --source "$prefix" --binary "$package_binary" "$@"
    )
}
package_cli --platform "$local_os-$local_arch" --output "$fixture/platform-package" > "$fixture/platform-package.json"
assert_success $? "an installed prefix packages for the local platform"
grep -q '"platform":{"os":"'"$local_os"'","arch":"'"$local_arch"'"}' "$fixture/platform-package"
assert_success $? "the package records its platform"
grep -q '"path":"lib\\/hydra\\/git\.sh"' "$fixture/platform-package"
assert_success $? "installed-prefix libraries are packaged under lib/hydra"
package_cli --platform "$local_os-$other_arch" --output "$fixture/other-package" > "$fixture/other-package.json"
assert_failure $? "a helper for another architecture is refused"
grep -q '"code":"platform_mismatch"' "$fixture/other-package.json"
assert_success $? "the refusal names the platform mismatch"
package_cli --platform solaris-sparc --output "$fixture/bad-package" > "$fixture/bad-package.json"
assert_failure $? "an unsupported platform name is refused"
platform_digest="$(sed -n 's/.*"sha256":"\([^"]*\)".*/\1/p' "$fixture/platform-package.json")"
(
    unset HYDRA_ROOT HYDRA_FLEET_BIN
    HOME="$test_home" HYDRA_HOME="$test_home/state" PATH="$transport_path" \
        "$prefix/bin/hydra" fleet bootstrap bootstrap --input "$fixture/platform-package" --sha256 "$platform_digest" > "$fixture/platform-bootstrap.json"
)
assert_success $? "a platform-bound package bootstraps on its own platform"

install_fleet "$fixture/disabled" never
assert_success $? "never mode installs the shell with a fleet binary available"
test ! -e "$fixture/disabled/libexec/hydra/hydra-fleet"
assert_success $? "never mode omits the fleet helper from a fresh prefix"

rm "$source_tree/build/hydra-fleet"
install_fleet "$fixture/missing" required
assert_failure $? "required mode refuses a missing fleet helper"
install_fleet "$fixture/optional" auto
assert_success $? "auto mode permits a shell-only installation without fleet"
test ! -e "$fixture/optional/libexec/hydra/hydra-fleet"
assert_success $? "missing optional helper is not installed"

printf '%s\n' '#!/bin/sh' 'echo incompatible-fleet' > "$source_tree/build/hydra-fleet"
chmod +x "$source_tree/build/hydra-fleet"
install_fleet "$prefix" required
assert_failure $? "incompatible helper handshake is rejected"
cmp "$binary" "$prefix/libexec/hydra/hydra-fleet"
assert_success $? "rejected helper preserves the previously installed fleet bytes"
install_fleet "$fixture/invalid-mode" invalid
assert_failure $? "invalid fleet installation mode is rejected"

HOME="$test_home" HYDRA_HOME="$test_home/state" PREFIX="$prefix" DESTDIR='' \
    sh "$source_tree/uninstall.sh" --purge > "$fixture/uninstall.log" 2>&1
assert_success $? "public uninstall succeeds after fleet installation"
test ! -e "$prefix/libexec/hydra/hydra-fleet"
assert_success $? "uninstall removes the fleet helper"
test ! -e "$prefix/libexec/hydra/hydra-fleet.LICENSE"
assert_success $? "uninstall removes the fleet license"
test ! -e "$prefix/share/hydra" && test ! -e "$prefix/share/licenses/hydra"
assert_success $? "uninstall removes Hydra's shared files"

printf '\nTests: %s, Passed: %s, Failed: %s\n' "$test_count" "$pass_count" "$fail_count"
[ "$fail_count" -eq 0 ]
