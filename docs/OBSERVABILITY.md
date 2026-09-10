# Workflow observability projections

The workflow statistics feed is the single bounded evidence source for the
optional projections below. They are read-only and do not start, resume, or
retry a workflow.

```sh
hydra workflow statistics-json
hydra workflow statistics-announce
hydra workflow statistics-compare <left-statistics.tsv> <right-statistics.tsv>
```

`statistics-json` produces a versioned JSON document containing the sampled
runs, steps, warnings, and coverage counts. Missing scalar evidence is `null`;
it is unknown rather than zero. If collection cannot produce a valid feed, the
document has `availability: unavailable` and an empty projection.

`statistics-announce` emits one short ASCII line for the snapshot, each run,
each warning, and the final coverage count. A missing run state is announced as
unknown, and warning lines preserve the distinction between an empty sample and
failed evidence.

`statistics-compare` compares two previously captured feed files. It reports
the sampled run and step counts and their deltas. The result is unavailable if
either file is missing, symlinked, or unreadable. These counts describe the
recorded sample only; they do not infer remote transfer bytes, provider cost,
CPU, memory, tokens, or other remote metrics that the feed does not record.
