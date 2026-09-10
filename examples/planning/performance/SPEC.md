# Performance objective: count workflow records

Compare two implementations of the same line-count workload over the supplied
`records.txt`: a POSIX shell loop (baseline) and one `awk` pass (candidate).
The task is a measurement qualification, not a claim about Hydra-wide
performance.

Requirements:

- `binding`: identify exact baseline and candidate scripts, workload bytes and
  digest, host/OS/shell/toolchain, units, and command forms.
- `protocol`: perform two warm-up runs, then ten alternating baseline/candidate
  trials in one exclusive process, recording every raw elapsed sample in
  nanoseconds; do not discard failures or cherry-pick trials.
- `analysis`: report sample counts, failures, median and nearest-rank p95 for
  each implementation, and candidate change relative to baseline. Use the
  raw CSV as the authority and state the stopping rule: ten valid paired trials
  or an invalid result if any trial fails.
- `outcome`: classify the result as target established only if candidate median
  is at least 10% lower and both p95 values are available; otherwise report
  target not established or invalid/insufficient measurement as appropriate.
- `limits`: explain that this is one synthetic text workload on one host and
  does not establish Hydra-wide CPU, latency, scalability, or user-facing
  improvement.

The independent checker must recompute the digest, trial counts, summaries and
classification from the sealed raw output and fail on missing, altered or
inconsistent evidence.
