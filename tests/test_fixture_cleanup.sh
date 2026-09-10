#!/bin/sh
# Real dirty heads must release their PTYs on success, failure and signals.
set -eu
root="$(CDPATH='' cd -- "$(dirname "$0")/.." && pwd)"
if ! command -v tmux >/dev/null 2>&1; then
    printf 'SKIP fixture cleanup: tmux unavailable\n'
    exit 0
fi
base="$(mktemp -d /tmp/hydra-fixture-cleanup.XXXXXX)"
TMUX_TMPDIR="$base/socket"
export TMUX_TMPDIR
unset TMUX
mkdir "$TMUX_TMPDIR"
cleanup() {
    tmux kill-server 2>/dev/null || : # This test's private socket only.
    rm -rf "$base"
}
trap cleanup EXIT
trap 'exit 130' INT
trap 'exit 143' TERM
trap 'exit 129' HUP

# A similarly prefixed directory and session must survive every fixture cleanup.
mkdir "$base/fixture success-other"
tmux new-session -d -s dirty-cleanup-elsewhere -c "$base/fixture success-other"
sentinel="$(tmux display-message -p -t dirty-cleanup-elsewhere '#{session_id}')"

cat > "$base/child.sh" <<'CHILD'
#!/bin/sh
set -eu
root="$1" fixture="$2" mode="$3"
export HYDRA_HOME="$fixture/home" HYDRA_NONINTERACTIVE=1 HYDRA_SKIP_AI=1 HYDRA_NO_SWITCH=1
# shellcheck source=/dev/null
. "$root/tests/tmux_fixture_cleanup.sh"
child_code=0
trap 'child_code=$?; test_tmux_fixture_cleanup "$fixture" || child_code=1; exit "$child_code"' EXIT
trap 'exit 130' INT
trap 'exit 143' TERM
trap 'exit 129' HUP
mkdir -p "$fixture/repo"
cd "$fixture/repo"
git init -q
git config user.name Fixture
git config user.email fixture@example.invalid
printf 'initial\n' > dirty.txt
git add dirty.txt
git -c commit.gpgSign=false commit -qm initial
"$root/bin/hydra" init --no-agent --trust >/dev/null
"$root/bin/hydra" spawn dirty-cleanup --no-agent >/dev/null
worktree="$("$root/bin/hydra" path dirty-cleanup)"
printf 'retained evidence\n' > "$worktree/dirty.txt"
printf '%s\n' "$worktree" > "$fixture/worktree"
if "$root/bin/hydra" kill dirty-cleanup --force >/dev/null 2>&1; then
    printf 'Expected normal teardown to preserve dirty work\n' >&2
    exit 1
fi
case "$mode" in
    success) exit 0 ;;
    failure) exit 7 ;;
    INT|TERM|HUP) kill -"$mode" "$$"; exit 99 ;;
esac
CHILD

for mode in success failure INT TERM HUP; do
    case "$mode" in success) expected=0 ;; failure) expected=7 ;; INT) expected=130 ;; TERM) expected=143 ;; HUP) expected=129 ;; esac
    fixture="$base/fixture $mode"
    code=0
    sh "$base/child.sh" "$root" "$fixture" "$mode" > "$base/$mode.log" 2>&1 || code=$?
    if [ "$code" -ne "$expected" ]; then cat "$base/$mode.log"; exit 1; fi
    [ "$(cat "$(cat "$fixture/worktree")/dirty.txt")" = 'retained evidence' ]
    [ "$(tmux list-sessions -F '#{session_id}')" = "$sentinel" ]
    [ "$(tmux list-panes -a -F '#{pane_id}' | wc -l | tr -d ' ')" = 1 ]
    printf 'PASS fixture cleanup: %s, dirty evidence retained, PTYs returned to baseline\n' "$mode"
done

# The dashboard must fail promptly both on allocation failure and on no progress.
for split_code in 1 0; do
    (
        # shellcheck source=/dev/null
        . "$root/tests/dashboard_cases.sh"
        # Called by the sourced dashboard case.
        # shellcheck disable=SC2329,SC2317
        print_error() { printf '%s\n' "$*" >&2; }
        split_calls=0
        # shellcheck disable=SC2329,SC2317
        tmux() {
            case "$1" in
                list-panes) printf 'one pane\n' ;;
                split-window) split_calls=$((split_calls + 1)); return "$split_code" ;;
                *) return 99 ;;
            esac
        }
        if dashboard_test_ensure_panes fixture > /dev/null 2>&1; then exit 1; fi
        [ "$split_calls" = 1 ]
    )
    printf 'PASS dashboard allocation stops after one failed/non-progressing split\n'
done

# Receiver teardown follows its real lock, never an unrelated reused PID.
# shellcheck source=/dev/null
. "$root/tests/fixture-tools.sh"
fixture_lock check "$root" "$base"
