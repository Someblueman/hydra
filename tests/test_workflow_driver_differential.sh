#!/bin/sh
# Differential check of the native run driver, the default, against the shell
# driver (HYDRA_NATIVE_DRIVER=0). Every fixture runs once with each driver, in the same
# directory and with fixed Git dates, and the two run records must agree once
# times, PIDs, generated identities and temporary names are normalized: the
# same files with the same modes and contents, the same exit status, and the
# same events (in order at parallelism 1, as a multiset at higher
# parallelism, where workers interleave).
#
# HYDRA_TEST_DIFF_SIZES (default "5 20 64") selects the plan sizes and
# HYDRA_TEST_DIFF_CASES (default all) a space-separated subset of fixtures.
set -u
REPO="$(CDPATH='' cd -- "$(dirname "$0")/.." && pwd)"
HYDRA_BIN="$REPO/bin/hydra"
: "${HYDRA_FLEET_BIN:?HYDRA_FLEET_BIN is required: run via make test-driver-differential}"
test_count=0
pass_count=0
fail_count=0
# shellcheck disable=SC1091
. "$REPO/tests/helpers.sh"
ROOT="$(mktemp -d "${TMPDIR:-/tmp}/hydra-diff.XXXXXX")"
ROOT="$(cd "$ROOT" && pwd -P)"
CASE="$ROOT/case"
OUT="$ROOT/out"
mkdir -p "$OUT" "$ROOT/bin"
export HYDRA_NONINTERACTIVE=1 HYDRA_SKIP_AI=1 HYDRA_NO_SWITCH=1
# Commits made by fixtures and by the run itself get the same identities.
export GIT_AUTHOR_DATE='2026-01-01T00:00:00Z' GIT_COMMITTER_DATE='2026-01-01T00:00:00Z'

# Every driver hand-off is logged, so a case also proves which driver ran.
DIFF_FLEET_REAL="$HYDRA_FLEET_BIN" DIFF_FLEET_LOG="$ROOT/fleet.log"
export DIFF_FLEET_REAL DIFF_FLEET_LOG
cat > "$ROOT/bin/hydra-fleet" <<'EOF'
#!/bin/sh
if [ "${1:-}" = workflow-drive ] && [ "${2:-}" != --capability ]; then printf '%s\n' "$*" >> "$DIFF_FLEET_LOG"; fi
exec "$DIFF_FLEET_REAL" "$@"
EOF
chmod +x "$ROOT/bin/hydra-fleet"
HYDRA_FLEET_BIN="$ROOT/bin/hydra-fleet"
export HYDRA_FLEET_BIN

case_cleanup() {
    if [ -d "$CASE/repo/.git" ]; then
        (cd "$CASE/repo" && HYDRA_HOME="$CASE/home" "$HYDRA_BIN" kill --all --force) >/dev/null 2>&1 || true
    fi
    for diff_source in "$CASE"/hydra-task.*/source; do
        [ -d "$diff_source/.git" ] || continue
        (cd "$diff_source" && HYDRA_HOME="${diff_source%/source}/home" "$HYDRA_BIN" kill --all --force) >/dev/null 2>&1 || true
    done
    for diff_workspace in "$CASE"/hydra-task.*/home/fleet/tasks/task_*/workspace; do
        [ -f "$diff_workspace/.git/hydra/project-id" ] || continue
        (cd "$diff_workspace" && HYDRA_HOME="${diff_workspace%/fleet/tasks/*}" "$HYDRA_BIN" kill --all --force) >/dev/null 2>&1 || true
    done
    test_stop_processes_naming "$CASE"
    rm -rf "$CASE"
}
cleanup() {
    case_cleanup
    # HYDRA_TEST_DIFF_OUT keeps the compared records outside the test's directory.
    [ -z "${HYDRA_TEST_DIFF_OUT:-}" ] || cp -R "$OUT" "$HYDRA_TEST_DIFF_OUT"
    if [ "${HYDRA_TEST_KEEP:-0}" = 1 ]; then printf 'Preserved differential records: %s\n' "$ROOT" >&2; else rm -rf "$ROOT"; fi
}
trap cleanup EXIT
trap 'exit 130' INT
trap 'exit 143' TERM
trap 'exit 129' HUP

