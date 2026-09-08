#!/bin/sh
# Public exec workers must consume host reservations before invoking commands.
set -eu
ROOT="$(CDPATH='' cd -- "$(dirname "$0")/.." && pwd)"
fixture="$(mktemp -d)"
export HYDRA_HOME="$fixture/home" HYDRA_NONINTERACTIVE=1 HYDRA_SKIP_AI=1
CLI="$ROOT/bin/hydra"
cleanup() {
    _test_status=$?
    if [ "$_test_status" -ne 0 ]; then
        "$CLI" admission status --json >&2 || true
        [ ! -f "$fixture/released" ] || cat "$fixture/released" >&2
    fi
    for repo in "$fixture/a" "$fixture/b"; do
        [ -d "$repo/.git" ] || continue
        (cd "$repo" && "$CLI" kill --all --force) >/dev/null 2>&1 || true
    done
    rm -rf "$fixture"
}
trap cleanup 0
trap 'exit 130' INT
trap 'exit 143' TERM
trap 'exit 129' HUP
for name in a b; do
    mkdir "$fixture/$name"
    (
        cd "$fixture/$name"
        git init -q
        git -c user.name=Test -c user.email=test@example.invalid -c commit.gpgSign=false commit --allow-empty -qm initial
        "$CLI" init --no-agent --trust >/dev/null
        "$CLI" spawn "admission-$name" --no-agent >/dev/null
    )
done
cat > "$fixture/work" <<'EOF'
#!/bin/sh
set -eu
if ! mkdir "$1/active"; then touch "$1/overbooked"; exit 1; fi
printf 'start\n' >> "$1/events"
sleep 1
printf 'end\n' >> "$1/events"
rmdir "$1/active"
EOF
"$CLI" admission configure 1 1 0 8 - >/dev/null
(cd "$fixture/a" && "$CLI" exec --branch admission-a --json -- sh "$fixture/work" "$fixture") > "$fixture/result-a" &
first=$!
(cd "$fixture/b" && "$CLI" exec --branch admission-b --json -- sh "$fixture/work" "$fixture") > "$fixture/result-b" &
second=$!
wait "$first"
wait "$second"
[ ! -e "$fixture/overbooked" ]
[ "$(cat "$fixture/events")" = "$(printf 'start\nend\nstart\nend')" ]
"$CLI" admission status --json > "$fixture/status"
grep -q '"reserved":0,"queued":0' "$fixture/status"

# A queue deadline prevents command execution, independently of exec timeout.
"$CLI" admission request blocker manual 60 - >/dev/null
if (cd "$fixture/a" && HYDRA_ADMISSION_QUEUE_SECONDS=1 "$CLI" exec --branch admission-a --exit-code --json -- touch "$fixture/should-not-run") > "$fixture/expired"; then exit 1; fi
[ ! -e "$fixture/should-not-run" ]
grep -q queue_deadline "$fixture/expired"

# Cancel a queued CLI owner before it starts the command.
(cd "$fixture/a" && exec "$CLI" exec --branch admission-a --json -- touch "$fixture/should-not-run") > "$fixture/cancelled" &
pending=$!
tries=0
while :; do
    "$CLI" admission status --json > "$fixture/status"
    if grep -q '"queued":1' "$fixture/status"; then break; fi
    tries=$((tries + 1)); [ "$tries" -lt 50 ]
    sleep 0.1
done
kill -TERM "$pending"
if wait "$pending"; then exit 1; fi
tries=0
while :; do
    "$CLI" admission status --json > "$fixture/status"
    if grep -q '"reserved":1,"queued":0' "$fixture/status"; then break; fi
    tries=$((tries + 1)); [ "$tries" -lt 50 ]
    sleep 0.1
done
[ ! -e "$fixture/should-not-run" ]

# Required host labels are checked before any command effects.
"$CLI" admission release blocker --confirmed > "$fixture/released"
if (cd "$fixture/a" && HYDRA_ADMISSION_LABELS=gpu "$CLI" exec --branch admission-a --exit-code --json -- touch "$fixture/should-not-run") > "$fixture/refused"; then exit 1; fi
[ ! -e "$fixture/should-not-run" ]
grep -q capability_unavailable "$fixture/refused"
"$CLI" admission status --json > "$fixture/status"
grep -q '"reserved":0,"queued":0' "$fixture/status"

# Losing the worker after its command starts cannot free its slot. The command
# writes an end marker so this fixture can explicitly reconcile it afterwards.
# shellcheck disable=SC2016
(cd "$fixture/a" && exec "$CLI" exec --branch admission-a --json -- sh -c 'touch "$1"; sleep 2; touch "$2"' worker "$fixture/started" "$fixture/ended") > "$fixture/lost-owner" &
owner=$!
tries=0
while [ ! -f "$fixture/started" ]; do
    tries=$((tries + 1)); [ "$tries" -lt 50 ]; sleep 0.1
done
project="$(cat "$fixture/a/.git/hydra/project-id")"
run="$(sed -n 's/.*"run_id":"\([^"]*\)".*/\1/p' "$fixture/lost-owner")"
[ -n "$run" ]
worker="$(cat "$HYDRA_HOME/state/v2/projects/$project/exec/$run/worker-pids")"
kill -KILL "$worker"
if wait "$owner"; then exit 1; fi
tries=0
while [ ! -f "$fixture/ended" ]; do
    tries=$((tries + 1)); [ "$tries" -lt 50 ]; sleep 0.1
done
"$CLI" admission status --json > "$fixture/status"
grep -q '"reserved":1,"queued":0' "$fixture/status"
for record in "$HYDRA_HOME/admission/$run"-*.request; do
    request="${record##*/}"; request="${request%.request}"
    "$CLI" admission unknown "$request" >/dev/null
    "$CLI" admission release "$request" --confirmed >/dev/null
done
printf 'Passed real exec admission, release, queue deadlines/cancellation, labels, and lost-owner retention\n'
