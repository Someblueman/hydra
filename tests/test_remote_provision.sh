#!/bin/sh
# Remote provisioning (U10 S3) over a fake SSH boundary: pinned release asset
# from a file:// base, approval before any remote change, digest refusal
# without caching, lost responses reconciled without a second install,
# same-version and same-pin reuse, other pins untouched, platform mismatch
# refused before the package binary runs, unpinned --binary and local-helper
# plans, and no alias publication. Every case runs under umask 022 and 002.
set -eu
root="$(CDPATH='' cd -- "$(dirname "$0")/.." && pwd)"
fixture="$(mktemp -d)"
fixture="$(cd "$fixture" && pwd -P)"
trap 'rm -rf "$fixture"' 0
trap 'exit 130' INT
trap 'exit 143' TERM HUP
fleet="${HYDRA_FLEET_BIN:-$root/build/hydra-fleet}"
[ -x "$fleet" ] || { echo 'Build the fleet helper before this test' >&2; exit 1; }
version="$(sed -n 's/^#define F_VERSION "\(.*\)"$/\1/p' "$root/src/fleet/fleet.h")"
[ -n "$version" ]
unset CI HYDRA_NONINTERACTIVE HYDRA_ROOT HYDRA_FLEET_ASSETS_FILE HYDRA_FLEET_ASSET_BASE PROVISION_REMOTE_PATH PROVISION_UNAME
local_os="$(uname -s)" local_arch="$(uname -m)"
platform="$(printf '%s' "$local_os" | tr '[:upper:]' '[:lower:]')"
case "$local_arch" in arm64|aarch64) platform="$platform-aarch64" other="linux-x86_64" ;; *) platform="$platform-x86_64" other="linux-aarch64" ;; esac
[ "$platform" != linux-x86_64 ] || other=linux-aarch64
[ "$platform" != linux-aarch64 ] || other=linux-x86_64
asset="hydra-fleet-$version-$platform"

# The fake remote is this host with its own HOME and none of the local
# Hydra environment. PROVISION_DROP loses the first install's response after
# it ran; PROVISION_UNAME puts a different uname first on the remote PATH.
mkdir "$fixture/bin" "$fixture/uname" "$fixture/assets"
cat > "$fixture/bin/ssh" <<'SSH'
#!/bin/sh
for arg do [ "$arg" = -G ] && exec "$PROVISION_REAL_SSH" "$@"; done
while [ $# -gt 1 ]; do shift; done
printf '%s\n' "$1" >> "$PROVISION_SSH_LOG"
unset HYDRA_HOME HYDRA_FLEET_BIN HYDRA_ROOT HYDRA_BIN_DIR HYDRA_LIB_DIR HYDRA_BIN_CMD HYDRA_FLEET_ASSETS_FILE HYDRA_FLEET_ASSET_BASE
HOME="$PROVISION_REMOTE_HOME"; export HOME
if [ -n "${PROVISION_REMOTE_PATH:-}" ]; then PATH="$PROVISION_REMOTE_PATH"; export PATH; fi
if [ -n "${PROVISION_UNAME:-}" ]; then PATH="$PROVISION_UNAME:$PATH"; export PATH; fi
case "$1" in
    *"install '"*)
        if [ -f "$PROVISION_DROP" ]; then
            rm -f "$PROVISION_DROP"
            /bin/sh -c "$1" >/dev/null 2>&1 || true
            exit 255
        fi
        ;;