wanted() {
    case " ${HYDRA_TEST_DIFF_CASES:-all} " in *" all "*|*" $1 "*) return 0 ;; esac
    return 1
}

run_dir_for() {
    find "$1/state/v2/projects" -type d -path "*/workflows/runs/$2" -print 2>/dev/null | sed -n '1p'
}

# Times, PIDs, generated identities, temporary names and this harness's
# directory, which differ between any two runs of one fixture.
normalize() {
    sed -E \
        -e "s|$ROOT|<ROOT>|g" \
        -e 's/(project|head|instance|run|step|evt|claim|pack|task)_[0-9a-f]{20}/\1_<ID>/g' \
        -e 's/[0-9]{4}-[0-9]{2}-[0-9]{2}T[0-9]{2}:[0-9]{2}:[0-9]{2}(\.[0-9]+)?Z/<TIME>/g' \
        -e 's/(^|[^0-9])1[0-9]{9}([0-9]{3})?([^0-9]|$)/\1<EPOCH>\3/g' \
        -e 's/(^|[^0-9])1[0-9]{9}([0-9]{3})?([^0-9]|$)/\1<EPOCH>\3/g' \
        -e 's/\.(hydra-tmp|request|decision)\.[A-Za-z0-9]{6}/.\1.<TMP>/g' \
        -e 's/(previous_owner|binding)=[0-9a-f]+/\1=<X>/g' \
        -e 's/hydra-task\.[A-Za-z0-9]{6}/hydra-task.<TMP>/g' \
        -e 's/(^|[/])tmp\.[A-Za-z0-9]{10}/\1tmp.<TMP>/g'
}

# Content that is a function of the run's identities rather than of what
# the driver did: digests over records that name generated IDs.
identity_bound() {
    case "$1" in
        */binding-hash|*/binding.tsv|*/remote/*|*/task-initialization.json|*/tasks.json|*/schedule.jsonl|*/data-preparation.json|*/data-seal.json) return 0 ;;
    esac
    return 1
}

# Output of a command that a fixture kills, whose last lines depend on how
# far it got before the signal: listed by the fixture in $CASE/volatile.
volatile() { [ -f "$CASE/volatile" ] && grep -qxF "$1" "$CASE/volatile"; }

snapshot_content() {
    case "$1" in
        *-pid|*/residual-children.tsv) sed 's/[0-9][0-9]*/<PID>/g' "$1" ;;
        # A receipt embeds the remote task's runtime as it was when the
        # submission was acknowledged, which is the remote side's timing.
        */remote/receipt.json) printf '<REMOTE RECEIPT>\n' ;;
        # Remote task evidence carries the task owner's measurements and times.
        */remote/*) sed -E 's/[0-9a-f]{40,64}/<DIGEST>/g; s/":-?[0-9]+/":<N>/g; s/"hex":"[0-9a-f]*"/"hex":"<HEX>"/g' "$1" ;;
        *)
            if volatile "$1"; then printf '<VOLATILE>\n'
            elif identity_bound "$1"; then sed -E 's/[0-9a-f]{40,64}/<DIGEST>/g' "$1"
            else cat "$1"
            fi
            ;;
    esac
}

snapshot_tree() (
    cd "$1" || exit 1
    {
        find . -type d -perm 700 -print | sed 's/^/dir 0700 /'
        find . -type d ! -perm 700 -exec ls -ld {} + | awk '{ print "dir " $1 " " $NF }'
        find . -type f -perm 600 -print | sed 's/^/file 0600 /'
        find . -type f ! -perm 600 -exec ls -ld {} + | awk '{ print "file " $1 " " $NF }'
        find . -type l -print | sed 's/^/link /'
    } | normalize | LC_ALL=C sort
    find . -type f ! -name events.jsonl -print | LC_ALL=C sort | while IFS= read -r diff_file; do
        printf '== %s\n' "$diff_file"
        snapshot_content "$diff_file"
        printf '\n'
    done | normalize
)

# The drivers that drove a run, one word per drive, as run.running records them.
run_drivers() {
    sed -n 's/.*"type":"run.running","detail":"driver=\([a-z]*\)".*/\1/p' "$1/events.jsonl" | LC_ALL=C sort -u | tr '\n' ' '
}

