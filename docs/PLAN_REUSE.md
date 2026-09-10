# Sealed artifact reuse during repair

Schema-2 plans may explicitly opt into version-1 same-run artifact reuse. The default remains whole-plan repair. This policy preserves original historical artifacts and execution receipts; it does not cache an external effect or predict a fresh execution result.

```json
"reuse_policy": {
  "schema_version": 1,
  "mode": "sealed_artifacts",
  "steps": {
    "produce": {
      "dependencies": "complete",
      "effects": "artifact_only",
      "environment_input": "environment"
    }
  }
}
```

Each listed step must be an executable task with no declared writes or dynamic source, repair-context or provenance inputs. Its named environment input must reference a declared global object input. Compilation binds that object to source bytes. Unsupported declarations fail compilation; there is no speculative fallback cache.

The environment object has closed version-1 fields: `schema_version`, a nonempty `scope`, a nonempty bounded `dependencies` list, `external_observations: false`, `effects: artifact_only`, `platform`, `tools` and `variables`. Platform is the exact `system`, `release` and `machine` from `uname`. Tools map symbolic command names to their resolved absolute `path` and SHA-256; the task's direct command must be present. Variables contain exactly PATH, LANG, LC_ALL, LC_CTYPE and TZ (unset values are empty strings). The native receiver normalizes LC_ALL to C. Empty or relative PATH components and active loader/shell-injection variables are unsupported. Current qualification covers local execution only.

Dependency completeness and artifact-only effects are explicit operator assumptions about the supplied code. Hydra verifies declared bindings; it does not establish arbitrary script purity, discover every implicit library dependency, or provide OS isolation. External observations, changing provider state and LLM outcomes are outside this reuse policy. Declare uncertain steps ineligible.

The repair coordinator starts with failed deliverable producers and the declared dependency graph. A step is retained only if it is allowlisted, succeeded on its original authoritative attempt, has passing applicable checks, and every predecessor is also retained. It reconstructs all input hashes, verifies every sealed output, checks the original dispatch and receipt, and revalidates the original result bundle. These identities and the environment/task/rule digests become a canonical repair proof bound to the accepted compiled plan.

Affected steps receive a new repair attempt. Retained steps continue to identify their actual original attempt; no replacement receipt is invented. The existing atomic pending-repair journal protects interrupted resets. Resume and final result retrieval revalidate reuse proofs. Affected checks must run again against their new subject, and an unchanged failed subject cannot pass merely because a later report says PASS. The compiled repair count, timeout, head and artifact budgets still cover every admitted round; reuse does not enlarge them.

## Qualification

The real headless public fixture combines a correct intermediate artifact with an initially incorrect final composition. Selective repair preserves `produce` and `inspect` at attempt 1, reruns `compose` and `verify` at attempt 2, and accepts six receiver tasks instead of the eight used by whole-plan repair. This is a count of avoided work, not a latency or general planning-benefit measurement.

`HYDRA_TEST_PLAN_REPAIR=combine HYDRA_TEST_PLAN_REUSE=1 HYDRA_TEST_PLAN_REPAIR_FAULT=1 sh tests/test_workflow_plan_task.sh` interrupts the per-step reset and resumes the same run and repair journal. Default and UBSan executions passed, including ordered first-ready, first-dispatch and terminal-observation timestamps for all four task steps. The original candidate, checks, input manifest and result bytes remain inspectable. `tests/test_plan_reuse_invalidation.py FIXTURE` checks a passing baseline and corrupted copies through the public result gate; it verifies canonical hash parity before recomputing a tampered repair digest.
The public compiler also accepted the original policy and rejected nine unsupported
reuse declarations (version, mode, unknown keys/steps, incomplete dependencies,
external effects, unbound environment, repair input and provenance input).

Final default fixture: `/var/folders/sp/gftbmpy17y1_q_6cp75p8gm40000gn/T/tmp.hpHdK6HWYK`,
run `run_857ba3d3ceb46c8b93f0`, repair proof SHA-256
`725cc52dfdb88e0687dd804c061b6201bd267f5717563f9903d7d0a00ed0be7d`.
Final UBSan fixture: `/var/folders/sp/gftbmpy17y1_q_6cp75p8gm40000gn/T/tmp.W3XYEdYsCr`,
run `run_02adb833551bd3cfcd20`. The existing whole-plan combine, exhausted-budget,
unchanged-subject and crashed-checker regressions also passed.

Static serial/fork/join patterns and separately compiled stages remain the graph boundary; see [bounded patterns](PLAN_PATTERNS.md). Runtime map/conditional expansion is not admitted by this policy. New scientific questions, sources, contracts or acceptance methods require a new compiled plan, rather than an old repair round.
