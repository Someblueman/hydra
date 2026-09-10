#!/bin/sh
# Real workflow -> read adapter -> C graph, plus malformed-boundary checks.
set -eu
viz_root="$(CDPATH='' cd -- "$(dirname "$0")/.." && pwd)"
viz_tmp="$(mktemp -d "${TMPDIR:-/tmp}/hydra-visualization.XXXXXX")"
viz_bin="$viz_root/bin/hydra"
viz_build="${BUILD_DIR:-$viz_root/build}"
case "$viz_build" in /*) ;; *) viz_build="$viz_root/$viz_build" ;; esac
viz_tui="${HYDRA_VISUAL_TEST_BIN:-$viz_build/hydra-tui}"
export HYDRA_HOME="$viz_tmp/home" HYDRA_SKIP_AI=1 HYDRA_NONINTERACTIVE=1 HYDRA_NO_SWITCH=1
cleanup() {
    if [ -d "$viz_tmp/repo/.git" ]; then
        (cd "$viz_tmp/repo" && "$viz_bin" kill viz-proof --force) >/dev/null 2>&1 || true
    fi
    rm -rf "$viz_tmp"
}
trap cleanup EXIT
trap 'exit 130' INT
trap 'exit 143' TERM
trap 'exit 129' HUP
mkdir "$viz_tmp/repo"
cd "$viz_tmp/repo"
git init -q
git config user.name Test
git config user.email test@example.com
printf 'visualization\n' > input
git add input
git commit -qm base
"$viz_bin" init --no-agent --trust >/dev/null
cat > "$viz_tmp/workflow.yml" <<'EOF'
version: 1
id: visualization-proof
parallelism: 2
resources:
  disk_mb: 1
  max_heads: 1
steps:
  - id: prepare
    kind: spawn
    needs: []
    retry: 0
    idempotent: false
    args:
      branch: viz-proof
  - id: build
    kind: exec
    needs: [prepare]
    retry: 0
    idempotent: true
    args:
      head: viz-proof
      argv: [true]
  - id: tests
    kind: exec
    needs: [prepare]
    retry: 0
    idempotent: true
    args:
      head: viz-proof
      argv: [true]
  - id: review
    kind: message
    needs: [build,tests]
    retry: 0
    idempotent: false
    args:
      head: viz-proof
      message: verified
EOF
"$viz_bin" workflow run "$viz_tmp/workflow.yml" > "$viz_tmp/run.log"
viz_before="$(find "$HYDRA_HOME/state" -type f -exec cksum {} \; | sort | cksum)"
"$viz_bin" workflow tui-data > "$viz_tmp/workflows.tsv"
"$viz_bin" workflow statistics-data > "$viz_tmp/statistics.tsv"
viz_after="$(find "$HYDRA_HOME/state" -type f -exec cksum {} \; | sort | cksum)"
[ "$viz_before" = "$viz_after" ]
awk -F '\t' '$1=="R" && $3=="visualization-proof" && $4=="succeeded" {run=1}
    $1=="S" {steps++; if ($6!=1 || $7 !~ /^[0-9]+$/ || $8 !~ /^[0-9]+$/ || $8<$7) bad=1}
    END {exit (bad || !(run && steps==4))}' "$viz_tmp/statistics.tsv"
awk -F '\t' '$1=="W" && $3=="visualization-proof" && $4=="succeeded" {run=1}
    $1=="N" && $3=="review" && $5=="succeeded" && $7=="build,tests" {join=1}
    END {exit !(run && join)}' "$viz_tmp/workflows.tsv"
"$viz_bin" tui --data > "$viz_tmp/heads.tsv"
for viz_size in 40x10 80x24 140x40; do
    "$viz_tui" --headless-fixture "$viz_tmp/heads.tsv" --workflow-fixture "$viz_tmp/workflows.tsv" \
        --view workflows --ascii --size "$viz_size" > "$viz_tmp/graph.out"
    viz_cols="${viz_size%x*}" viz_rows="${viz_size#*x}"
    awk -v cols="$viz_cols" -v rows="$viz_rows" 'length >= cols {exit 1} END {if (NR!=rows+1) exit 1}' "$viz_tmp/graph.out"
done
grep -q '4 dependency edges' "$viz_tmp/graph.out"
grep -q 'review' "$viz_tmp/graph.out"
grep -q 'succeeded' "$viz_tmp/graph.out"
mkdir -p "$viz_root/build/visualization-evidence"
cp "$viz_tmp/workflows.tsv" "$viz_root/build/visualization-evidence/real-workflow.tsv"
cp "$viz_tmp/heads.tsv" "$viz_root/build/visualization-evidence/real-heads.tsv"
cp "$viz_tmp/statistics.tsv" "$viz_root/build/visualization-evidence/real-statistics.tsv"
cp "$viz_tmp/graph.out" "$viz_root/build/visualization-evidence/real-graph.txt"
# Cycles and missing dependencies must not produce a plausible graph.
for viz_bad in cycle missing duplicate empty_dependency duplicate_dependency; do
    awk -F '\t' -v OFS='\t' -v mode="$viz_bad" '
        $1=="N" && $3=="prepare" {if(mode=="cycle") $7="review"; if(mode=="missing") $7="absent"}
        $1=="N" && $3=="review" {if(mode=="empty_dependency") $7="build,,tests"; if(mode=="duplicate_dependency") $7="build,build"}
        {print; if(mode=="duplicate" && $1=="N" && $3=="prepare") print}
    ' "$viz_tmp/workflows.tsv" > "$viz_tmp/bad.tsv"
    if "$viz_tui" --headless-fixture "$viz_tmp/heads.tsv" --workflow-fixture "$viz_tmp/bad.tsv" \
        --view workflows >/dev/null 2>&1; then
        printf 'FAIL: accepted %s graph\n' "$viz_bad" >&2; exit 1
    fi
done
for viz_size in 40x10 80x24 140x40; do
    "$viz_tui" --fleet --headless-fixture "$viz_root/tests/fixtures/tui/fleet-v2.tsv" \
        --view hosts --ascii --size "$viz_size" > "$viz_tmp/hosts.out"
    viz_cols="${viz_size%x*}" viz_rows="${viz_size#*x}"
    awk -v cols="$viz_cols" -v rows="$viz_rows" 'length >= cols {exit 1} END {if (NR!=rows+1) exit 1}' "$viz_tmp/hosts.out"
done
grep -q 'empty.*responded.*0 heads' "$viz_tmp/hosts.out"
grep -q 'offline.*failed' "$viz_tmp/hosts.out"
for viz_size in 999999999999999999x24 80x9999999999999999 4097x24; do
    if "$viz_tui" --headless-fixture "$viz_tmp/heads.tsv" --size "$viz_size" >/dev/null 2>&1; then
        echo 'FAIL: accepted out-of-range dimensions'; exit 1
    fi
done
# Missing or symlinked scalar evidence remains unknown rather than becoming zero.
viz_run="$(awk -F '\t' '$1=="R" {print $2; exit}' "$viz_tmp/statistics.tsv")"
viz_record="$(find "$HYDRA_HOME/state/v2" -type d -name "$viz_run" | head -n 1)"
[ -n "$viz_record" ]
# shellcheck source=/dev/null
. "$viz_root/tests/fixture-tools.sh"
statistics_evidence "$viz_bin" "$viz_record" 0 unverified
rm "$viz_record/started-at" "$viz_record/recovery-count" "$viz_record/steps/build/initial-ready-at"
rm "$viz_record/steps/build/started-at"
printf '123456\n' > "$viz_tmp/outside-start"
ln -s "$viz_tmp/outside-start" "$viz_record/steps/build/started-at"
printf '%s\n' 'not-a-number' > "$viz_record/steps/tests/attempts"
"$viz_bin" workflow statistics-data > "$viz_tmp/missing-statistics.tsv"
awk -F '\t' '$1=="S" && $3=="build" {start=($7=="-")}
    $1=="S" && $3=="tests" {attempt=($6=="not-a-number" && $8=="-")}
    END {exit !(start && attempt)}' "$viz_tmp/missing-statistics.tsv"
"$viz_tui" --headless-fixture "$viz_tmp/heads.tsv" --statistics-fixture "$viz_tmp/missing-statistics.tsv" \
    --view statistics --ascii --size 140x40 > "$viz_tmp/statistics.out"
grep -q 'Timing 2/4' "$viz_tmp/statistics.out"
grep -q 'Attempts 3/4' "$viz_tmp/statistics.out"
"$viz_build/test-statistics" "$viz_tmp/missing-statistics.tsv" "$viz_run" > "$viz_tmp/metrics.txt"
awk '$1==0 {q=($2==4 && $3==3)} $1==1 {e=($2==1 && $3==0 && $4==0)}
     $1==3 {r=($2==1 && $3==0 && $4==0)} END {exit !(q && e && r)}' "$viz_tmp/metrics.txt"
printf 'PASS: real workflow execution, read-only projection, dependency graph, responsive bounds, invalid graph rejection\n'
