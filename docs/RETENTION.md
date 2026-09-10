# Retained evidence and storage boundaries

`hydra fleet retention` previews or applies an explicit policy to local receiver
and workflow evidence in the selected `HYDRA_HOME`. It preserves execution identity,
protects active recovery and references, and discloses evidence expiry. It does not
remove Git worktrees, working copies or their directories.

```sh
hydra fleet retention preview --policy retention.json
hydra fleet retention pin run run_example
hydra fleet retention apply --policy retention.json
hydra fleet retention unpin run run_example
```

A policy has exactly these fields:

```json
{"schema_version":1,"audit_days":30,"max_bytes":67108864,"max_evidence_records":128}
```

`audit_days` is 1–3650, `max_bytes` is 4096–1 TiB, and
`max_evidence_records` is 0–1024. Preview reports selected records, protection
reasons, current file bytes and an upper bound including new expiry/audit metadata.
Apply refuses before expiry when protected evidence plus permanent receipts cannot
fit. The count limits records retaining raw evidence; permanent identity stubs and
filesystem allocation overhead are not counted as evidence records. The byte limit
includes their stored files. This is an explicit maintenance operation, not an
admission quota or a continuous disk-usage guarantee; active writers can add data.

## Preservation and audit policy

Raw evidence is retained in place throughout the audit window, measured from the
latest stored non-retention record modification. Successful apply records a durable
minimum `retain_until` for each unexpired record. A later shorter policy cannot
shorten that promise. Invalid audit metadata protects the record. An older record
without a prior declared window uses the policy supplied for this operation.

Active, queued, waiting, retrying, cancelled-but-unreconciled, unknown, owner-locked,
and otherwise unrecognized records remain protected. Every workflow step must be
terminal before its evidence can expire. Pins protect accepted claims for longer
than the audit window. Pinning a protected run also preserves its transitive local
task/run references, including a submission key whose acceptance acknowledgement
was lost. Expired evidence cannot be restored by pinning it.

The inventory follows recorded local task IDs, run IDs and submission keys in JSON
records. It cannot discover a reference in an external report, another host or an
unrecorded user intention: pin those claims and their local roots explicitly before
expiry. Retention cannot confer permission to replay an uncertain external effect.

| Record | Retention rule |
| --- | --- |
| Receiver acceptance and submission key | Kept permanently with original key, specification hash, project and task identity. Repeated submission reconciles the original receipt; a changed specification remains a conflict. |
| Receiver result, source bundle, package, inputs and logs | Kept during audit/recovery/reference protection; expire only after the window and references permit it. |
| Workflow contracts, observations, journals and sealed artifacts | Retained together while protected. Expiry removes raw evidence and contract payloads while preserving run identity, step states, attempt counters and expiry receipts. |
| Workspaces and Git worktrees | Excluded from evidence-byte accounting and traversal; never deleted by this command. Their storage needs a separate operator decision. |
| Head events | Separate `events retain` archive policy; default 32 protected archives, 64 MiB and 30 audit days. See [Events](EVENTS.md). |
| Provider diagnostics | Separate `--retain-raw` history, ten completed runs and at most 1 MiB per stream; it is not an acceptance archive. |

## Interruption, limits and visible expiry

The bounded inventory accepts at most 1024 records, 16384 files and directory depth
16. It requires owned directories without writable shared ancestors or symlinks,
refuses incomplete control JSON, and never follows a workspace link. A per-file
expiry hash is bounded to 16 MiB; larger evidence causes an inspectable refusal.

Before removing any raw file, Hydra writes and syncs a manifest of exact paths,
sizes and SHA-256 hashes, then an `expiring` receipt. Each unlink verifies the
original bytes through owned, link-free directories. A retry resumes that manifest;
it does not create replacement execution. New or changed bytes stop removal.
Previously completed removals remain disclosed if an operation is interrupted.

Task status keeps the historical runtime and reports `result_state=expired`.
Task results, logs and observations, workflow evidence, and plan result/repair gates
report `evidence_expired`; historical workflow status retains its state with an
`evidence_expired` flag. Missing evidence is never presented as an empty successful
result. Permanent manifests and receipts remain inspectable and can themselves
exhaust the quota; apply then refuses instead of forgetting deduplication history.

Focused tests cover audit monotonicity, pins/reference closure, protected capacity,
owner locks, inconsistent step/run states, corrupt receipts, symlinks and interrupted
expiry. The accepted-fixture check expires a copy, verifies original submission
bindings and explicit result refusal, and preserves workspace bytes. Operational
receiver homes were not pruned during qualification.