# Events without their times and without the name of the driver, which
# run_case checks. At parallelism above 1 workers interleave, so only each
# step's events are compared, as a multiset without sequences.
snapshot_events() {
    sed -E -e 's/"occurred_at":"[^"]*",//' -e 's/("type":"run.running","detail":"driver=)(shell|native)"/\1<DRIVER>"/' "$1/events.jsonl" |
        normalize > "$2.ordered"
    if [ "$3" = order ]; then
        cp "$2.ordered" "$2"
    else
        sed -E 's/"sequence":[0-9]+,//' "$2.ordered" | LC_ALL=C sort > "$2"
    fi
    # Both drivers number every event once, without gaps.
    sed -n 's/^{"schema_version":1,"sequence":\([0-9]*\),.*/\1/p' "$1/events.jsonl" |
        awk '$1 != NR { bad = 1 } END { exit bad || NR == 0 }'
}

snapshot_runs() {
    _sr_index=0
    : > "$3.drivers"
    while IFS= read -r _sr_run; do
        [ -n "$_sr_run" ] || continue
        _sr_index=$((_sr_index + 1))
        _sr_dir="$(run_dir_for "$2" "$_sr_run")"
        if [ -z "$_sr_dir" ]; then printf 'missing run %s\n' "$_sr_run" > "$3.run$_sr_index.tree"; continue; fi
        snapshot_tree "$_sr_dir" > "$3.run$_sr_index.tree"
        run_drivers "$_sr_dir" >> "$3.drivers"
        snapshot_events "$_sr_dir" "$3.run$_sr_index.events" "$4" || printf 'sequence gap\n' >> "$3.run$_sr_index.events"
    done < "$1"
    printf '%s\n' "$_sr_index" > "$3.runs"
}

# Usage: run_case <name> <order|multiset> <function>
# The function runs in $CASE, writes the IDs of the runs it made to
# $CASE/runs, and returns 0 when the fixture behaved as its test expects.
run_case() {
    _rc_name="$1" _rc_events="$2" _rc_function="$3"
    wanted "$_rc_name" || return 0
    for _rc_driver in shell native; do
        case_cleanup
        mkdir -p "$CASE"
        : > "$CASE/runs"
        : > "$DIFF_FLEET_LOG"
        HYDRA_HOME="$CASE/home"
        export HYDRA_HOME
        # The native driver is the default: it needs no setting.
        unset HYDRA_NATIVE_DRIVER
        if [ "$_rc_driver" = shell ]; then HYDRA_NATIVE_DRIVER=0; export HYDRA_NATIVE_DRIVER; fi
        _rc_started="$(date +%s)"
        (cd "$CASE" && "$_rc_function") > "$OUT/$_rc_name.$_rc_driver.log" 2>&1
        printf '%s\n' "$?" > "$OUT/$_rc_name.$_rc_driver.status"
        printf '%s %s %ss\n' "$_rc_name" "$_rc_driver" "$(($(date +%s) - _rc_started))" >> "$OUT/durations"
        _rc_handoffs="$(wc -l < "$DIFF_FLEET_LOG" | tr -d ' ')"
        if [ "$_rc_driver" = shell ]; then
            assert_equal 0 "$_rc_handoffs" "$_rc_name: the shell driver drives every run with HYDRA_NATIVE_DRIVER=0"
        else
            if [ "$_rc_handoffs" -gt 0 ]; then _rc_used=0; else _rc_used=1; fi
            assert_success "$_rc_used" "$_rc_name: the native driver drives the runs by default"
        fi
        _rc_home="$CASE/home"
        [ ! -f "$CASE/home-path" ] || _rc_home="$(cat "$CASE/home-path")"
        snapshot_runs "$CASE/runs" "$_rc_home" "$OUT/$_rc_name.$_rc_driver" "$_rc_events"
        assert_equal "$_rc_driver" "$(LC_ALL=C sort -u "$OUT/$_rc_name.$_rc_driver.drivers" | tr -d ' \n')" \
            "$_rc_name: every drive of every run records the $_rc_driver driver"
    done
    assert_equal 0 "$(cat "$OUT/$_rc_name.shell.status")" "$_rc_name: the fixture behaves as expected with the shell driver"
    assert_equal 0 "$(cat "$OUT/$_rc_name.native.status")" "$_rc_name: the fixture behaves as expected with the native driver"
    _rc_count="$(cat "$OUT/$_rc_name.shell.runs")"
    assert_equal "$_rc_count" "$(cat "$OUT/$_rc_name.native.runs")" "$_rc_name: both drivers record the same runs"
    _rc_index=1
    while [ "$_rc_index" -le "$_rc_count" ]; do
        for _rc_part in tree events; do
            _rc_left="$OUT/$_rc_name.shell.run$_rc_index.$_rc_part"
            _rc_right="$OUT/$_rc_name.native.run$_rc_index.$_rc_part"
            if diff -u "$_rc_left" "$_rc_right" > "$OUT/$_rc_name.run$_rc_index.$_rc_part.diff"; then
                _rc_same=0
            else
                _rc_same=1
                head -n 60 "$OUT/$_rc_name.run$_rc_index.$_rc_part.diff"
            fi
            assert_success "$_rc_same" "$_rc_name: run $_rc_index has the same $_rc_part with both drivers"
            if [ "$_rc_part" = events ]; then
                if grep -q '^sequence gap$' "$_rc_left" "$_rc_right"; then _rc_gap=1; else _rc_gap=0; fi
                assert_success "$_rc_gap" "$_rc_name: run $_rc_index numbers its events 1, 2, ... with both drivers"
            fi
        done
        _rc_index=$((_rc_index + 1))
    done
    printf 'DIFFERENTIAL %s: %s run(s), %s events\n' "$_rc_name" "$_rc_count" "$_rc_events"
}

