# Planner evaluation: archived pilot and current runner

This directory holds the current planning-quality runner inputs and the record
of the archived finite pilot. The archived pilot's candidates, contracts, cases,
oracle and execution code remain reproducible only at baseline commit
`b0fb5b1d9220189ad2498a244274ae498a3c7d0c`; they are never edited here.
`scripts/plan-eval.py` exports them from Git history on every run and refuses to
continue when any byte differs from [archive-manifest.json](archive-manifest.json).

## Current runner

```sh
make build-fleet build-plan-example
make test-plan-eval                      # python3 scripts/plan-eval.py --execute 1
python3 scripts/plan-eval.py             # structural and oracle phases only (no tmux)
python3 scripts/plan-eval.py --execute 3 --repeat 5 --output build/plan-eval/report.json
```

Inputs beside this README:

- [heldout-v3-additions.json](heldout-v3-additions.json): eight held-out cases
  (three feature-correctness, three finite-research-claim, two
  manifest-membership) with sixteen declared incorrect artifacts, specified
  after the six candidates were frozen and never shown to them. Every case stays
  inside the archived visible contracts; a case that would need a contract
  clarification (for example the wording of a positive threshold claim) was not
  admitted.
- [oracle.py](oracle.py): an independent oracle that computes each expected
  artifact from the case inputs and the contract text, checks it against the
  declared acceptance before use, compares artifacts with strict JSON types and a
  64 KiB bound, and runs a candidate in a separate `python3 -I` interpreter with
  empty stdin and a five-second timeout.
- [archive-manifest.json](archive-manifest.json): the archived commit, the
  SHA-256 of every archived file the runner uses, and the two candidate arms.

