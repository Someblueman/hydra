# Workflow observability projections

The workflow statistics feed is the single bounded evidence source for the
optional projections below. They are read-only and do not start, resume, or
retry a workflow.

```sh
hydra workflow statistics-json
hydra workflow statistics-compare <left-statistics.tsv> <right-statistics.tsv>
```

`statistics-json` produces a versioned JSON document from the validated native
statistics model. It includes the observed timestamp, filter snapshot, matched
run and step cohort, and queue, elapsed, verified, and recovery metrics. Timing
values are seconds; recovery values are counts. Each metric carries eligible
and known denominators plus sum, integer mean, maximum, nearest-rank p50 and
p95 when samples are known. A metric with eligible records but no known samples
is `unknown`; a metric with no eligible records is `unavailable`. Missing values
are `null`, never zero. Invalid, truncated, or oversized feeds fail closed.
New task attempts record coordinator dispatch and terminal observation times.
First ready-to-dispatch queue time survives repair and reconciliation; these
values do not measure the receiver's separate admission queue. Earlier receipts
without these fields remain unknown.
The `coverage` object reports partial runs and bounded-feed warnings, so a valid
partial sample is not mistaken for complete history.

`recovery_outcomes` counts matched runs with a known positive coordinator
owner-recovery count. Successful unplanned runs and successfully verified planned
runs count as success; failed or cancelled runs count as unsuccessful. A still
running or unverified recovered run remains unknown. The fraction uses only the
known terminal denominator, and missing recovery history has its own count.
Approval continuation and automatic step retries are not owner recovery.

The current workflow feed does not record receiver unknown outcomes, total manual
interventions or actual network transfer bytes. Those fields remain explicitly
unmeasured. An approval count or package file size would measure narrower things
and is not substituted for them.

`statistics-compare` loads two previously captured feed files through the same
native parser and emits both metric summaries plus deltas for mean, maximum,
p50, and p95. It preserves unknown and unavailable states rather than ranking
incomparable cohorts. These are recorded local samples only; remote transfer
bytes, provider cost, CPU, memory, and tokens are explicitly unavailable.

## Saved task announcements

```sh
hydra fleet task announce --input observation.json
```

This command reads one saved observation page and returns an ASCII announcement
in the JSON response field `data.announcement`
for the recorded task state and each retained event. It validates the normalized
task identity and observation schema before reading the event page, accepts at
most 128 events, and requires strictly increasing contiguous sequence numbers.
Event time, run, step, sequence, type, and detail are printed on one line;
control characters are replaced so a saved payload cannot inject terminal
control sequences. The header identifies the receiver observation time as a
saved snapshot, not a freshness claim.

The output reports unavailable history, retention gaps, stream resets, and
truncated scans explicitly. Its final line exposes the resumable cursor,
byte offset, and stream ID. Those values must be retained together when a
caller resumes observation; a changed stream ID requires starting from the
reported reset offset.