new_repo() {
    mkdir -p "$CASE/repo"
    cd "$CASE/repo" || return 1
    git init -q && git config user.name Test && git config user.email test@example.invalid || return 1
    printf 'base\n' > tracked.txt
    git add . && git commit -qm base || return 1
    "$HYDRA_BIN" init --no-agent --trust >/dev/null
}

new_head_repo() {
    new_repo || return 1
    "$HYDRA_BIN" spawn diff-head --headless --no-agent >/dev/null 2>&1
}

# Usage: write_plan <n> <chain|wide|layers> <parallelism>
# A schema-1 plan of n steps: a headless spawn, n-3 work steps in the given
# shape, then compose and verify, as the planning benchmarks use.
write_plan() {
    _wp_n="$1" _wp_shape="$2" _wp_par="$3" _wp_works=$(($1 - 3))
    _wp_prev='"spawn"' _wp_layer_prev='"spawn"' _wp_layer='' _wp_all='' _wp_i=1
    {
        printf '{"schema_version":1,"id":"diff-%s-%s","objective":"Driver differential plan","context":[],"assumptions":[],"questions":[],' "$_wp_shape" "$_wp_n"
        printf '"envelope":{"hosts":["local"],"tools":["sh"],"effects":["worktree","execute"],"writes":[],"parallelism":%s,"timeout_seconds":%s,"artifact_bytes":1048576,"max_heads":1,"disk_mb":1,"retry_budget":0,"repair_budget":0},' "$_wp_par" "$((30 * _wp_n))"
        printf '"steps":[{"id":"spawn","role":"work","kind":"spawn","needs":[],"writes":[],"args":{"branch":"diff-head","terminal_mode":"headless"}}'
        while [ "$_wp_i" -le "$_wp_works" ]; do
            case "$_wp_shape" in
                chain) _wp_needs="$_wp_prev"; _wp_prev="\"w$_wp_i\"" ;;
                wide) _wp_needs='"spawn"' ;;
                layers)
                    _wp_needs="$_wp_layer_prev"
                    _wp_layer="${_wp_layer:+$_wp_layer,}\"w$_wp_i\""
                    if [ $((_wp_i % 4)) -eq 0 ] || [ "$_wp_i" -eq "$_wp_works" ]; then _wp_layer_prev="$_wp_layer"; _wp_layer=''; fi
                    ;;
            esac
            _wp_all="${_wp_all:+$_wp_all,}\"w$_wp_i\""
            printf ',{"id":"w%s","role":"work","kind":"exec","needs":[%s],"writes":[],"args":{"head":"diff-head","argv":["sh","work.sh","w%s"],"timeout":30}}' "$_wp_i" "$_wp_needs" "$_wp_i"
            _wp_i=$((_wp_i + 1))
        done
        case "$_wp_shape" in chain) _wp_last="$_wp_prev" ;; wide) _wp_last="$_wp_all" ;; *) _wp_last="$_wp_layer_prev" ;; esac
        printf ',{"id":"compose","role":"compose","kind":"exec","needs":[%s],"writes":[],"args":{"head":"diff-head","argv":["sh","compose.sh"],"timeout":30}}' "$_wp_last"
        printf ',{"id":"verify","role":"verify","kind":"exec","needs":["compose"],"writes":[],"args":{"head":"diff-head","argv":["sh","check.sh"],"timeout":30}}],'
        printf '"deliverables":[{"id":"report","description":"Final report","step":"compose","output":"report","destination":"run-artifact"}],'
        printf '"checks":[{"id":"check","method":"executable","definition":"Compare expected text","step":"verify","input":"subject","report":"check","deliverable":"report"}],'
        printf '"requirements":[{"id":"content","criterion":"Report has the expected contents","deliverable":"report","check":"check"}],'
        printf '"data":{"schema_version":1,"inputs":{"expected":{"path":"expected.txt","type":"file","max_bytes":128}},"steps":{'
        printf '"compose":{"outputs":{"report":{"type":"file","path":"report.txt","max_bytes":1024}}},'
        printf '"verify":{"inputs":{"subject":{"step":"compose","output":"report"},"expected":{"input":"expected"}},"outputs":{"check":{"type":"object","path":"check.json","max_bytes":2048}}}}}}\n'
    } > "$CASE/plan.json"
    printf '{"schema_version":1,"envelope":{"hosts":["local"],"tools":["sh"],"effects":["worktree","execute"],"writes":[],"parallelism":16,"timeout_seconds":86400,"artifact_bytes":1048576,"max_heads":4,"disk_mb":1,"retry_budget":0,"repair_budget":0}}\n' > "$CASE/policy.json"
}

