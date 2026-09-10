# Workflow observability projections

The workflow statistics feed is the single bounded evidence source for the
optional projections below. They are read-only and do not start, resume, or
retry a workflow.

```sh
hydra workflow statistics-json
hydra workflow statistics-compare <left-statistics.tsv> <right-statistics.tsv>
hydra workflow statistics-compare <left-statistics.tsv> <right-statistics.tsv> --format text
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
`--format text` displays the same validated summaries and deltas as plain ASCII
lines, with units, eligible/known denominators, observation times, partial-history
counts and recovery outcomes. No JSON extraction or terminal escape processing is
needed. Both feeds must validate before any comparison is printed. JSON remains
the default format; deltas always mean right minus left and imply no causal result.

## Saved task announcements

```sh
hydra fleet task announce --input observation.json --format text
```

This command reads one saved observation page and prints ASCII lines for the
recorded task state and each retained event. `--format text` needs no JSON
extraction and emits no terminal controls. The default `--format json` retains
the same announcement in the response field `data.announcement`. Invalid input
returns a nonzero status and a structured error before any announcement. It validates the normalized
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

Storage scope and preservation requirements are documented in [Retention](RETENTION.md).
The real two-receiver UI/CLI qualification is recorded in [V4 evidence](evidence/recovery-visibility-v4.md).
