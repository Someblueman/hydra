# Host discovery and read-only qualification

H1 adds two commands to the existing fleet namespace. Neither registers an alias
or changes a receiver's installation, state, project mappings, or trust.

```sh
hydra fleet discover --ssh build --ssh mac --json
hydra fleet qualify --ssh build --require list --timeout 5 --json
hydra fleet discover --inventory /path/hosts.json --select build --select mac --json
```

`discover` resolves explicitly selected targets with OpenSSH `ssh -G`. It makes
no SSH connection and sends no remote command. `qualify` resolves the same
selection, then sends exactly the existing fleet protocol-1 `handshake` request
through fleet's SSH transport. It checks the existing Hydra 2.x, fleet, state,
event, and JSON compatibility gate and the advertised required capability
(`list` by default). A successful handshake also records `data.peer_fingerprint`,
taken from the authenticated SSH session's peer-key exchange; it is not copied
from known_hosts or inventory text. Even `--require spawn` only checks the
advertisement; it does not invoke `spawn`. Compatibility is not project trust,
enrollment, proof that an advertised operation works, or permission to submit work.

Repeat `--ssh [USER@]ALIAS`, or supply one `--inventory` and repeat `--select NAME`.
There is no scan, wildcard alias expansion, automatic selection of an entire
inventory, or dynamic inventory execution. Direct SSH selection allows at most
16 aliases; explicit inventory selections allow at most 100 distinct targets.
Repeated identical selectors are ignored. The selected inventory must be a
regular file of at most 1 MiB, with at most 1,024 schema-1 static records or 100
schema-2 source records. Qualification processes at most 16 targets per call;
larger selections require an absolute `--progress` export path and repeated calls.
See [source snapshots and durable resume](HOST_ENROLLMENT.md) for the private
progress binding and its distinction from the public export.
Invalid or missing selectors reject the whole input before OpenSSH runs.

Both commands always print one JSON envelope, including without `--json`.
Results are source snapshots on stdout; a saved public result is never imported
as authority. Qualification with `--progress` retains private bound progress and
marks previously successful rows as requiring reauthentication. To keep a snapshot, use
`umask 077` before redirecting output to an operator-selected file. Refresh by
repeating the selection and compare retained snapshots by candidate ID. A missing
source in a later snapshot deletes neither an alias nor a previous snapshot.

## Trusted OpenSSH configuration boundary

By default, Hydra includes readable `~/.ssh/config` and `/etc/ssh/ssh_config`.
`--ssh-config /absolute/literal/path` selects another operator-trusted file,
omitting the system config as OpenSSH's `-F` does. OpenSSH evaluates nested
Include, Host, Match, User, Port, IdentityFile, HostKeyAlias, and ProxyJump;
Hydra does not approximate them with its own config parser. Explicit paths with
glob, quote, escape, or expansion characters are rejected. Discovery never reads
or copies private keys.

SSH configuration is executable operator policy, **not untrusted inventory**.
`Match exec` can run local commands even during `ssh -G`; `ProxyCommand` can run
local commands when connecting. Only use configuration you already authorize
OpenSSH to evaluate. H1 does not claim arbitrary configuration is inert or
sandbox custom proxy programs. The no-mutation guarantee covers Hydra's
operations, not arbitrary operator-configured programs.

A private mode-0600 temporary configuration, removed when the command finishes,
forces BatchMode, StrictHostKeyChecking, UpdateHostKeys=no, no multiplexing, no
agent/X11 forwarding, no local commands or TTY, one connection attempt, and
ClearAllForwardings. CanonicalizeHostname is disabled to avoid discovery-triggered
DNS expansion; reported effective endpoints reflect this qualification policy.
Existing OpenSSH host keys remain the authority. Unknown/changed keys stop;
Hydra never invokes ssh-keyscan, accepts a key, or updates known_hosts.

The temporary file is passed with `-F`, which OpenSSH propagates to ProxyJump
children. Normal jump chains therefore receive the same strict policy. Custom
ProxyCommand implementations remain within the trusted configuration boundary.
IdentityFile paths are metadata only. ProxyCommand text and raw SSH failure
stderr are omitted because they can contain credentials; a boolean reports
whether a custom ProxyCommand was configured.

Each `ssh -G` and handshake process has a default five-second wall deadline,
configurable with `--timeout 1-30`. Fleet bounds each captured stream to 8 MiB;
configuration output is additionally limited to 64 KiB before parsing. H1 uses
one worker: at most 16 sequential resolutions and 16 sequential handshakes.
Cancellation terminates the active fleet transport process group and retains
unvisited candidates with typed cancellation results.

## Static inventory schema 1

```json
{
  "schema_version": 1,
  "observed_at": 1788960000,
  "hosts": [
    {"name": "build", "target": "build-alias", "labels": ["linux", "build"]},
    {"name": "mac", "target": "developer@mac-alias", "labels": ["macos"]}
  ]
}
```

All input objects have closed fields. Every record requires `name`, `target`,
and `labels`. Unknown fields, duplicate names, invalid types, control/NUL bytes
in text, and unsupported versions are rejected. Targets use fleet's existing
SSH target grammar with fewer than 256 bytes. Names are fewer than 128 bytes.
Each record permits up to 16 nonempty labels, each fewer than 128 bytes.
`observed_at` is a nonnegative Unix timestamp no later than import time,
reported by the inventory owner; it is not a verified observation. Credentials,
executable paths, environment assignments, and requested mutations are not input
fields. This does not interpret or execute Ansible/VPN/cloud/mDNS inventories.