plan_case() {
    mkdir -p "$CASE/repo"
    cp "$REPO/tests/fixtures/plan/repo/compose.sh" "$REPO/tests/fixtures/plan/repo/check.sh" "$REPO/tests/fixtures/plan/repo/expected.txt" "$CASE/repo/"
    printf '#!/bin/sh\nexit 0\n' > "$CASE/repo/work.sh"
    cd "$CASE/repo" || return 1
    git init -q && git config user.name Test && git config user.email test@example.invalid || return 1
    git add . && git commit -qm fixture || return 1
    "$HYDRA_BIN" init --no-agent --trust >/dev/null || return 1
    write_plan "$PLAN_N" "$PLAN_SHAPE" "$PLAN_PAR"
    "$HYDRA_BIN" workflow plan compile "$CASE/plan.json" "$CASE/policy.json" "$CASE/compiled.json" > "$CASE/compile.json" || return 1
    _pc_digest="$(sed -n 's/.*"sha256":"\([a-f0-9]*\)".*/\1/p' "$CASE/compile.json")"
    "$HYDRA_BIN" workflow plan run "$CASE/compiled.json" --accept "$_pc_digest" > "$CASE/run.out" 2> "$CASE/run.err"
    _pc_code=$?
    sed -n '1p' "$CASE/run.out" > "$CASE/runs"
    [ "$_pc_code" -eq 0 ] && [ "$(cat "$(run_dir_for "$HYDRA_HOME" "$(cat "$CASE/runs")")/state")" = succeeded ]
}

for PLAN_N in ${HYDRA_TEST_DIFF_SIZES:-5 20 64}; do
    for PLAN_SHAPE in chain wide layers; do
        case "$PLAN_SHAPE:$PLAN_N" in chain:*) PLAN_PAR=1 ;; wide:64) PLAN_PAR=16 ;; *) PLAN_PAR=4 ;; esac
        if [ "$PLAN_PAR" -eq 1 ]; then plan_events=order; else plan_events=multiset; fi
        run_case "plan-$PLAN_SHAPE-$PLAN_N" "$plan_events" plan_case
    done
