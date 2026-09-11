#!/bin/sh
set -eu
root="$(CDPATH='' cd -- "$(dirname "$0")/.." && pwd)"
base=$(mktemp -d)
trap 'rm -rf "$base"' EXIT
trap 'exit 130' INT
trap 'exit 143' TERM
trap 'exit 129' HUP
export HYDRA_HOME="$base/home" HYDRA_NONINTERACTIVE=1 HYDRA_NO_SWITCH=1
mkdir -p "$base/repo" "$HYDRA_HOME"
for library in output locks identity state_v2 events; do
    # shellcheck source=/dev/null
    . "$root/lib/$library.sh"
done
git -C "$base/repo" init -q
git -C "$base/repo" config user.email test@example.invalid
git -C "$base/repo" config user.name Test
git -C "$base/repo" commit --allow-empty -qm fixture
cd "$base/repo"
project=$(hydra_ensure_project_id)
for scenario in same appended zero-tail conflict expired; do
    head=$(state_v2_create_head "$project" "$scenario" "$scenario" - - 1 - - "$base/repo")
    head_dir=$(state_v2_head_dir "$project" "$head")
    instance=$(sed -n '1p' "$head_dir/current-instance")
    file=$(event_file_for_head "$project" "$head")
    for type in started progress declared; do
        event_emit "$project" "$head" "$instance" "lifecycle.$type" >/dev/null
    done
    cp "$file" "$base/original"
    max=1
    [ "$scenario" != zero-tail ] || max=0
    code=0
    # Exit exactly after archive metadata/cursor publication, before stream trim.
    sh -c '
        root=$1 file=$2 project=$3 head=$4 max=$5
        for library in output locks identity state_v2 events; do . "$root/lib/$library.sh"; done
        atomic_replace() {
            if [ "$1" = "$file" ]; then exit 73; fi
            mv "$2" "$1"
        }
        event_retain_file "$file" "$max" "$project" "$head"
    ' sh "$root" "$file" "$project" "$head" "$max" || code=$?
    [ "$code" = 73 ]
    # Explicit stale-owner reconciliation is required before acquiring the lock.
    remove_stale_lock_dir "$HYDRA_HOME/locks/events_${project}_${head}.lock"
    cmp "$base/original" "$file"
    archive_dir="$(dirname "$file")/archive"
    archive=$(find "$archive_dir" -name 'events-*.jsonl' -type f)
    [ -f "$archive.meta" ]
    cp "$archive.meta" "$base/metadata"
    case "$scenario" in
        appended)
            event_emit "$project" "$head" "$instance" lifecycle.extra >/dev/null
            max=2 ;;
        expired)
            sed -e 's/^created_at=.*/created_at=1/' -e 's/^expires_at=.*/expires_at=2/' "$archive.meta" > "$base/expired"
            mv "$base/expired" "$archive.meta"
            cp "$archive.meta" "$base/metadata" ;;
        conflict)
            sed 's/lifecycle.started/lifecycle.changed/' "$file" > "$base/conflict"
            mv "$base/conflict" "$file" ;;
    esac
    cp "$file" "$base/before-retry"
    if [ "$scenario" = conflict ]; then
        if "$root/bin/hydra" events retain --project "$project" --head "$head" --max-events "$max" --apply; then exit 1; fi
        cmp "$base/before-retry" "$file"
    else
        "$root/bin/hydra" events retain --project "$project" --head "$head" --max-events "$max" --dry-run > "$base/dry"
        grep -q would-resume "$base/dry"
        cmp "$base/before-retry" "$file"
        # A full protected quota must not prevent completing the existing trim.
        "$root/bin/hydra" events retain --project "$project" --head "$head" --max-events "$max" --archive-max-count 1 --apply >/dev/null
        tail -n "$max" "$base/before-retry" > "$base/expected"
        cmp "$base/expected" "$file"
        "$root/bin/hydra" events retain --project "$project" --head "$head" --max-events "$max" --apply >/dev/null
        next=$(cat "$file.next-sequence")
        event_emit "$project" "$head" "$instance" lifecycle.after-recovery >/dev/null
        [ "$(tail -n 1 "$file" | _event_sequence)" = "$next" ]
    fi
    [ "$(find "$archive_dir" -name 'events-*.jsonl' -type f | wc -l | tr -d ' ')" = 1 ]
    cmp "$base/metadata" "$archive.meta"
    printf 'PASS interrupted retention: %s\n' "$scenario"
done
