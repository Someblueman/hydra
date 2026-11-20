#!/bin/sh
# Builds the state store a rows fixture describes, for tests that run the
# interactive control centre: it reads its heads from Hydra's state store,
# never from a rows file.
#
# Usage: rows-store.sh <rows.tsv> <directory>
#
# <directory> must not exist. It receives:
#   repo/          a repository of the fixture project: the control centre's
#                  working directory
#   home/          HYDRA_HOME, holding the records of every H row
#   sessions       the sessions of the rows that say "active", one per line:
#                  what the fixture tmux lists (HYDRA_TEST_STORE)
#   leftovers.tsv  the orphan-worktree rows: what the fixture hydra prints for
#                  `tui --orphan-rows` (HYDRA_TEST_STORE)
#
# Read back with `hydra-tui --data`, the store prints the fixture's rows where
# records decide them: names, counts, states, identities, and the stale-lock
# and leftover-worktree findings. What follows from the store instead:
#   - paths (a row's source, the notification file) are the store's own;
#   - a row without a head identity gets one (a listed head always has a
#     record), and with it an instance and a desired state;
#   - a dead session's liveness is "stopped";
#   - findings the reader derives are its own, whatever R rows the fixture
#     holds: every dead session is reported, and a teardown or lifecycle
#     finding only for a head whose row says it is stopping.

set -eu

rows="$1"
store="$2"
project=project_00000000000000f1
tab="$(printf '\t')"

[ -f "$rows" ] || { echo "rows-store.sh: no rows fixture: $rows" >&2; exit 2; }
[ ! -e "$store" ] || { echo "rows-store.sh: $store exists" >&2; exit 2; }
[ "$(sed -n '1p' "$rows")" = "HYDRA_TUI${tab}2" ] || { echo "rows-store.sh: not a version 2 rows fixture: $rows" >&2; exit 2; }

mkdir -p "$store/repo" "$store/home"
store="$(cd "$store" && pwd -P)"
repo="$store/repo"
home="$store/home"
project_dir="$home/state/v2/projects/$project"
host="$(hostname 2>/dev/null || uname -n 2>/dev/null || echo unknown)"

put() { printf '%s\n' "$2" > "$1"; }

# Usage: lines <count> <text> -> that many lines of text
lines() {
    awk -v count="$1" -v text="$2" 'BEGIN { for (i = 0; i < count; i++) print text }'
}

number() {
    case "$1" in ''|*[!0-9]*) printf '0\n' ;; *) printf '%s\n' "$1" ;; esac
}

valid_id() {
    case "$1" in "$2"_*) ;; *) return 1 ;; esac
    case "${1#"$2"_}" in ''|*[!0-9a-f]*) return 1 ;; esac
}

git init -q -b main "$repo"
mkdir -p "$repo/.git/hydra" "$project_dir/heads" "$project_dir/queue"
put "$repo/.git/hydra/project-id" "$project"
put "$home/state/v2/schema-version" 2
put "$project_dir/project-id" "$project"
put "$project_dir/repo-root" "$repo"
: > "$store/sessions"
: > "$store/leftovers.tsv"
# The control centre answers the workflow sources itself for a project that
# records no run. The fixture project records one that has not ended, so its
# workflow projection, workspace links and attention are asked of the CLI at
# every refresh: the fixture hydra's to answer.
mkdir -p "$project_dir/workflows/runs/run_00000000000000f1"

# A tab is white space to `read`, which would drop an empty field: the rows
# are read with a separator that is not.
unit="$(printf '\037')"
awk -F '\t' '$1 == "H"' "$rows" | tr '\t' '\037' > "$store/heads.rows"