done

# Workflow definitions after tests/test_workflow_runtime.sh, test_workflow_driver.sh
# and test_workflow_approval.sh.
step_yaml() { # <id> <needs> <retry> <idempotent> <argv> [extra lines]
    printf '  - id: %s\n    kind: exec\n    needs: [%s]\n    retry: %s\n    idempotent: %s\n' "$1" "$2" "$3" "$4"
    [ -z "${6:-}" ] || printf '%s\n' "$6"
    printf '    args:\n      head: diff-head\n      argv: %s\n' "$5"
}

flow_header() { printf 'version: 1\nid: %s\nparallelism: %s\nresources:\n  disk_mb: %s\n  max_heads: 1\nsteps:\n' "$1" "$2" "${3:-1}"; }

run_flow() { # <file> <expected-status>
    "$HYDRA_BIN" workflow run "$1" > "$CASE/run.out" 2> "$CASE/run.err"
    _rf_code=$?
    sed -n '1p' "$CASE/run.out" >> "$CASE/runs"
    [ -s "$CASE/run.out" ] && [ "$_rf_code" -eq "$2" ]
}

cat > "$ROOT/retry.sh" <<'EOF'
#!/bin/sh
count="$(sed -n '1p' "$1" 2>/dev/null || printf 0)"
count=$((count + 1))
printf '%s\n' "$count" > "$1"
[ "$count" -ge "$2" ]
EOF
cat > "$ROOT/long.sh" <<'EOF'
#!/bin/sh
count="$(sed -n '1p' "$1" 2>/dev/null || printf 0)"
count=$((count + 1))
printf '%s\n' "$count" > "$1"
if [ "$count" -eq 1 ]; then
    : > "$2"
    trap 'exit 143' HUP INT TERM
    sleep 30 &
    wait $!
fi
EOF

retry_case() {
    new_head_repo || return 1
    {
        flow_header diff-retry 1
        step_yaml flaky '' 1 true "[sh, $ROOT/retry.sh, $CASE/count, 2]"
        step_yaml after flaky 0 true '[true]'
        step_yaml side '' 0 true '[true]'
    } > "$CASE/flow.yml"
    run_flow "$CASE/flow.yml" 0
}
run_case retry order retry_case

cascade_case() {
    new_head_repo || return 1
    {
        flow_header diff-cascade 1
        step_yaml a '' 1 true '["false"]'
        step_yaml b a 0 true '["true"]'
        step_yaml c '' 0 true '["true"]'
        step_yaml d b 0 true '["true"]'
        step_yaml e a 0 true '["true"]'
        step_yaml f c 0 true '["true"]'
        step_yaml g 'd, f' 0 true '["true"]'
    } > "$CASE/flow.yml"
    run_flow "$CASE/flow.yml" 1
}
run_case cascade order cascade_case

backoff_case() {
    new_head_repo || return 1
    {
        flow_header diff-backoff 1
        step_yaml retry '' 1 true "[sh, $ROOT/retry.sh, $CASE/count, 2]" '    retry_on: [failure]
    retry_backoff: 2'
        step_yaml after retry 0 true '[true]'
    } > "$CASE/flow.yml"
    run_flow "$CASE/flow.yml" 0
}
run_case backoff order backoff_case

timeout_case() {
    new_head_repo || return 1
    {
        flow_header diff-timeout 1
        printf '  - id: slow\n    kind: exec\n    needs: []\n    retry: 1\n    idempotent: true\n    retry_on: [timeout]\n    args:\n      head: diff-head\n      argv: [sleep, 3]\n      timeout: 1\n'
    } > "$CASE/flow.yml"
    run_flow "$CASE/flow.yml" 1
}
run_case timeout order timeout_case

disk_case() {
    new_head_repo || return 1
    { flow_header diff-disk 1 1048576; step_yaml big '' 0 true '[true]'; step_yaml later big 0 true '[true]'; } > "$CASE/flow.yml"
    run_flow "$CASE/flow.yml" 1
}
run_case disk order disk_case

