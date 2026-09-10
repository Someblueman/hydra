# 9D performance outcome

A single coordinated public workflow measurement compared the fixed POSIX shell line counter and awk candidate on the supplied 4003-line synthetic workload. Other Hydra CPU jobs were paused for measurement; unrelated system activity was recorded, not excluded. No measurements were rerun or dropped to select a favorable result.

- Retained source, state, compiled plan and raw output: `/var/folders/sp/gftbmpy17y1_q_6cp75p8gm40000gn/T/hydra-performance-public-_jfc4ufl`.
- Public run: `run_f307411e13bb283ccd61`; run and result exit 0.
- Accepted plan: `5a53f89d6cbb9913505d99163d9d47d381a9c16f95a640a8a307130fb40b82f9`.
- Final report: `5f5921880757a9fa5dabf94c9b1cb3ba6fab62691c0eec5edab60152a80dea96`.
- Raw CSV: `b4430a97172a4a4f2e930832698c684a756ce0d7cf0023e73792db9fd9cbf073`.
- Manifest: `ce122ebdf7ef690d29bdd6019ba18c7f410b9aee992dd5982a89623209ab0f2b`.

Two warmups per implementation preceded ten alternating AB/BA pairs (20 raw measured observations), with no exclusions, failures or retries. The manifest binds source/tree hashes, exact commands, workload, environment/tool binaries, nanosecond timer, observed load and the fixed stopping rule. An independent checker reconstructs counts, paired summaries, uncertainty and outcome from the sealed CSV and binds five obligations in schema-3 evidence.

The baseline median was 46.017354 ms; candidate median was 7.021604 ms. The paired median relative change was -85.148%, with a seeded 90% paired-bootstrap percentile interval [-85.997%, -84.085%]. This establishes the declared 10% target on this workload. It makes no Hydra-wide performance claim.

Fresh-process startup is included. Ten pairs give limited precision; sample p95 is the maximum observation, not a population-tail guarantee. The bootstrap assumes independent pairs and can understate correlated noise or drift. The paired effect and interval use the same estimator; these bounds are conditional on that assumption.

Five focused checker tests cover valid no-improvement evidence, missing/duplicated/wrong-result/nonzero/zero-time/bad-order trials, altered source/tree/workload/tool units/protocol, missing or failed warmups, recorded failures, altered raw bytes, false claims and intervals, and removed limits. Synthetic controls are explicitly separate from the real timing run. Valid no-improvement completes this measurement investigation as `target not established`; invalid or insufficient instrumentation fails acceptance.