index=0
while IFS="$unit" read -r _kind branch session profile group pr status _liveness declared observed confidence instance \
        events signals messages claims scopes queue resources changed gates approved desired _source head \
        _adapter _adapter_confidence _adapter_source notifications _notification_source mode agent_exit; do
    index=$((index + 1))
    valid_id "$head" head || head="$(printf 'head_ffffffffffff%04x' "$index")"
    valid_id "$instance" instance || instance="instance_${head#head_}"
    case "$mode" in
        interactive|headless) ;;
        *) if [ "$session" = - ]; then mode=headless; else mode=interactive; fi ;;
    esac
    case "$desired" in
        running|headless|stopping) ;;
        *) if [ "$mode" = headless ]; then desired=headless; else desired=running; fi ;;
    esac

    dir="$project_dir/heads/$head"
    instance_dir="$dir/instances/$instance"
    mkdir -p "$instance_dir" "$dir/events"
    put "$dir/head-id" "$head"
    put "$dir/branch" "$branch"
    put "$dir/session" "$session"
    put "$dir/terminal-mode" "$mode"
    put "$dir/profile" "$profile"
    put "$dir/group" "$group"
    put "$dir/created-at" 100
    put "$dir/dependencies" -
    put "$dir/pr" "$pr"
    put "$dir/current-instance" "$instance"
    put "$dir/desired-state" "$desired"
    put "$dir/worktree" ""
    put "$instance_dir/instance-id" "$instance"
    put "$instance_dir/session" "$session"
    put "$instance_dir/terminal-mode" "$mode"
    [ -z "$declared" ] || put "$instance_dir/declared-outcome" "$declared"
    put "$instance_dir/observed-status" "$observed"
    put "$instance_dir/observed-confidence" "$confidence"
    case "$agent_exit" in ''|*[!0-9]*) ;; *) put "$instance_dir/agent-exit" "$agent_exit 1788786000" ;; esac

    events="$(number "$events")"; signals="$(number "$signals")"
    [ "$signals" -le "$events" ] || signals="$events"
    {
        lines "$signals" '{"type":"signal.idle"}'
        lines "$((events - signals))" '{"type":"lifecycle.recorded"}'
    } > "$dir/events/events.jsonl"

    count="$(number "$messages")"
    while [ "$count" -gt 0 ]; do
        mkdir -p "$dir/messages/queue"
        : > "$dir/messages/queue/message-$count"
        count=$((count - 1))
    done
    count="$(number "$claims")"
    while [ "$count" -gt 0 ]; do
        mkdir -p "$project_dir/claims/claim_${head#head_}$count"
        put "$project_dir/claims/claim_${head#head_}$count/owner-head" "$head"
        count=$((count - 1))
    done
    [ "$(number "$scopes")" -eq 0 ] || lines "$scopes" 'src/' > "$dir/scopes"
    count="$(number "$queue")"
    while [ "$count" -gt 0 ]; do
        put "$project_dir/queue/${head#head_}-$count.queue" "branch=$branch"
        count=$((count - 1))
    done
    if [ "$(number "$resources")" -gt 0 ]; then
        mkdir -p "$project_dir/resources/$head"
        lines "$resources" 8080 > "$project_dir/resources/$head/ports"
    fi
    count="$(number "$gates")"; granted="$(number "$approved")"
    while [ "$count" -gt 0 ]; do
        mkdir -p "$dir/gates/gate-$count"
        if [ "$count" -le "$granted" ]; then : > "$dir/gates/gate-$count/approved-at"; fi
        count=$((count - 1))
    done
    # Uncommitted files are counted by git: a worktree with that many files.
    count="$(number "$changed")"
    if [ "$count" -gt 0 ]; then
        worktree="$store/worktrees/$head"
        git init -q -b "fixture-$index" "$worktree"
        put "$dir/worktree" "$worktree"
        while [ "$count" -gt 0 ]; do
            : > "$worktree/changed-$count"
            count=$((count - 1))
        done
    fi

    if [ "$status" = active ] && [ "$mode" = interactive ]; then printf '%s\n' "$session" >> "$store/sessions"; fi
    if [ "$index" -eq 1 ] && [ "$(number "$notifications")" -gt 0 ]; then
        lines "$notifications" 'lifecycle.declared terminal 60' > "$repo/.git/hydra/notifications"
    fi
done < "$store/heads.rows"
rm "$store/heads.rows"

# A stale lock is one whose owner on this machine is gone: no process has
# this identity (it is above every limit a kernel sets on process IDs).
awk -F '\t' '$1 == "R" && $2 == "stale-lock" { print $3 }' "$rows" | while IFS= read -r lock; do
    [ -n "$lock" ] || continue
    mkdir -p "$home/locks/$lock"
    put "$home/locks/$lock/pid" 2147483647
    put "$home/locks/$lock/host" "$host"
done

# Leftover worktrees are described by the CLI, and asked for only while a
# head_<id> directory under the worktree root has no head record.
awk -F '\t' '$1 == "R" && $2 ~ /^orphan-worktree/' "$rows" > "$store/leftovers.tsv"
if [ -s "$store/leftovers.tsv" ]; then
    mkdir -p "$store/.hydra-worktrees/$project/head_00000000000000ee"
fi
