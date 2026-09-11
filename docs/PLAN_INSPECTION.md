# Inspecting and comparing compiled plans

`hydra workflow plan explain compiled.json` reports every declared node, its
upstream dependencies, declared inputs/writes, and reachable deliverables and
checks. A dependency which creates a head or supplies an artifact says so.
Other edges are identified as declared order with an unformalized purpose;
unconnected work is visible for review. Requirements, obligations and semantic
review flags retain their original identities.

`hydra workflow plan compare left.json right.json` compares two structurally
admissible compiled snapshots. It reports node/edge counts, verification effort,
hard resource budgets, changed sections and changed check bindings. Matching
scope requires equal objectives, acceptance contracts, envelopes, data contracts,
source/context bindings and policy. Changing a recipe or dependency can be
compared within those boundaries. Different scope is reported, without ranking.

These commands are offline inspections. They do not execute work, confirm current
source freshness, establish semantic adequacy, accept a plan, or reuse artifacts.
Existing run admission still requires the reviewed exact digest. Whole-plan
validator binding remains in force: changing a compiled binding invalidates its
acceptance evidence. `compare` exposes this rather than skipping fresh checks.

## Optional estimates

Both commands accept `--estimates estimates.json`. Estimates use a closed schema
and must name the exact digest(s) of the inspected compiled artifacts:

```json
{
  "schema_version": 1,
  "plans": {
    "REPLACE_WITH_COMPILED_SHA256": {
      "source": "Operator estimate from the stated pilot measurements",
      "cost_unit": "USD",
      "steps": {
        "compose": {"milliseconds": [1000, 2000], "cost_microunits": [0, 0]}
      }
    }
  }
}
```

Ranges contain two ordered nonnegative integers, each at most one trillion.
Unknown steps/fields, duplicate JSON keys, stale digests and malformed ranges are
rejected. Individual metric estimates may be omitted. Aggregate metrics stay
`null` when a required input is unknown; a timeout is never used as a prediction.
Cost amounts use millionths of the declared unit and cannot be compared across
different units. The exact estimate-file digest is included in the result.

Critical-path ranges describe unlimited-resource execution of the declared fixed
graph. They exclude queueing, contention, transfer and recovery; scheduling and
host placement remain separate decisions. Transfer sizes and rework probabilities
stay unknown. Supplied ranges are not calibrated confidence intervals.

A modeled preference is reported only when matched candidates have nonoverlapping
critical-path ranges and no cost disadvantage. Overlapping/missing estimates,
tradeoffs, changed scope or an estimated lower bound exceeding a hard timeout
leave the choice unresolved. This does not claim measured planning benefit or
global optimality. [Finite patterns](PLAN_PATTERNS.md) add public serial/fork/join
outcomes and deterministic incorrect-artifact controls. Matched planner-quality
evaluation, estimate calibration and measured semantic benefit remain open 9E
qualification work.

## Verification

`make test-plan-inspection` runs public CLI cases for complete node/edge mapping,
unknowns, deterministic comparisons, exact estimate binding, hard-budget conflicts,
changed criteria/output contracts, malformed ranges and whole-plan invalidation.
The same cases also run against a UBSan build during native acceptance.
