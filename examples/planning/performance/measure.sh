#!/bin/sh
set -eu
input="$HYDRA_WORKFLOW_INPUTS_DIR/records"
out="$HYDRA_WORKFLOW_OUTPUTS_DIR"
base="$HYDRA_WORKFLOW_REPO_ROOT/baseline.sh"
candidate="$HYDRA_WORKFLOW_REPO_ROOT/candidate.sh"
mkdir -p "$out"
printf 'implementation,trial,elapsed_ns,status,count\n' > "$out/raw.csv"
sha256sum "$input" | awk '{print $1}' > "$out/workload.sha256"
uname -srm > "$out/environment.txt"
printf 'shell=%s\n' "${SHELL:-unknown}" >> "$out/environment.txt"
for impl in baseline candidate; do
    script="$base"; [ "$impl" = candidate ] && script="$candidate"
    "$script" "$input" >/dev/null
    "$script" "$input" >/dev/null
    for trial in 1 2 3 4 5 6 7 8 9 10; do
        start=$(date +%s%N)
        status=0
        count=$($script "$input" 2>/dev/null) || status=$?
        end=$(date +%s%N)
        elapsed=$((end - start))
        if [ "$status" -eq 0 ] && [ "$count" = 4003 ]; then
            printf '%s,%s,%s,ok,%s\n' "$impl" "$trial" "$elapsed" "$count" >> "$out/raw.csv"
        else
            printf '%s,%s,%s,fail,%s\n' "$impl" "$trial" "$elapsed" "${count:-}" >> "$out/raw.csv"
        fi
    done
done
