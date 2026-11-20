#!/bin/sh
# Files Hydra 1.x left in a store are inert: the session map ($HYDRA_HOME/map)
# and the 1.9 compatibility projection (projects/<id>/compat-map). No release
# since 2.0 reads either, and nothing removes them, so a store that still
# holds them must behave exactly as one that does not.
set -u
root="$(CDPATH='' cd -- "$(dirname "$0")/.." && pwd)"
# shellcheck disable=SC1091
. "$root/tests/helpers.sh"
test_count=0 pass_count=0 fail_count=0
HYDRA_BIN="$root/bin/hydra"

fixture="$(test_mktemp_dir)"
cleanup() {
    (cd "$fixture/repo" 2>/dev/null && "$HYDRA_BIN" kill --all --force >/dev/null 2>&1) || true
    rm -rf "$fixture"
}
trap cleanup EXIT
trap 'exit 130' INT
trap 'exit 143' TERM
trap 'exit 129' HUP
export HOME="$fixture"
export HYDRA_HOME="$fixture/home"
export HYDRA_NONINTERACTIVE=1 HYDRA_SKIP_AI=1 HYDRA_NO_SWITCH=1
unset HYDRA_ROOT
(umask 077; mkdir -p "$HYDRA_HOME")
mkdir -p "$fixture/repo" "$fixture/clean" "$fixture/leftover"
cd "$fixture/repo" || exit 1
git init -q
git -c user.name=Test -c user.email=test@example.invalid commit --allow-empty -qm init

echo "Running leftover 1.x file tests..."
echo "=================================="

"$HYDRA_BIN" init --no-agent >/dev/null 2>&1 || { echo "init failed"; exit 1; }
"$HYDRA_BIN" spawn standing --no-agent >/dev/null 2>&1 || { echo "spawn failed"; exit 1; }
project_id="$(sed -n '1p' .git/hydra/project-id)"
project_dir="$HYDRA_HOME/state/v2/projects/$project_id"
old_row='old-branch old-session claude group 123 dep 9'

# Usage: observe <directory>: each read-only command's stdout, stderr and
# status, one file per command. A head's age counts seconds, so the age
# column and the JSON durations are replaced by a fixed word.
observe() {
    for _ob_command in "list" "list --json" "status" "status --json" "snapshot" "snapshot --native" \
        "snapshot --shell" "state verify"; do
        _ob_file="$1/$(printf '%s' "$_ob_command" | tr ' ' '_')"
        # shellcheck disable=SC2086 # the command and its arguments are separate words
        "$HYDRA_BIN" $_ob_command > "$_ob_file.out" 2> "$_ob_file.err"
        printf '%s\n' "$?" > "$_ob_file.status"
        sed -e 's/[0-9][0-9]*[smhd]\( [0-9][0-9]*[smhd]\)*$/AGE/' \
            -e 's/([0-9][0-9]*[smhd]\( [0-9][0-9]*[smhd]\)*)/(AGE)/g' \
            -e 's/"duration_seconds": *[0-9]*/"duration_seconds": AGE/g' \
            -e 's/"duration_human": *"[^"]*"/"duration_human": "AGE"/g' \
            "$_ob_file.out" > "$_ob_file.out.normal"
        mv "$_ob_file.out.normal" "$_ob_file.out"
    done
}

# Usage: spawn_and_kill <directory> <branch>: a spawn and a kill with the
# branch name replaced, so two runs can be compared.
spawn_and_kill() {
    "$HYDRA_BIN" spawn "$2" --no-agent > "$1/spawn.out" 2> "$1/spawn.err"
    printf '%s\n' "$?" > "$1/spawn.status"
    "$HYDRA_BIN" path "$2" > "$1/path.out" 2> "$1/path.err"
    printf '%s\n' "$?" > "$1/path.status"
    "$HYDRA_BIN" kill "$2" --force > "$1/kill.out" 2> "$1/kill.err"
    printf '%s\n' "$?" > "$1/kill.status"
    for _sk_file in "$1"/spawn.out "$1"/spawn.err "$1"/path.out "$1"/kill.out "$1"/kill.err; do
        sed -e "s/$2/BRANCH/g" -e 's/head_[0-9a-f]*/HEAD/g' -e 's/instance_[0-9a-f]*/INSTANCE/g' \
            "$_sk_file" > "$_sk_file.normal"
        mv "$_sk_file.normal" "$_sk_file"
    done
}

observe "$fixture/clean"
assert_equal 0 "$(cat "$fixture/clean/state_verify.status")" "state verify accepts the store before any leftover is added"
assert_equal "State is valid" "$(cat "$fixture/clean/state_verify.out")" "state verify reports a valid store"
assert_equal "" "$(cat "$fixture/clean/snapshot_--native.err")" "the native helper answers the snapshot of the clean store"
case "$(cat "$fixture/clean/list.out")" in
    *standing*) assert_success 0 "the standing head is listed" ;;
    *) assert_success 1 "the standing head is listed"; cat "$fixture/clean/list.out" ;;
esac

printf '%s\n' "$old_row" > "$HYDRA_HOME/map"
printf '%s\n' "$old_row" > "$project_dir/compat-map"
observe "$fixture/leftover"
for observed in "$fixture"/clean/*; do
    name="${observed##*/}"
    if cmp -s "$observed" "$fixture/leftover/$name"; then
        assert_success 0 "$name is the same with a leftover map and compat-map"
    else
        assert_success 1 "$name is the same with a leftover map and compat-map"
        diff "$observed" "$fixture/leftover/$name" | sed 's/^/  /'
    fi
done

# A spawn, the path of the new head and its removal, with the leftovers and
# then without them.
mkdir "$fixture/spawn-leftover" "$fixture/spawn-clean"
spawn_and_kill "$fixture/spawn-leftover" with-leftovers
assert_equal 0 "$(cat "$fixture/spawn-leftover/spawn.status")" "spawn succeeds beside a leftover map and compat-map"
assert_equal "$old_row" "$(sed -n '1p' "$HYDRA_HOME/map")" "the leftover map is left as it was"
assert_equal "$old_row" "$(sed -n '1p' "$project_dir/compat-map")" "the leftover compat-map is left as it was"
rm -f "$HYDRA_HOME/map" "$project_dir/compat-map"
spawn_and_kill "$fixture/spawn-clean" none-left
for observed in "$fixture"/spawn-clean/*; do
    name="${observed##*/}"
    if cmp -s "$observed" "$fixture/spawn-leftover/$name"; then
        assert_success 0 "$name is the same with a leftover map and compat-map"
    else
        assert_success 1 "$name is the same with a leftover map and compat-map"
        diff "$observed" "$fixture/spawn-leftover/$name" | sed 's/^/  /'
    fi
done

printf 'Tests: %s, Passed: %s, Failed: %s\n' "$test_count" "$pass_count" "$fail_count"
[ "$fail_count" -eq 0 ]
