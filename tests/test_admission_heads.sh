#!/bin/sh
# Head creation/resume and verification gates share the receiving host's slots.
set -eu
ROOT="$(CDPATH='' cd -- "$(dirname "$0")/.." && pwd)"
fixture="$(mktemp -d)"
CLI="$ROOT/bin/hydra"
export HYDRA_HOME="$fixture/home" HYDRA_NONINTERACTIVE=1 HYDRA_NO_SWITCH=1
cleanup() {
    "$CLI" kill --all --force >/dev/null 2>&1 || true
    rm -rf "$fixture"
}
trap cleanup 0
trap 'exit 130' INT
trap 'exit 143' TERM
trap 'exit 129' HUP
mkdir "$fixture/repo"
cd "$fixture/repo"
git init -q
git -c user.name=Test -c user.email=test@example.invalid -c commit.gpgSign=false commit --allow-empty -qm initial
"$CLI" init --no-agent --trust >/dev/null
"$CLI" admission configure 1 1 0 8 - >/dev/null
"$CLI" spawn admission-plain --no-agent >/dev/null
"$CLI" admission status > "$fixture/status"
grep -q '"reserved":0,"queued":0' "$fixture/status"
"$CLI" gate run admission-plain --name smoke -- touch "$fixture/gate-ran" >/dev/null
[ -f "$fixture/gate-ran" ]
"$CLI" admission status > "$fixture/status"
grep -q '"reserved":0,"queued":0' "$fixture/status"

"$CLI" admission request blocker manual 60 - >/dev/null
if HYDRA_ADMISSION_QUEUE_SECONDS=1 "$CLI" spawn admission-blocked --no-agent > "$fixture/blocked" 2>&1; then exit 1; fi
if git show-ref --verify --quiet refs/heads/admission-blocked; then exit 1; fi
grep -q queue_deadline "$fixture/blocked"
if HYDRA_ADMISSION_QUEUE_SECONDS=1 "$CLI" gate run admission-plain --name blocked -- touch "$fixture/gate-blocked" > "$fixture/blocked-gate" 2>&1; then exit 1; fi
[ ! -e "$fixture/gate-blocked" ]
plain_path="$("$CLI" path admission-plain)"
"$CLI" kill admission-plain --force >/dev/null
if HYDRA_ADMISSION_QUEUE_SECONDS=1 "$CLI" resume admission-plain > "$fixture/blocked-resume" 2>&1; then exit 1; fi
[ ! -d "$plain_path" ]
"$CLI" admission release blocker --confirmed >/dev/null
"$CLI" resume admission-plain >/dev/null
"$CLI" admission status > "$fixture/status"
grep -q '"reserved":0,"queued":0' "$fixture/status"

# A real fixture agent occupies its instance slot until explicit teardown.
cat > "$fixture/agent" <<EOF
#!/bin/sh
touch "$fixture/agent-ran"
exec sleep 60
EOF
chmod +x "$fixture/agent"
"$CLI" agent init admission-fixture --executable "$fixture/agent" >/dev/null
# Custom profile files are the documented declarative profile interface.
printf 'cwd-last\n' > "$HYDRA_HOME/profiles/admission-fixture/resume_mode"
HYDRA_SKIP_AI='' "$CLI" spawn admission-agent --profile admission-fixture >/dev/null
tries=0
while [ ! -e "$fixture/agent-ran" ]; do
    tries=$((tries + 1)); [ "$tries" -lt 50 ]; sleep 0.1
done
"$CLI" admission status > "$fixture/status"
grep -q '"reserved":1,"queued":0' "$fixture/status"
if HYDRA_ADMISSION_QUEUE_SECONDS=1 "$CLI" exec --branch admission-plain --exit-code -- touch "$fixture/overbooked" > "$fixture/blocked-exec" 2>&1; then exit 1; fi
[ ! -e "$fixture/overbooked" ]
"$CLI" kill admission-agent --force >/dev/null
"$CLI" admission status > "$fixture/status"
grep -q '"reserved":0,"queued":0' "$fixture/status"
"$CLI" resume admission-agent >/dev/null
"$CLI" admission status > "$fixture/status"
grep -q '"reserved":1,"queued":0' "$fixture/status"
"$CLI" kill admission-agent --force >/dev/null

# The existing spawn queue dispatches through exactly the same reservation path.
"$CLI" kill admission-plain --force >/dev/null
# Enqueue uses an interactive prompt; seed with its existing helper, then test
# the public queue processor and real spawned head without automating a prompt.
# shellcheck disable=SC1090,SC1091
enqueue() (
    . "$ROOT/lib/locks.sh"
    . "$ROOT/lib/identity.sh"
    . "$ROOT/lib/state_v2.sh"
    . "$ROOT/lib/limits.sh"
    queue_spawn "$1" none '' default 50 >/dev/null
)
enqueue admission-queued
"$CLI" queue process > "$fixture/queue"
"$CLI" path admission-queued >/dev/null
enqueue admission-after-kill
"$CLI" kill admission-queued --force >/dev/null
"$CLI" path admission-after-kill >/dev/null
"$CLI" admission status > "$fixture/status"
grep -q '"reserved":0,"queued":0' "$fixture/status"

# Both callers pass their preflight before admission. Exactly one may create
# or resume the head after the waiting period.
for action in spawn resume; do
    "$CLI" admission request "blocker-$action" manual 60 - >/dev/null
    if [ "$action" = spawn ]; then set -- spawn admission-race --no-agent; else set -- resume admission-race; fi
    "$CLI" "$@" > "$fixture/race-one" 2>&1 &
    first=$!
    "$CLI" "$@" > "$fixture/race-two" 2>&1 &
    second=$!
    tries=0
    while :; do
        "$CLI" admission status > "$fixture/status"
        if grep -q '"queued":2' "$fixture/status"; then break; fi
        tries=$((tries + 1)); [ "$tries" -lt 50 ]; sleep 0.1
    done
    "$CLI" admission release "blocker-$action" --confirmed >/dev/null
    wins=0
    if wait "$first"; then wins=$((wins + 1)); fi
    if wait "$second"; then wins=$((wins + 1)); fi
    [ "$wins" -eq 1 ]
    [ "$(git worktree list --porcelain | grep -c '^branch refs/heads/admission-race$')" -eq 1 ]
    "$CLI" admission status > "$fixture/status"
    grep -q '"reserved":0,"queued":0' "$fixture/status"
    "$CLI" kill admission-race --force >/dev/null
done

# An installed copy invoked relatively must use the same home after gate chdir.
mkdir "$fixture/bin"
cp "$CLI" "$fixture/bin/hydra"
relative_cli() { HYDRA_ROOT="$ROOT" HYDRA_HOME=../relative-home ../bin/hydra "$@"; }
relative_cli init --no-agent --trust >/dev/null
relative_cli admission configure 1 1 0 8 - >/dev/null
relative_cli spawn admission-relative --no-agent >/dev/null
relative_cli gate run admission-relative --name relative -- touch "$fixture/relative-ran" >/dev/null
[ -f "$fixture/relative-ran" ]
relative_cli admission status > "$fixture/status"
grep -q '"reserved":0,"queued":0' "$fixture/status"
relative_cli kill --all --force >/dev/null
printf 'Passed head/gate admission, spawn queue paths, and concurrent create/resume revalidation\n'
