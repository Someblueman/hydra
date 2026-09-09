# Hydra host discovery and onboarding — decision brief

## Bottom line

The safest useful meaning of “seamless onboarding” is: discover candidates from inventories the operator already controls, qualify them with the existing strict SSH and Hydra handshake, show an immutable plan, then apply only the reviewed actions. Discovery must reduce manual translation; it must not turn a hostname, VPN membership, cloud tag, mDNS record, or successful SSH login into automatic execution authority.

This brief is a discussion aid. It records current evidence and new proposals separately. It does not update Hydra's roadmap.

## Strongest findings

1. **Hydra is intentionally explicit and SSH-centered today.** Fleet aliases are named records under the fleet home and point to normal OpenSSH configuration. BatchMode and strict host-key checking are required; the current native fleet records do not include an inventory adapter, mDNS browser, VPN API, cloud inventory client, or automatic enrollment path. See [FLEET.md](https://github.com/Someblueman/hydra/blob/2c307c82e97b948ecd6965d8d33e7079a3701eca/docs/FLEET.md), [fleet CLI](https://github.com/Someblueman/hydra/blob/2c307c82e97b948ecd6965d8d33e7079a3701eca/src/fleet/cli.c), and [fleet header](https://github.com/Someblueman/hydra/blob/2c307c82e97b948ecd6965d8d33e7079a3701eca/src/fleet/fleet.h).

2. **The existing handshake and bounded aggregation are reusable qualification primitives.** Hydra already checks protocol versions, capabilities, project mappings, bounded SSH deadlines, worker limits, output limits, deterministic ordering, and typed per-host failures. The current observer is capped at 16 hosts per bounded aggregation, with a default of four workers. See [aggregate transport](https://github.com/Someblueman/hydra/blob/2c307c82e97b948ecd6965d8d33e7079a3701eca/src/fleet/transport/aggregate.c) and [receiver server](https://github.com/Someblueman/hydra/blob/2c307c82e97b948ecd6965d8d33e7079a3701eca/src/fleet/transport/server.c).

3. **Bootstrap is already a high-integrity, explicit operation.** The pinned package, installer bytes, staged qualification, immutable digest-named prefix, allowlisted paths, and strict remote command are stronger foundations than a new discovery-specific installer. See [bootstrap implementation](https://github.com/Someblueman/hydra/blob/2c307c82e97b948ecd6965d8d33e7079a3701eca/src/fleet/transport/bootstrap.c) and [onboarding acceptance](https://github.com/Someblueman/hydra/blob/2c307c82e97b948ecd6965d8d33e7079a3701eca/tests/test_onboarding.sh).

4. **Remote-task recovery rules must carry into onboarding.** Receiver-owned acceptance, idempotency keys, outcome_unknown, durable ownership, reconciliation, and no blind replay are not optional details for a batch bootstrap/apply workflow. See [REMOTE_TASKS.md](https://github.com/Someblueman/hydra/blob/2c307c82e97b948ecd6965d8d33e7079a3701eca/docs/REMOTE_TASKS.md) and [REMOTE_TASK_ACCEPTANCE.md](https://github.com/Someblueman/hydra/blob/2c307c82e97b948ecd6965d8d33e7079a3701eca/docs/REMOTE_TASK_ACCEPTANCE.md).

5. **External inventories provide coordinates and metadata, not trust.** OpenSSH config preserves user/jump-host policy; ssh-keyscan explicitly requires out-of-band verification. mDNS is link-local, VPN/cloud inventories can be stale or credential-scoped, and Ansible inventories may carry variables outside Hydra's authority. See [ssh_config](https://man.openbsd.org/ssh_config), [ssh-keyscan](https://man.openbsd.org/ssh-keyscan), [RFC 6762](https://www.rfc-editor.org/rfc/rfc6762), [Tailscale API](https://tailscale.com/docs/reference/tailscale-api), [AWS SSM Inventory](https://docs.aws.amazon.com/systems-manager/latest/userguide/systems-manager-inventory.html), and [Ansible inventory](https://docs.ansible.com/projects/ansible/latest/inventory_guide/intro_inventory.html).

## Implications for Hydra

- Add a candidate/source layer, but keep aliases, verified keys, package decisions, project trust, and remote state as separate records.
- Make the first release an SSH-config/static-inventory adapter. It gives the biggest reduction in setup friction with the least new authority.
- Use a state progression such as discovered → normalized → deduplicated → transport-checked → host-key verified → authenticated → Hydra-compatible → optionally bootstrapped → project-mapped → explicitly trusted → ready.
- Require an immutable, digest-bound plan before any alias mutation, bootstrap, project-trust change, or other mutation.
- Treat changed keys, identity conflicts, stale source data, missing prerequisites, and lost mutation responses as review/unknown states, not ordinary retries.
- Preserve per-user file-backed state and strict permissions. Do not add a daemon, database, provider-wide controller, or shared credential store for the first slice.

## Prioritized options and trade-offs

| Priority | Option | Benefit | Cost / boundary |
|---|---|---|---|
| P0 | SSH-config/static discovery + candidate records + read-only probe | Immediate one-host and small-fleet improvement; reuses OpenSSH and current handshake | Still requires explicit host-key decision and inventory hygiene |
| P1 | Immutable plans + staged apply/resume/reconcile | Makes 1/10/50-host onboarding explainable and recoverable | Requires new schemas, event records, and failure-injection tests |
| P2 | Opt-in mDNS, VPN/Tailscale, cloud-tag, Ansible adapters | Better discovery and filtering for LAN, VPN, cloud, and existing ops inventories | Adds source credentials, freshness/cache behavior, provider scope, and conflict cases |
| P3 | 50/100-host batching, snapshot diffs, TUI | Scales the operator surface without changing trust semantics | More UX and resource-budget work; must retain the 16-host bounded observer limit per batch |

## Existing milestones versus new proposals

**Already implemented in the pinned checkout:** explicit SSH aliases; strict BatchMode/host-key behavior; versioned handshake/capability evidence; bounded aggregation; pinned package/bootstrap; fleet and onboarding tests; receiver-owned remote-task acceptance and reconciliation.

**Planned but not delivered:** roadmap T1/T2 headless/tmux-independent execution, 9A satisfiable outcomes, and V1–V4 freshness/attempt/recovery surfaces. See [ROADMAP.md](https://github.com/Someblueman/hydra/blob/2c307c82e97b948ecd6965d8d33e7079a3701eca/docs/ROADMAP.md). Discovery should integrate with these later; it must not claim them as prerequisites already shipped.

**New proposals in the report:** candidate/source schema; conservative layered identity and dedupe; plan/event/tombstone records; hydra host CLI/TUI; deterministic 16-host batches; opt-in source adapters; measurable 1/10/50/100-host acceptance matrix.

## Uncertainties and contradictory evidence

- A provider can report a stable node/resource ID while the SSH key, address, or installed Hydra state has changed. Provider identity must therefore remain evidence, not transport trust.
- mDNS can improve local “what is nearby?” discovery but is not a cross-subnet inventory and is not an authentication mechanism.
- Cloud/CM inventory freshness may be minutes or longer; live SSH observation can contradict it. The product needs source timestamps and snapshot diffs rather than a single “truth” field.
- The current 16-host observation cap is a bounded transport contract, not evidence that a 100-host inventory should be rejected. The safe scale proposal is deterministic batching with explicit local resource budgets.
- Existing bootstrap requires Git and tmux. Headless/tmux-optional work is separately planned; discovery should classify missing prerequisites rather than broaden bootstrap silently.

## Decisions needed before roadmap work

1. **Default source:** approve SSH config/static inventory as the first source; keep mDNS, VPN, cloud, and Ansible adapters opt-in.
2. **Host-key ownership:** confirm that Hydra will display fingerprints and record decisions but will not auto-accept or rewrite OpenSSH host keys.
3. **Bootstrap policy:** choose whether the plan offers bootstrap by default after review, or only when the operator passes an explicit bootstrap flag.
4. **Project scope:** decide whether initial onboarding maps one explicit project per host or only qualifies host capability and leaves project mapping to a later command.
5. **Shared state:** confirm per-user fleet state for v1; defer team-shared inventory/audit backend until its authorization model is designed.
6. **Scale target:** choose whether the first acceptance target is 10 hosts, with 50/100-host batching as a follow-on, or require the batching contract in the first release.
7. **Roadmap placement:** after these choices, decide which approved proposal belongs in the roadmap; no roadmap entry has been changed by this research.


