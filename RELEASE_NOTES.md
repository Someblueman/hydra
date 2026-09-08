# Hydra v2.3.0 Release Notes

Release date: 2026-09-08

Hydra 2.3.0 adds finite distributed workflows and shared resource admission.
Explicitly placed tasks exchange verified files and Git results, while independent
checks gate composition and final delivery.

## Highlights

- Run workflows across selected trusted hosts with durable coordinator ownership,
  stable task receipts, verified handoffs, and deterministic scheduling replay.
- Compile schema-2 distributed plans with artifact-bound validation joins and
  version-2 reports tied to accepted check definitions. Repair rejected candidates
  within an accepted whole-graph budget, then validate the combined result.
- Share FIFO admission across local execution, gates, head startup and resume,
  queued spawns, and remote tasks and workflows. Receiver policy controls host and
  project concurrency, disk floors, labels, and queue bounds.
- Inspect reservations and queue reasons with `hydra admission` and
  `hydra fleet admission HOST`. Unknown execution retains capacity; resume keeps
  the original task identity, including after result-collection transport loss.
- Reject incomplete admission policies, orphan plan checks, corrupt results, and
  changed handoff identities before they can authorize dependent execution.

## Compatibility and upgrade

This is a backward-compatible minor release. Upgrade the shell CLI and optional
native helpers together to 2.3.0, including the selected fleet hosts. Existing state
v2 and core, TUI, and fleet protocol versions remain unchanged. Existing local plans
and command workflows remain supported; distributed plans and admission policy are
opt-in additions. An absent admission policy retains the default limits; an
existing policy must contain every documented field.

The attached tar.gz and zip archives contain the exact release source. Verify them
with `SHA256SUMS`, unpack, and run `sh install.sh`. The shell CLI remains
compiler-free. Optional native helpers build with
`make build-core build-tui build-fleet`; fleet requires JSON-C development files
and pkg-config, with JSON-C linked statically.

## Scope and qualification limits

Distributed execution uses finite graphs, explicit placement, and one original
coordinator. Coordinator failover, automatic reassignment, dynamic expansion, and
scheduled pools remain outstanding work. A collected result does not grant review
approval or permission to promote it.

The retained [two-host acceptance evidence](https://github.com/Someblueman/hydra/blob/v2.3.0/docs/evidence/distributed/qualification.md)
uses executable fixtures on macOS and Linux through the public task interfaces;
it does not establish live AI-provider conformance or identical worker outputs.
See [distributed workflows](https://github.com/Someblueman/hydra/blob/v2.3.0/docs/DISTRIBUTED_DAG.md)
and [admission policy](https://github.com/Someblueman/hydra/blob/v2.3.0/docs/ADMISSION.md)
for the contracts and recovery behavior.

Linux sanitizer runs retain a pre-existing, nonfatal UBSan diagnostic in the empty
record-list sort at `src/libhydra.c:409`. It is unchanged by this release; passing
CI must not be interpreted as an absence of sanitizer diagnostics.
