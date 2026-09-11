# Archived planner evaluation pilot

This superseded toy runner was retired during the real Python cleanup workload.
Its candidates, contracts, held-out cases and execution code remain reproducible
at baseline commit `b0fb5b1d9220189ad2498a244274ae498a3c7d0c`. The evidence below
belongs to that source snapshot. Removing the active runner does not change its
recorded observations or qualify broader 9E planning benefit.

## Reproduce the historical packet

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

## Observations and limits

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