esac
exec /bin/sh -c "$1"
SSH
chmod +x "$fixture/bin/ssh"
cat > "$fixture/uname/uname" <<'UNAME'
#!/bin/sh
case "${1:-}" in -m) echo riscv64 ;; *) echo Linux ;; esac
UNAME
chmod +x "$fixture/uname/uname"
cp "$fleet" "$fixture/assets/$asset"
sha="$(shasum -a 256 "$fleet" 2>/dev/null || sha256sum "$fleet")"
sha="${sha%% *}"
printf 'version\tplatform\tsha256\tfilename\n%s\t%s\t%s\t%s\n' "$version" "$platform" "$sha" "$asset" > "$fixture/assets.tsv"
printf 'version\tplatform\tsha256\tfilename\n%s\t%s\t%064d\t%s\n' "$version" "$platform" 0 "$asset" > "$fixture/bad-assets.tsv"
printf 'version\tplatform\tsha256\tfilename\n' > "$fixture/empty-assets.tsv"
PROVISION_REAL_SSH="$(command -v ssh || echo /usr/bin/ssh)"
PATH="$fixture/bin:$PATH"
PROVISION_SSH_LOG="$fixture/ssh.log" PROVISION_DROP="$fixture/drop"
HYDRA_FLEET_BIN="$fleet" HYDRA_FLEET_ASSET_BASE="file://$fixture/assets" HYDRA_FLEET_ASSETS_FILE="$fixture/assets.tsv"
export PATH PROVISION_REAL_SSH PROVISION_SSH_LOG PROVISION_DROP HYDRA_FLEET_BIN HYDRA_FLEET_ASSET_BASE HYDRA_FLEET_ASSETS_FILE

fail() { printf 'FAIL: %s\n' "$1" >&2; exit 1; }
# run EXPECTED_STATUS OUTPUT_FILE ARGS...: hydra remote ARGS through bin/hydra.
run() {
    expected=$1 out=$2; shift 2
    code=0
    "$root/bin/hydra" remote "$@" > "$out" 2> "$out.err" < /dev/null || code=$?
    [ "$code" -eq "$expected" ] || { cat "$out" "$out.err" >&2; fail "remote $* exited $code, expected $expected"; }
}
has() { grep -q -- "$2" "$1" || { cat "$1" >&2; fail "$1 lacks $2"; }; }
lacks() { if grep -q -- "$2" "$1"; then cat "$1" >&2; fail "$1 unexpectedly has $2"; fi; }
exact_mode() { [ -n "$(find "$1" -prune -perm "$2")" ]; }
plan_hash() { sed -n 's/.*"plan_sha256":"\([0-9a-f]*\)".*/\1/p' "$1"; }
installs() { grep -c "install '" "$PROVISION_SSH_LOG" || true; }
state() { printf '%s/fleet/setup/%s.json' "$HYDRA_HOME" "$1"; }
private_dirs() {
    for dir in "$HYDRA_HOME" "$HYDRA_HOME/fleet" "$HYDRA_HOME/fleet/setup"; do
        [ -d "$dir" ] || mkdir -m 700 "$dir"
    done
}
# seed NAME REMOTE_HOME [OS ARCH [HYDRA_PATH [PINS_JSON]]]: host key and
# preflight done, as the setup core records them.
seed() {
    name=$1 home=$2 os=${3:-$local_os} arch=${4:-$local_arch} hydra=${5:-} pins=${6:-[]}
    hydra_version='' reusable=false
    [ -z "$hydra" ] || { hydra_version="Hydra version $version"; reusable=true; }
    mkdir -p "$home"
    private_dirs
    printf '{"schema_version":1,"kind":"remote-setup","name":"%s","destination":"%s-host","ssh_config":"","steps":{"host_key":{"status":"done"},"preflight":{"status":"done","detail":{"os":"%s","arch":"%s","home":"%s","path":"/usr/bin:/bin","umask":"0022","tools":{},"hydra":{"path":"%s","version":"%s","reusable":%s},"pins":%s,"agents":[]}}},"remote":{"target":"%s-host","ssh_config":"","hydra":"hydra","home":"","principal":"","project":"","accepted_host_key":"SHA256:fixture","multiplex":false}}' \
        "$name" "$name" "$os" "$arch" "$home" "$hydra" "$hydra_version" "$reusable" "$pins" "$name" > "$(state "$name")"
    chmod 600 "$(state "$name")"
    : > "$PROVISION_SSH_LOG"
    PROVISION_REMOTE_HOME="$home"; export PROVISION_REMOTE_HOME
}
pins_dir() { printf '%s/.local/share/hydra/fleet' "$1"; }

