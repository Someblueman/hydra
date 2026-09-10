# Performance objective: count workflow records

Compare two implementations of the same line-count workload over the supplied
`records.txt`: a POSIX shell loop (baseline) and one `awk` pass (candidate).
The task is a measurement qualification, not a claim about Hydra-wide
performance.

Requirements:

- `binding`: identify exact baseline and candidate script hashes, workload bytes and
  digest, host/OS/native monotonic timer environment, units, and command forms.
- `protocol`: perform two warm-up runs, then ten alternating baseline/candidate
  trials in one exclusive process, recording every raw elapsed sample in
  nanoseconds; do not discard failures or cherry-pick trials.
- `analysis`: report sample counts, failures, median and nearest-rank p95 for
  each implementation, plus a seeded paired bootstrap percentile interval for
  the median relative change. Use raw CSV as authority and state the stopping
  rule: exactly ten pairs, including failures, with no exclusions or retries.
  A failed warm-up, trial, timeout, incorrect count or incomplete set is invalid.
- `outcome`: classify the result as target established only if the median paired
  relative change is at most -10% and the upper bootstrap interval is below zero;
  the effect and interval use the same estimator. Otherwise report
  target not established or invalid/insufficient measurement as appropriate.
- `limits`: explain that this is one synthetic text workload on one host and
  does not establish Hydra-wide CPU, latency, scalability, or user-facing
  improvement.

The independent checker must recompute the digest, trial counts, summaries, paired
interval and classification from the sealed raw output and fail on missing, altered
or inconsistent evidence. Altered and truncated raw evidence are explicit negative controls.

Two warm-ups per implementation precede ten pairs, with baseline/candidate order
reversed on every pair. Timing includes fresh process startup. At ten observations
the nearest-rank p95 is the sample maximum and cannot establish a population tail.
The 90% paired bootstrap percentile interval assumes independent trial pairs;
temporal drift and correlated system activity can make it overconfident.

The measurement owner requires `HYDRA_PERFORMANCE_EXCLUSIVE=1` after coordinating
a quiet window for other Hydra jobs. This flag records operator coordination; it
does not isolate the computer or exclude unrelated system activity. Raw load
observations and exact source, workload, shell, awk and native executable hashes are retained.
A correctly classified valid result can complete the investigation whether the
target is established or not. Invalid instrumentation does not pass the checker.

Native analysis admits positive integer nanoseconds up to 2^53-1 so conversion
to floating-point ratio arithmetic is exact. Larger values produce invalid
measurement evidence; the ten-second sample timeout is far below this bound.