## Candidate/result schema 1

The Hydra schema-1 envelope contains `data.candidate_schema_version: 1`,
`observed_at` (Unix seconds), `required_capability` (empty for discovery),
`partial_failure`, and `candidates`. Exit status is zero only if every candidate
resolves and, when requested, qualifies. Partial failure has `ok: false` and
**retains every candidate and its evidence**.

| Candidate field | Meaning |
| --- | --- |
| `candidate_id` | `cand_` plus lowercase hex of the exact target bytes. Stable across reorder/refresh/source addition; a local selection ID, never verified identity or an execution target. |
| `target` | Selected OpenSSH target, preserved exactly. |
| `deduplication` | Always `exact_target_only`. |
| `sources` | Entries with `kind` (`ssh-config` or `static`), `locator`, selected `name`, `observed_at`, `age_seconds`, `freshness`, and `labels`. |
| `verified_identity` | Null in H1: candidate identity remains separate from qualification `data.peer_fingerprint`, which is evidence from the current authenticated SSH session rather than a candidate identity. |
| `resolution` | Envelope with effective endpoint configuration or typed failure. |
| `qualification` | Qualify only: handshake evidence envelope or typed failure. Protocol/capability failures retain received handshake data. |
| `qualified_at` | Qualify only: check completion time, including failed/unvisited checks. |
| `status` | `discovered`, `resolution_failed`, `compatible`, or `qualification_failed`. |

Candidates sort by exact target; sources sort by kind, locator, then name.
Only byte-identical targets deduplicate, retaining all selected source records.
Two aliases resolving to the same hostname/IP remain distinct because User,
Port, keys, and jump policy may differ. SSH success never merges identities.
Source metadata is never copied into verified identity or handshake evidence.

Static freshness is `source_reported`: timestamp and elapsed age remain visible
even for old inventories. SSH freshness is `resolved_now`, meaning current
configuration selection, not live reachability; resolution failures stay explicit.
There is no arbitrary TTL, and SSH success never refreshes the inventory owner's
timestamp. Snapshot times/ages change; IDs, ordering, effective fields, and
fixture outcomes are deterministic for unchanged inputs.

Effective configuration exposes string arrays for hostname, user, port,
proxyjump, hostkeyalias, identityfile, identitiesonly, and known-host file
settings when OpenSSH emits them, and optionally `proxycommand_configured`.
These are coordinates/policy hints, not verified identity.

## Typed outcomes and acceptance

| Code | Meaning |
| --- | --- |
| `host_key_unknown` | OpenSSH explicitly reported no known host key under strict checking. |
| `host_key_changed` | OpenSSH explicitly reported changed host identification. |
| `host_key_failed` | Other/ambiguous key verification failure; diagnosis is not guessed. |
| `authentication_failed` | Batch authentication failed. |
| `offline` | SSH status 255 without a more specific transport diagnosis. |
| `timeout`, `output_limit`, `cancelled` | Fleet time/output boundary or interruption. |
| `version_mismatch` | Existing fleet/Hydra/schema compatibility gate failed. |
| `capability_unavailable` | Requested capability was not advertised. |
| `invalid_response` | Missing, malformed, or incorrect handshake response. |
| `ssh_config_failed`, `not_checked` | Configuration unavailable; qualification did not run. |
| `remote_failed`, `transport_failed` | Other remote exit or inability to start SSH. |

`make test-discovery`, also part of `make test-fleet`, exercises the public CLI
with real OpenSSH config expansion and controlled SSH: prepared alias/Include/jump
settings, ten mixed outcomes, static selection/freshness, conservative dedupe,
malformed input, limits, cancellation, and preservation of configuration/state.
This is **local fixture qualification**. It does not prove a live jump connection,
actual key negotiation/rotation, external installation behavior, or live provider
capability. Those remain explicitly operator-authorized live qualification
requirements. Tests contact no real host. Reviewed enrollment/bootstrap/trust
mutations use the separate [enrollment contract](HOST_ENROLLMENT.md). The H3
snapshot adapters above import supplied records; they do not discover live
provider membership or access provider credentials.

Local qualification on 2026-09-09, from integrated baseline
`0190bf938d82fa7d136369f96ed9061d6c0e2df6`:

- `make test-discovery`: all five acceptance tests passed, including all ten
  candidate rows and byte-for-byte preservation of a pre-existing alias/state.
- The same tests passed with the repository's macOS UBSan configuration
  (`-fsanitize=undefined`) and `UBSAN_OPTIONS=halt_on_error=1`.
- `build/test-fleet`, `sh tests/test_fleet.sh`, and the 179 assertions in
  `sh tests/test_completion.sh` passed; `make lint` passed.
- `make test-quality-c quality-c` passed with worktree-local clang-tidy 22.1.8:
  188 advisory functions, no complexity regressions, no ceiling changes.
- Full tmux-dependent fleet/remote-task integration was left to the coordinator's
  serialized acceptance slot. These checks do not claim live SSH qualification.

Reviewed enrollment is available through `fleet enroll review` and `fleet enroll
apply`; see [host enrollment](HOST_ENROLLMENT.md). These commands require an
actual peer fingerprint from qualification, bind the selected source/candidate,
principal, capability, project, and optional package digest/prefix into a
digest-confirmed intent, and preserve typed outcomes. They do not copy
credentials or trust repositories implicitly. Controlled transport and ephemeral loopback OpenSSH acceptance are described
in the enrollment guide; neither establishes live external host or provider
qualification.