cases() {
    out="$fixture/out"

    # Provisioning needs the preflight snapshot and never contacts the remote without it.
    private_dirs
    printf '{"schema_version":1,"kind":"remote-setup","name":"p0","destination":"p0-host","ssh_config":"","steps":{"host_key":{"status":"done"}},"remote":{"target":"p0-host","ssh_config":"","hydra":"hydra","home":"","principal":"","project":"","accepted_host_key":"SHA256:fixture","multiplex":false}}' > "$(state p0)"
    chmod 600 "$(state p0)"
    : > "$PROVISION_SSH_LOG"
    run 1 "$out" provision p0 --json; has "$out" '"code":"prerequisite_missing"'
    [ ! -s "$PROVISION_SSH_LOG" ] || fail "provisioning without preflight contacted the remote"

    # A download that differs from the pinned digest is refused and not cached.
    seed p1 "$fixture/remote-$mask-a"
    HYDRA_FLEET_ASSETS_FILE="$fixture/bad-assets.tsv" run 1 "$out" provision p1 --json
    has "$out" '"code":"hash_mismatch"'
    [ -z "$(ls -A "$HYDRA_HOME/fleet/assets")" ] || fail "a mismatching asset was cached"
    [ ! -s "$PROVISION_SSH_LOG" ] || fail "a refused asset reached the remote"

    # A remote with an older pinned install keeps it; the new plan is reviewed
    # before any remote command and binds the pinned release asset.
    old_pin="$(pins_dir "$fixture/remote-$mask-a")/$(printf '%064d' 28)"
    mkdir -p "$old_pin/bin"
    printf 'Hydra 2.8 pin\n' > "$old_pin/bin/hydra"
    run 3 "$out" provision p1 --json
    has "$out" '"code":"approval_required"'; has "$out" '"trust":"pinned"'; has "$out" '"source":"release-asset"'
    has "$out" "\"name\":\"$asset\""; has "$out" "\"platform\":\"$platform\""; has "$out" "\"hydra_version\":\"$version\""
    has "$out" '"prefix":"[^"]*remote-'"$mask"'-a\\/.local\\/share\\/hydra\\/fleet\\/[0-9a-f]\{64\}"'
    has "$out" 'no PATH changes'; has "$out" '"peer_fingerprint":"SHA256:fixture"'
    has "$out" '"argv":\["hydra","remote","provision","p1","--approve","[0-9a-f]\{64\}","--json"\]'
    has "$out" '"id":"provision","status":"approval_required"'
    [ ! -s "$PROVISION_SSH_LOG" ] || fail "the plan contacted the remote before approval"
    hash="$(plan_hash "$out")"
    exact_mode "$HYDRA_HOME/fleet/assets" 700 || fail "asset cache is not private"
    exact_mode "$HYDRA_HOME/fleet/assets/$sha" 600 || fail "cached asset is not 0600"
    run 1 "$out" provision p1 --approve "$(printf '%064d' 1)" --json; has "$out" '"code":"approval_mismatch"'
    [ ! -s "$PROVISION_SSH_LOG" ] || fail "a mismatched approval contacted the remote"
    run 0 "$out" provision p1 --approve "$hash" --json
    has "$out" '"id":"provision","status":"done"'; has "$out" "Hydra $version installed at"
    [ "$(installs)" -eq 1 ] || fail "fresh provisioning did not install exactly once"
    prefix="$(sed -n 's/.*"prefix":"\([^"]*\)".*/\1/p' "$out" | sed 's#\\/#/#g')"
    [ "$(HOME="$fixture/remote-$mask-a" "$prefix/bin/hydra" --version)" = "Hydra version $version" ] || fail "installed version differs"
    [ "$(cat "$old_pin/bin/hydra")" = 'Hydra 2.8 pin' ] || fail "the existing pin changed"
    has "$(state p1)" '"hydra":"[^"]*bin\\/hydra"'
    exact_mode "$HYDRA_HOME/fleet/packages" 700 || fail "package cache is not private"
    for package in "$HYDRA_HOME"/fleet/packages/*.json; do exact_mode "$package" 600 || fail "package cache file is not 0600"; done
    test ! -e "$HYDRA_HOME/fleet/remotes/p1.json" || fail "provisioning published an alias"
    run 0 "$out" provision p1 --json; has "$out" '"id":"provision","status":"done"'
    [ "$(installs)" -eq 1 ] || fail "a done step installed again"
    digest="${prefix##*/}"

    # Same-version Hydra already on the remote is reused without approval.
    seed p2 "$fixture/remote-$mask-a" "$local_os" "$local_arch" "$prefix/bin/hydra"
    run 0 "$out" provision p2 --json
    has "$out" '"reused":true'; has "$out" '"reuse":"existing"'; has "$out" '"id":"provision","status":"done"'
    [ "$(installs)" -eq 0 ] || fail "reuse installed again"
    # An identical pin at the exact prefix is adopted after a read-only check.
    seed p3 "$fixture/remote-$mask-a" "$local_os" "$local_arch" '' "[\"$(pins_dir "$fixture/remote-$mask-a")/$digest\"]"
    run 0 "$out" provision p3 --json
    has "$out" '"reuse":"pin"'
    [ "$(installs)" -eq 0 ] || fail "pin reuse installed again"
    grep -q 'install-check' "$PROVISION_SSH_LOG" || fail "pin reuse skipped the install check"

    # A lost install response is outcome_unknown; the rerun reconciles
    # the recorded prefix and never installs a second time.
    seed p4 "$fixture/remote-$mask-b"
    run 3 "$out" provision p4 --json; hash="$(plan_hash "$out")"
    : > "$PROVISION_DROP"
    run 4 "$out" provision p4 --approve "$hash" --json
    has "$out" '"code":"outcome_unknown"'; has "$out" '"id":"provision","status":"outcome_unknown"'
    [ "$(installs)" -eq 1 ] || fail "the lost response did not come from one install"
    run 0 "$out" provision p4 --approve "$hash" --json
    has "$out" '"id":"provision","status":"done"'
    [ "$(installs)" -eq 1 ] || fail "reconciliation installed a second time"
    grep -q 'install-check' "$PROVISION_SSH_LOG" || fail "the rerun did not reconcile"

    # The same, with the local package cache deleted meanwhile: the rerun
    # rebuilds the identical package from this Hydra and reconciles.
    seed p11 "$fixture/remote-$mask-g"
    run 3 "$out" provision p11 --json; hash="$(plan_hash "$out")"
    : > "$PROVISION_DROP"
    run 4 "$out" provision p11 --approve "$hash" --json
    rm -f "$HYDRA_HOME"/fleet/packages/*.json
    run 0 "$out" provision p11 --json; has "$out" '"id":"provision","status":"done"'
    [ "$(installs)" -eq 1 ] || fail "reconciling a rebuilt package installed a second time"
    grep -q 'install-check' "$PROVISION_SSH_LOG" || fail "the rebuilt package was not reconciled"

    # When this Hydra can no longer rebuild it (here the helper's pinned
    # digest changed) the step stays outcome_unknown and names the prefix;
    # the documented recovery removes it on the remote, and the next reruns
    # record install_failed and then plan afresh.
    seed p12 "$fixture/remote-$mask-h"
    run 3 "$out" provision p12 --json; hash="$(plan_hash "$out")"
    : > "$PROVISION_DROP"
    run 4 "$out" provision p12 --approve "$hash" --json
    rm -f "$HYDRA_HOME"/fleet/packages/*.json
    HYDRA_FLEET_ASSETS_FILE="$fixture/bad-assets.tsv" run 4 "$out" provision p12 --json
    has "$out" '"code":"outcome_unknown"'; has "$out" 'local package is gone'
    stuck="$(sed -n 's/.*"data":{"prefix":"\([^"]*\)".*/\1/p' "$out" | sed 's#\\/#/#g')"
    [ -d "$stuck" ] || fail "outcome_unknown did not name the installed prefix"
    [ "$(installs)" -eq 1 ] || fail "an unconfirmed install was retried"
    rm -rf "$stuck"
    HYDRA_FLEET_ASSETS_FILE="$fixture/bad-assets.tsv" run 1 "$out" provision p12 --json; has "$out" '"code":"install_failed"'
    run 3 "$out" provision p12 --json; has "$out" '"code":"approval_required"'

    # A remote whose uname disagrees with the package is refused by the
    # bootstrap guard before the package binary runs; nothing is installed.
    seed p5 "$fixture/remote-$mask-c"
    run 3 "$out" provision p5 --json; hash="$(plan_hash "$out")"
    PROVISION_UNAME="$fixture/uname" run 1 "$out" provision p5 --approve "$hash" --json
    has "$out" '"code":"platform_mismatch"'; has "$out" '"id":"provision","status":"failed"'
    test ! -e "$(pins_dir "$fixture/remote-$mask-c")" || fail "a mismatched platform created remote files"
    # Failure is definitive: the next attempt needs a fresh approval.
    run 3 "$out" provision p5 --json; has "$out" '"code":"approval_required"'

    # --binary is unpinned and repeated in next.argv; a foreign file is refused.
    run 0 "$out" add p6 other-host
    seed p6 "$fixture/remote-$mask-d"
    printf 'not an executable\n' > "$fixture/not-binary"
    run 1 "$out" provision p6 --binary "$fixture/not-binary" --json; has "$out" '"code":"platform_mismatch"'
    run 3 "$out" provision p6 --binary "$fleet" --json
    has "$out" '"trust":"unpinned"'; has "$out" '"source":"binary"'
    has "$out" '"argv":\["hydra","remote","provision","p6","--binary","[^"]*","--approve","[0-9a-f]\{64\}","--json"\]'
    hash="$(plan_hash "$out")"
    run 0 "$out" provision p6 --binary "$fleet" --approve "$hash" --json; has "$out" '"id":"provision","status":"done"'
    has "$HYDRA_HOME/fleet/remotes/p6.json" '"target":"other-host"'

    # Without a pinned row: the identical local platform uses this host's
    # helper (unpinned); another platform has no asset and names the fix.
    seed p7 "$fixture/remote-$mask-e"
    HYDRA_FLEET_ASSETS_FILE="$fixture/empty-assets.tsv" run 3 "$out" provision p7 --json
    has "$out" '"source":"local-helper"'; has "$out" '"trust":"unpinned"'
    seed p8 "$fixture/remote-$mask-e" "${other%%-*}" "${other#*-}"
    HYDRA_FLEET_ASSETS_FILE="$fixture/empty-assets.tsv" run 1 "$out" provision p8 --json
    has "$out" '"code":"asset_unavailable"'; has "$out" 'make build-fleet-static ARCH='
    seed p9 "$fixture/remote-$mask-e" Linux riscv64
    run 1 "$out" provision p9 --json; has "$out" '"code":"platform_unsupported"'
    [ ! -s "$PROVISION_SSH_LOG" ] || fail "an unavailable asset contacted the remote"

    # The real preflight snapshot feeds provisioning (a macOS remote passes
    # preflight because this host's helper has its platform). The remote PATH
    # excludes any Hydra installed on this host so a fresh pin is planned.
    if [ "$local_os" = Linux ] || [ "$local_os" = Darwin ]; then
        PROVISION_REMOTE_PATH=/usr/bin:/bin; export PROVISION_REMOTE_PATH
        private_dirs
        mkdir -p "$fixture/remote-$mask-f"
        printf '{"schema_version":1,"kind":"remote-setup","name":"p10","destination":"p10-host","ssh_config":"","steps":{"host_key":{"status":"done"}},"remote":{"target":"p10-host","ssh_config":"","hydra":"hydra","home":"","principal":"","project":"","accepted_host_key":"SHA256:fixture","multiplex":false}}' > "$(state p10)"
        chmod 600 "$(state p10)"
        PROVISION_REMOTE_HOME="$fixture/remote-$mask-f"; export PROVISION_REMOTE_HOME
        run 0 "$out" preflight p10 --json
        run 3 "$out" provision p10 --json; hash="$(plan_hash "$out")"
        has "$out" "\"platform\":\"$platform\""
        run 0 "$out" provision p10 --approve "$hash" --json; has "$out" '"id":"provision","status":"done"'
    fi
}

for mask in 022 002; do
    (
        umask "$mask"
        HYDRA_HOME="$fixture/home-$mask"; export HYDRA_HOME
        cases
    )
done
printf 'Remote provisioning acceptance passed\n'
