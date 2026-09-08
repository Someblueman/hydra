#!/bin/sh
# Compare cognitive complexity by source/function, independent of line numbers.
set -eu
baseline="$1" tool="$2"; shift 2
if [ "$#" -eq 0 ] || [ "$1" = -- ]; then
    echo 'C analysis requires source files' >&2
    exit 2
fi
[ -f "$baseline" ] || { echo "Missing complexity baseline: $baseline" >&2; exit 2; }
"$tool" --version | grep -q 'version 22\.1\.8' || { echo 'C analysis requires clang-tidy 22.1.8' >&2; exit 2; }
report="${QUALITY_C_LOG:-build/quality-c.log}"
mkdir -p "$(dirname "$report")"
if "$tool" --header-filter='(src|tests/c)/' \
    --checks='-*,clang-analyzer-*,readability-function-cognitive-complexity' \
    --config='{CheckOptions: {readability-function-cognitive-complexity.Threshold: 15, readability-function-cognitive-complexity.DescribeBasicIncrements: false}}' \
    "$@" > "$report" 2>&1; then
    cat "$report"
else
    code=$?
    cat "$report"
    exit "$code"
fi
# clang-tidy reports progress for multi-file runs; single-file runs are silent.
# Check source existence as well as progress, including warning-free analyses.
expected=0
for arg do
    [ "$arg" != -- ] || break
    [ -f "$arg" ] || { echo "Missing C source: $arg" >&2; exit 2; }
    expected=$((expected + 1))
done
processed=$(grep -c 'Processing file ' "$report" || true)
[ "$expected" -ne 1 ] || [ "$processed" -ne 0 ] || processed=1
[ "$processed" -eq "$expected" ] || { echo "Incomplete C analysis: $processed/$expected files" >&2; exit 2; }
metrics="${report%.log}.tsv"
awk -v root="$(pwd)/" '
    /warning: function .* has cognitive complexity of .*\[readability-function-cognitive-complexity\]/ {
        path=$0; sub(/:[0-9]+:[0-9]+: warning:.*/, "", path)
        if (index(path, root)==1) path=substr(path, length(root)+1)
        sub(/^\.\//, "", path)
        name=$0; sub(/^.*warning: function /, "", name)
        sub(/ has cognitive complexity of .*/, "", name)
        name=substr(name, 2, length(name)-2)
        score=$0; sub(/^.*has cognitive complexity of /, "", score); sub(/ .*/, "", score)
        printf "%s\t%s\t%d\n", path, name, score
    }
' "$report" | LC_ALL=C sort -u > "$metrics"
awk -F '\t' '
    /^[[:space:]]*#/ || NF==0 { next }
    FILENAME==ARGV[1] {
        if (NF!=3 || $3 !~ /^[0-9]+$/ || $3<=15 || (($1 SUBSEP $2) in allowed)) {
            print "Invalid complexity baseline record: " $0; failed=1
        }
        allowed[$1,$2]=$3; next
    }
    {
        key=$1 SUBSEP $2; previous=(key in allowed) ? allowed[key] : 15
        if ($3>previous) {
            printf "COMPLEXITY %s:%s increased from %d to %d\n", $1, $2, previous, $3
            failed=1
        }
        count++
    }
    END {
        if (failed) exit 1
        printf "C complexity: %d advisory functions, no regressions against the reviewed baseline\n", count
    }
' "$baseline" "$metrics"