parallel_case() {
    new_head_repo || return 1
    {
        flow_header diff-parallel 4
        for _pc_step in 1 2 3 4 5 6 7 8; do step_yaml "p$_pc_step" '' 0 true '[true]'; done
        step_yaml join 'p1, p2, p3, p4, p5, p6, p7, p8' 0 true '[true]'
    } > "$CASE/flow.yml"
    run_flow "$CASE/flow.yml" 0
}
run_case parallel multiset parallel_case

approval_flow() {
    {
        flow_header diff-approval 1
        step_yaml before '' 0 false '[true]'
        printf '  - id: approval\n    kind: approval-wait\n    needs: [before]\n    idempotent: false\n    args:\n      head: diff-head\n      name: review\n      message: Review the evidence before continuing\n'
        step_yaml after approval 0 false '[true]'
    } > "$CASE/flow.yml"
    run_flow "$CASE/flow.yml" 3 || return 1
    _af_run="$(cat "$CASE/runs")"
    _af_request="$(cat "$(run_dir_for "$HYDRA_HOME" "$_af_run")/steps/approval/request-id")"
    "$HYDRA_BIN" workflow decide "$_af_run" "$_af_request" "$1" --by differential >/dev/null || return 1
    "$HYDRA_BIN" workflow resume "$_af_run" >/dev/null 2>&1
}
approve_case() { new_head_repo || return 1; approval_flow approve; }
reject_case() { new_head_repo || return 1; ! approval_flow reject; }
run_case approval-approve order approve_case
run_case approval-reject order reject_case

cancel_case() {
    new_head_repo || return 1
    { flow_header diff-cancel 1; step_yaml long '' 0 true "[sh, $ROOT/long.sh, $CASE/count, $CASE/started]"; step_yaml after long 0 true '[true]'; } > "$CASE/flow.yml"
    "$HYDRA_BIN" workflow run "$CASE/flow.yml" > "$CASE/run.out" 2> "$CASE/run.err" &
    _cc_runner=$!
    WAIT_FOR_TIMEOUT=60 wait_for "the long step to start" test -f "$CASE/started" || return 1
    sed -n '1p' "$CASE/run.out" > "$CASE/runs"
    printf '%s\n' ./steps/long/attempt-1/stdout ./steps/long/attempt-1/stderr > "$CASE/volatile"
    "$HYDRA_BIN" workflow cancel "$(cat "$CASE/runs")" > "$CASE/cancel.out" 2>&1
    _cc_code=$?
    wait "$_cc_runner"
    [ "$_cc_code" -eq 0 ]
}
run_case cancel order cancel_case

long_step_gone() { [ -z "$(test_processes_naming "$ROOT/long.sh")" ]; }
process_gone() { ! kill -0 "$1" 2>/dev/null; }
crash_case() {
    new_head_repo || return 1
    {
        flow_header diff-crash 1
        step_yaml effect '' 0 false '[true]'
        step_yaml resumable effect 1 true "[sh, $ROOT/long.sh, $CASE/count, $CASE/started]"
    } > "$CASE/flow.yml"
    "$HYDRA_BIN" workflow run "$CASE/flow.yml" > "$CASE/run.out" 2> "$CASE/run.err" &
    _cr_runner=$!
    WAIT_FOR_TIMEOUT=60 wait_for "the resumable step to start" test -f "$CASE/started" || return 1
    sed -n '1p' "$CASE/run.out" > "$CASE/runs"
    _cr_dir="$(run_dir_for "$HYDRA_HOME" "$(cat "$CASE/runs")")"
    # The driver dies; its worker and the step's command are stopped after it.
    kill -KILL "$(cat "$_cr_dir/owner-pid")" 2>/dev/null
    wait "$_cr_runner" 2>/dev/null
    _cr_worker="$(cat "$_cr_dir/steps/resumable/worker-pid")"
    printf '%s\n' ./steps/resumable/attempt-1/stdout ./steps/resumable/attempt-1/stderr > "$CASE/volatile"
    # KILL, not TERM: a TERM can land before or after the worker resets its
    # traps, so whether the worker records the attempt would be a race.
    test_signal_tree "$_cr_worker" KILL
    WAIT_FOR_TIMEOUT=30 wait_for "the interrupted step to stop" long_step_gone || return 1
    WAIT_FOR_TIMEOUT=30 wait_for "the interrupted worker to exit" process_gone "$_cr_worker" || return 1
    "$HYDRA_BIN" workflow resume "$(cat "$CASE/runs")" > "$CASE/resume.out" 2>&1
}
run_case crash order crash_case

