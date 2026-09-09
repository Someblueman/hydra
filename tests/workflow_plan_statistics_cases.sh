#!/bin/sh
# Sourced by test_workflow_plan.sh after its positive and durable-head checks.
mkdir "$ROOT/timing-bin"
cp "$REPO/tests/fixtures/statistics/fail-scalar-mv.sh" "$ROOT/timing-bin/mv"
chmod +x "$ROOT/timing-bin/mv"
timing_real_mv="$(command -v mv)"
for timing_field in verified-at verification-plan-sha256; do
    sed "s/plan-smoke/plan-timing-$timing_field/g" "$ROOT/plan.json" > "$ROOT/timing-plan.json"
    "$HYDRA_BIN" workflow plan compile "$ROOT/timing-plan.json" "$ROOT/policy.json" "$ROOT/timing-$timing_field.json" > "$ROOT/timing-compile.json"
    assert_success $? "compile plan for optional $timing_field failure"
    timing_digest="$(sed -n 's/.*"sha256":"\([a-f0-9]*\)".*/\1/p' "$ROOT/timing-compile.json")"
    PATH="$ROOT/timing-bin:$PATH" HYDRA_TEST_REAL_MV="$timing_real_mv" \
        HYDRA_TEST_STATS_FIELD="$timing_field" HYDRA_TEST_STATS_SEED=1 \
        HYDRA_TEST_STATS_MARKER="$ROOT/timing-$timing_field.fault" \
        "$HYDRA_BIN" workflow plan run "$ROOT/timing-$timing_field.json" --accept "$timing_digest" \
        > "$ROOT/timing-$timing_field.out" 2> "$ROOT/timing-$timing_field.err"
    assert_success $? "optional $timing_field persistence failure preserves verified delivery"
    if [ -f "$ROOT/timing-$timing_field.fault" ] && [ -f "$ROOT/timing-$timing_field.fault.seeded" ]; then timing_marker_status=0; else timing_marker_status=1; fi
    assert_success "$timing_marker_status" "injected $timing_field write failure after seeding old timing"
    timing_run="$(sed -n '1p' "$ROOT/timing-$timing_field.out")"
    timing_dir="$(find "$HYDRA_HOME/state/v2/projects" -type d -path "*/workflows/runs/$timing_run" -print)"
    assert_equal succeeded "$(cat "$timing_dir/state")" "verified plan succeeds without $timing_field"
    "$HYDRA_BIN" workflow plan result "$timing_run" > "$ROOT/timing-$timing_field.result"
    assert_success $? "fresh result verification succeeds without $timing_field"
    python3 "$REPO/tests/statistics_evidence.py" "$HYDRA_BIN" "$timing_dir" 0 unverified
    assert_success $? "partial $timing_field persistence leaves old verification timing unknown"
done
