# Planner evaluation

This directory holds the inputs of the planning-quality runner,
`scripts/plan-eval.py`: an independent oracle over held-out cases, and the
serial/fork-join scheduling comparison it runs through the current compiler.

```sh
make build-fleet build-plan-example
make test-plan-eval                      # python3 scripts/plan-eval.py --execute 1
python3 scripts/plan-eval.py             # oracle and structural comparison only (no tmux)
```

The report is `build/plan-eval/report.json` (schema in
[CONTRACTS](../../../docs/CONTRACTS.md#workflows-and-integration)). The runner
needs full Git history, because it exports the archived case file from commit
`b0fb5b1d9220189ad2498a244274ae498a3c7d0c` and refuses to continue when any byte
differs from [archive-manifest.json](archive-manifest.json).

Inputs beside this README:

- [heldout-v3-additions.json](heldout-v3-additions.json): eight held-out cases
  (three feature-correctness, three finite-research-claim, two
  manifest-membership) with sixteen declared incorrect artifacts. Every case stays
  inside the archived visible contracts; a case that would need a contract
  clarification was not admitted.
- [oracle.py](oracle.py): an independent oracle that computes each expected
  artifact from the case inputs and the contract text, checks it against the
  declared acceptance before use, and compares artifacts with strict JSON types
  and a 64 KiB bound.
- [archive-manifest.json](archive-manifest.json): the archived commit and the
  SHA-256 of the archived held-out case file.

## What is reported

| Effect | What is measured | Source |
| --- | --- | --- |
| Validation | False rejections of the seventeen correct artifacts and false acceptances of the thirty-four declared corruptions, by task class; any miss fails the run | oracle |
| Scheduling | The serial and fork/join patterns compiled by the current compiler and compared with `hydra workflow plan evaluate`; with `--execute 1`, both run, their recorded durations become estimates through `plan observed`, and one calibration compares a prediction with an observed run | Hydra CLI |

## Limits

- The oracle shows that the checker rejects its declared corruptions and accepts
  the correct artifacts. It measures no planner and establishes no planning
  benefit over its overhead; that needs a real workload with live agents, matched
  budgets and recorded human corrections.
- Scheduling uses one run per fixed graph at one-second step resolution. The
  graph is too small to separate serial from fork/join, and the fixed-graph
  model ignores scheduler polling, head creation latency and transfers, so the
  calibration prediction is expected to under-predict. Estimate calibration by
  task class, provider cost and human corrections are unknown.
- Fixtures are deterministic, so stochastic planner variability is not exercised.

## Archived pilot

The archived finite pilot's six frozen candidates (three `current`, three
`contract-aware`), contracts, execution code and measurements remain
reproducible only at baseline commit `b0fb5b1d9220189ad2498a244274ae498a3c7d0c`
and are never edited. Its candidate-arm comparison showed an interface
conformance difference between the arms, not a semantic planning advantage.
The runner no longer executes the candidates; the earlier version of this
README at commit `c82547c` records those observations. To reproduce the pilot:

```sh
archive="$(mktemp -d)"
git archive b0fb5b1d9220189ad2498a244274ae498a3c7d0c | tar -x -C "$archive"
cd "$archive"
mkdir -p build
python3 examples/planning/evaluation/evaluate.py \
  --candidate examples/planning/evaluation/candidates/contract-aware-1/candidate.py \
  --output build/planner-contract-aware-1.json
make build-core build-fleet
python3 tests/test_planner_evaluation_public.py \
  --output build/planner-public.json
```