statistics_fault_case() {
    new_head_repo || return 1
    mkdir "$CASE/bin"
    cp "$REPO/tests/fixtures/statistics/fail-scalar-mv.sh" "$CASE/bin/mv"
    chmod +x "$CASE/bin/mv"
    { flow_header diff-statistics 1; step_yaml only '' 0 true '[true]'; } > "$CASE/flow.yml"
    HYDRA_TEST_REAL_MV="$(command -v mv)" HYDRA_TEST_STATS_FIELD=started-at HYDRA_TEST_STATS_MARKER="$CASE/fault" \
        PATH="$CASE/bin:$PATH" run_flow "$CASE/flow.yml" 0 && [ -f "$CASE/fault" ]
}
run_case statistics-fault order statistics_fault_case

# Remote task DAGs and plans of remote tasks: tests/test_workflow_task.sh and
# tests/test_workflow_plan_task.sh as the fleet-case-dag-*, plan-task-* and
# plan-repair-* cases run them, kept in a fixed directory so that both
# drivers' records can be compared. Runs are compared in creation order.
# The cancel faults are left to make test-fleet and test-shell-driver: their tests resume once a
# second until the remote side confirms, so the number of resumes is timing.
task_case() {
    # shellcheck disable=SC2086 # TASK_ENV is a list of assignments.
    env $TASK_ENV HYDRA_TEST_FIXTURE_DIR="$CASE/hydra-task.fixed" HYDRA_TEST_KEEP_FIXTURE=1 \
        sh "$REPO/tests/$TASK_TEST" > "$CASE/task.log" 2>&1
    _tc_code=$?
    printf '%s\n' "$CASE/hydra-task.fixed/home" > "$CASE/home-path"
    for _tc_dir in "$CASE"/hydra-task.fixed/home/state/v2/projects/*/workflows/runs/run_*; do
        [ -d "$_tc_dir" ] || continue
        printf '%s %s %s\n' "$(cat "$_tc_dir/created-at")" "$(sed -n '1p' "$_tc_dir/events.jsonl" | cut -c1-80)" "${_tc_dir##*/}"
    done | LC_ALL=C sort | sed 's/.* //' > "$CASE/runs"
    cat "$CASE/task.log"
    [ "$_tc_code" -eq 0 ]
}
while IFS='|' read -r task_name TASK_TEST TASK_ENV; do
    run_case "$task_name" order task_case
done <<'EOF'
dag-handoff|test_workflow_task.sh|HYDRA_TEST_DAG_PLAIN=1
dag-parallelism-1|test_workflow_task.sh|HYDRA_TEST_DAG_PARALLELISM=1
dag-crash-2|test_workflow_task.sh|HYDRA_TEST_DAG_CRASH=2
dag-lost-ack-1|test_workflow_task.sh|HYDRA_TEST_DAG_LOST_ACK=1
dag-result-bad-1|test_workflow_task.sh|HYDRA_TEST_DAG_RESULT_BAD=1
dag-lost-ack-1-dag-fault-dispatch|test_workflow_task.sh|HYDRA_TEST_DAG_LOST_ACK=1 HYDRA_TEST_DAG_FAULT=dispatch
plan-task-verdict-pass|test_workflow_plan_task.sh|HYDRA_TEST_PLAN_TASK_VERDICT=pass
plan-task-verdict-fail|test_workflow_plan_task.sh|HYDRA_TEST_PLAN_TASK_VERDICT=fail
plan-repair-pass|test_workflow_plan_task.sh|HYDRA_TEST_PLAN_REPAIR=pass
EOF

echo "============================================"
echo "Test Results:"
echo "Total:  $test_count"
echo "Passed: $pass_count"
echo "Failed: $fail_count"
[ "$fail_count" -eq 0 ]