The report (`build/plan-eval/report.json`, schema in
[CONTRACTS](../../../docs/CONTRACTS.md#workflows-and-integration)) separates
three effects under the archived matched contract (same scope, tools, budgets,
policy and checker bytes for every candidate):

| Effect | What is measured | Source |
| --- | --- | --- |
| Planning | Acceptance of each frozen candidate on all seventeen cases under the declared interface and under the archived post hoc interface adaptation, by task class and source; repeated invocations and distinct outcomes | oracle |
| Validation | False rejections of the seventeen correct artifacts and false acceptances of the thirty-four declared corruptions, by task class | oracle |
| Scheduling | The serial and fork/join patterns compiled by the current compiler and compared with `hydra workflow plan evaluate`; with `--execute`, both run, their recorded durations become estimates through `plan observed`, and one calibration compares a prediction with an observed run | Hydra CLI |
| Execution | With `--execute N`, the first N candidates per arm run the archived evaluation plan through `hydra workflow plan run` at the current source: verified outcome, elapsed time, repeated work, cost and human corrections | Hydra CLI |

## Results at the current source (17 October 2026)

Measured by `make test-plan-eval` on this checkout with the fleet test suite
idle; `n` is one run per executed plan and two invocations per candidate and
case. Timings are descriptive elapsed measurements on a shared four-core machine,
not isolated overhead or speedup estimates.

| Arm / candidate | Declared interface | Adapted interface | Distinct outcomes (2 repeats) |
| --- | ---: | ---: | ---: |
| current-1, current-2, current-3 | 0/17 each | 17/17 each | 1 |
| contract-aware-1, contract-aware-2, contract-aware-3 | 17/17 each | 13/17 each | 1 |

- The archived nine-case outcomes are reproduced exactly (contract-aware 9/9,
  current 0/9 under the declared interface) and the runner fails if they are not.
- On the eight held-out additions the pattern is the same: 8/8 for every
  contract-aware candidate, 0/8 for every current candidate under the declared
  interface. Under the archived interface adaptation every current candidate
  passes all seventeen cases, while the contract-aware candidates then fail the
  four file-input cases (`fc-02`, `fc-05`, `fr-01`, `fr-04`): each arm reads the
  same contract through a different input convention. The observed difference
  between the arms is interface conformance, in both directions. It is not a
  semantic planning advantage.
- Validation: 34/34 declared corruptions rejected, 0 false acceptances, 0 false
  rejections, in every task class.
- Executed through the current compiler and runtime: the archived evaluation plan
  compiles for every candidate; `contract-aware-1` ran to a verified result
  (`plan result` ok, checker 9 executed / 0 failed; `plan run` 13.1 s, recorded
  run 11 s, time to verified result from recorded durations [4000, 10000] ms) and
  `current-1` did not (checker 9 executed / 9 failed, `plan run` 9.7 s), both as
  the archived record expects. No step was attempted twice; provider cost is
  unknown (local Python candidates); human corrections are unknown (not
  observed; nobody intervened).
- Scheduling: serial and fork/join patterns are matched alternatives with depth
  5 versus 4 nodes and the same widest level (4); decomposition and scheduling
  preferences are `unresolved` (no dominated metric; one-second ranges overlap)
  and placement is `not_assessed`. Both runs at parallelism 4 took 14 s of
  recorded elapsed time with verified results; the fixed graph is too small for
  the one-second step resolution to separate them.
- Calibration (n = 1): predicting the fork/join run from the serial run's step
  durations gave [1000, 8000] ms for the time to a verified result; the fork/join
  run's own recorded elapsed time was 14000 ms, outside the range. The
  fixed-graph model ignores scheduler polling, head creation latency and
  transfers, so it under-predicts; this is reported, not corrected. Estimate
  calibration by task class is unknown: the frozen candidates supplied no
  estimates.
- Stochastic variability is not exercised: candidates and fixtures are
  deterministic and every repeated invocation produced the same outcome.

These results support keeping the independent checker and making the candidate
interface explicit. They do not justify a more elaborate planner and establish no
planning benefit that exceeds its overhead; that remains open and needs a real
workload with live agents, matched budgets and recorded human corrections.

## Archived pilot

This superseded toy runner was retired during the real Python cleanup workload.
Its candidates, contracts, held-out cases and execution code remain reproducible
at baseline commit `b0fb5b1d9220189ad2498a244274ae498a3c7d0c`. The evidence below
belongs to that source snapshot. Removing the active runner does not change its
recorded observations or qualify broader 9E planning benefit.

### Reproduce the historical packet

Export the complete historical source into a disposable directory. This uses Git
history without switching this checkout or creating a Git worktree:

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

The public driver retains its own disposable source repositories, exact command
logs, run identities and independently computed schema-3 reports. Its expected
negative candidates are semantic outcomes; infrastructure failures are failures.
The optional active Make target was removed with the runner. The archived target
remains available in the export above.

### Observations and limits

| Frozen candidate | Accepted cases | Incorrect controls rejected | Generation seconds | Direct evaluation seconds | Public execution seconds |
| --- | ---: | ---: | ---: | ---: | ---: |
| contract-aware-1 | 9/9 | 18/18 | 87 | 0.220 | 8.422 |
| contract-aware-2 | 9/9 | 18/18 | 38 | 0.207 | 8.910 |
| contract-aware-3 | 9/9 | 18/18 | 34 | 0.220 | 9.742 |
| current-1 | 0/9 | 18/18 | unknown | 0.218 | 7.840 |
| current-2 | 0/9 | 18/18 | 67 | 0.219 | 8.044 |
| current-3 | 0/9 | 18/18 | 67 | 0.223 | 7.580 |

The public timings include Hydra execution and checking while the full fleet suite
was also running. They are descriptive elapsed measurements, not isolated overhead
or speedup estimates. Case rejection in the current arm is rejection of a candidate
that violates the declared interface; it is not evidence that the checker rejected
a correct artifact. All eighteen distinct incorrect controls were rejected on each
invocation; repeated invocations do not add new corruption coverage.

Direct held-out evaluation accepted all nine cases for each contract-aware
candidate and none for each current candidate. All eighteen declared corruptions
were rejected. A separately recorded post hoc diagnostic adapts named-file input
maps and unwraps output-file maps: after that protocol adaptation, all three
current candidates also pass all nine cases. The observed difference is interface
conformance. It does not demonstrate a semantic planning advantage or a benefit
that exceeds planning overhead. The diagnostic is not the primary outcome and does
not retroactively change the declared API.

The evaluation inputs retain every declared case, corruption result, candidate hash
and generation record.

Recorded generation durations are 67 and 67 seconds for current candidates 2 and 3,
and 87, 38 and 34 seconds for the contract-aware candidates. Current candidate 1's
duration was not recorded. Direct nine-case evaluation took approximately
0.20–0.23 seconds per candidate, including process startup and imports; reported CPU
time covers the timed worker region. These small samples, repeated generation in
continuing agent contexts, and prior exposure to the visible packet do not support
a population estimate of stochastic variability. Monetary/provider cost, human
correction effort and estimate calibration remain unmeasured. Candidate estimates
are preserved as supplied and must not be treated as measured costs.

The original held-out v1 file was frozen before candidate generation. Review found
that its hash-bound case supplied fabricated digests and no corresponding bytes,
making the expected passing answer unsatisfiable. Before evaluating any candidate,
v2 repaired only that case with explicit bytes and their real SHA-256 digests. V1
remains in the archived source for audit. V2 was created after generation, so its inherited freeze
label is not evidence of a pre-generation v2 freeze. The oracle recomputes hashes
and enforces JSON types independently; it does not accept a claimed digest or
Python's equivalence between booleans and integers.

The broader 9E planning-benefit requirement remains open. These results support keeping independent checking and
making the candidate interface explicit; they do not justify more elaborate
planning machinery or relaxed budgets.
