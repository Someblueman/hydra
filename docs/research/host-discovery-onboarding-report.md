# Seamless Hydra host discovery and onboarding

## Executive conclusion

Hydra should add a read-only discovery and staged-enrollment layer around its explicit SSH fleet, not turn discovery into an implicit trust or execution mechanism. The first useful path is an SSH-config/static-inventory adapter that normalizes candidates, verifies host keys through OpenSSH, negotiates Hydra compatibility, and produces an operator-reviewed onboarding plan. mDNS, VPN inventories, cloud tags, and configuration-management inventories should be additional source adapters with the same candidate contract; none should be a new authority for trust, credentials, project mapping, or execution.

The recommended state progression is:

~~~text
discovered -> normalized -> deduplicated -> transport-checked
  -> host-key verified -> authenticated -> Hydra-compatible
  -> bootstrapped (optional) -> project-mapped -> explicitly trusted
  -> ready for an explicitly authorized operation
~~~

Every arrow is observable and reversible. A discovered name, IP address, VPN node, cloud tag, or mDNS record is only a lead. A successful SSH login proves only that a particular principal authenticated to a host whose key was accepted; it does not approve repository configuration or grant permission to execute. Hydra's existing remote-task rule remains the final boundary: a lost response or owner is reconciled against the receiver and is never treated as permission to replay work.

The smallest coherent product sequence is:

1. **Static SSH inventory:** import SSH config Include files and explicit inventory files, preserving aliases, User, Port, IdentityFile, and ProxyJump semantics without copying private keys.
2. **Candidate and identity records:** add provenance, stable IDs, observed host-key fingerprints, freshness, deduplication, and typed failure states to the existing file-backed fleet area.
3. **Probe and preview:** run bounded, read-only SSH probes; negotiate Hydra protocol/capabilities; show platform, installation, project mappings, and prerequisites; generate a digest-bound onboarding plan.
4. **Batch onboarding:** apply only a reviewed plan, using existing pinned package bootstrap and explicit fleet init/trust decisions. Preserve per-host progress, partial failures, and resume semantics.
5. **Other sources and scale:** add opt-in mDNS, Tailscale/VPN, cloud-tag, and configuration-management adapters; shard 50–100 hosts without making a daemon, database, scheduler, or automatic failover part of discovery.

This design makes a one-host setup feel nearly automatic while remaining safe for a larger mixed macOS/Linux fleet over LAN, VPN, and SSH.

## Scope and evidence boundary

This report is grounded in the Hydra main checkout at commit 2c307c82e97b948ecd6965d8d33e7079a3701eca (version 2.3.0, observed 9 September 2026), the repository's fleet, remote-task, security, contract, roadmap, and acceptance documents, and the native fleet transport/bootstrap/receiver code and tests. The checkout was read-only for this research. No network scan, live host enrollment, credential distribution, install, provider campaign, implementation, commit, push, merge, or deployment was performed.

The report distinguishes:

- **Fact:** behavior or capability directly documented or present in the pinned checkout, or directly stated by a primary external source.
- **Inference:** a design implication drawn from those facts.
- **Recommendation:** proposed Hydra behavior, CLI, data contract, or acceptance target; it is not current functionality unless explicitly marked otherwise.

## 1. Hydra today: the boundary a discovery feature must preserve

### Existing fleet is explicit and SSH-centered

**Fact.** Hydra currently coordinates up to 16 trusted hosts through OpenSSH. A user registers a named alias with the command hydra remote add NAME [USER@]SSH_ALIAS; aliases are private JSON records below the HYDRA_HOME/fleet/remotes directory, and re-adding an alias replaces its record. The alias points to an ordinary SSH Host entry, so ports, identities, jump hosts, and connection settings remain OpenSSH concerns. Hydra forces batch authentication and strict host-key verification, and multiplexing is opt-in.[^1]

**Fact.** The native C helper stores only the alias name, target, Hydra executable, optional remote home, and multiplex flag in its current schema. It enumerates those local alias files; there is no source adapter, candidate store, mDNS browser, VPN API client, cloud inventory client, or automatic enrollment path. fleet list and fleet doctor operate on the aliases already present.[^2]

**Inference.** “Seamless” should mean fewer manual translations from a source inventory into an explicit alias and plan. It should not mean scanning arbitrary networks or silently converting a discovered endpoint into a trusted executor.

### Observation, compatibility, and bounded aggregation already exist

**Fact.** The receiver handshake exposes Hydra version, fleet/task/state/event/JSON protocols, native protocol availability, project mappings, capabilities, and the supported signal set. The coordinator requires Hydra 2.x and the compatible protocol versions before an operation; it checks the requested capability after the handshake. A native binary being present does not certify that its helper is compatible.[^1]

**Fact.** Fleet observation uses one handshake and one operation per host, with bounded SSH deadlines, 1–16 workers, a default of four workers, a default five-second observation timeout, and an 8 MiB per-stream output bound. Partial results are retained in data.hosts; errors distinguish host-key failure, authentication failure, offline, timeout, version mismatch, unavailable capability, malformed response, command failure, and unknown/interrupted outcomes. Host rows are sorted deterministically.[^1][^3]

**Inference.** These are good primitives for a discovery pipeline: source adapters can produce candidates, the existing bounded observer can qualify them, and the existing structured error envelope can report per-host progress. The current hard limit of 16 is a fleet-observation limit, not a reason to make a 100-host discovery attempt a single unbounded operation.

### Bootstrap is pinned, staged, and still explicit

**Fact.** fleet package creates a package containing the shell CLI/libraries, a target-platform hydra-fleet binary, and licenses, excluding configuration, credentials, and live state. fleet bootstrap requires existing Git and tmux, verifies the package digest and installer bytes, rejects paths outside an allowlist, qualifies the staged shell and fleet handshakes in isolated state, and publishes an immutable digest-named prefix before updating the local alias. It does not modify the host's default PATH or another installation.[^1][^4]

**Fact.** The bootstrap implementation accepts only fixed package paths, writes to a temporary stage, checks both Hydra and fleet version handshakes, removes the qualification state, and reuses an existing digest only when every packaged byte still matches. The remote transfer uses BatchMode=yes, StrictHostKeyChecking=yes, a bounded connection timeout, and an allowlisted fixed installer command.[^4][^5]

**Inference.** Onboarding should prepare a platform-specific package and show its digest in the preview. It must not make “discovered” or “SSH-authenticated” hosts implicitly run a bootstrap. Hosts without required prerequisites should be classified as blocked or probe-only; the separate T1/T2 work on tmux-optional headless execution is not yet delivered and should not be smuggled into discovery.

### Remote task recovery is a non-negotiable contract

**Fact.** A remote task binds an exact source commit, selected inputs, work, destination, completion policy, capabilities, and limits. Submission publishes a receiver-owned acceptance record and a pending launch intent. Submission keys deduplicate across all projects in one receiving host/state directory. A lost response is outcome_unknown; the operator repeats the same package and key to reconcile, never an automatic new mutation. A detached owner holds a durable launch claim; owner loss before a terminal state remains unknown, and restart never silently resumes or replays external work.[^6]

**Fact.** Result snapshots are sealed at the receiver, independently checked by the client, and collected into isolated refs without checking out code or granting integration approval. Remote success, artifact/result verification, local approval, and promotion remain separate facts.[^6][^7]

**Recommendation.** Onboarding operations need the same per-host operation identity and unknown-outcome discipline. A plan may be safely retried for read-only probes; any dispatched mutation must be reconciled through the existing authority before a new action is offered.

### Current roadmap boundaries are separate work

**Fact.** Roadmap V1–V4 plan versioned run/host observation freshness, attempt detail, resumable logs, visible controls, and recovery qualification. T1/T2 plan terminal-independent and remote execution without tmux; 9A plans satisfiable outcome obligations and compiler diagnostics. The roadmap explicitly calls these milestones planned work, not delivered runtime behavior.[^8]

**Recommendation.** Discovery/onboarding should feed those future observation surfaces, not depend on them or claim they already exist. It can ship useful read-only candidate and qualification records using today's fleet aliases and receiver handshake.

## 2. Design vocabulary and state machine

The operator and machine interfaces should use one vocabulary. These states are not merely UI colors: each is a distinct claim with a different permission level.

| State | What Hydra knows | What it may do | What it must not imply |
|---|---|---|---|
| discovered | A source reported a name/address/ID/record at a timestamp. | Normalize and show the candidate. | The endpoint exists now, is Hydra, or is safe. |
| normalized | Endpoint and source fields passed schema checks. | Deduplicate and filter. | Host-key identity or authentication. |
| deduplicated | Candidate was grouped with equivalent source records, or retained as a conflict. | Ask for an explicit merge/keep decision. | That a name or IP is a stable machine identity. |
| transport_checked | SSH/DNS/VPN transport returned a bounded result. | Retry read-only probe according to policy. | Login success or host trust. |
| host_key_verified | OpenSSH accepted an expected host key/fingerprint through the user's trusted process. | Proceed to batch authentication. | Authorization to install or execute. |
| authenticated | The selected Unix principal authenticated in batch mode. | Run a read-only Hydra handshake. | Repository trust, project mapping, or permission to run work. |
| hydra_compatible | Handshake matched required protocol versions and advertised capabilities. | Offer bootstrap/project mapping preview. | That a package is trusted or a project is trusted. |
| bootstrapped | A reviewed immutable package is installed and qualified. | Use the pinned executable in later operations. | Trust of repository configuration or task code. |
| project_mapped | A specific absolute remote project path and identity were observed/selected. | Offer fleet init or task preview for that mapping. | That the project configuration is trusted. |
| trusted | An operator explicitly accepted the exact relevant configuration/package/spec digest. | Allow the operation named by that decision. | Approval for unrelated projects, packages, or future changes. |
| ready | All required checks for one requested operation passed. | Execute only after the operation's explicit authorization. | Completion, health forever, or future retry permission. |
| needs_review | Key rotation, identity conflict, unsafe path, stale data, or policy conflict. | Show evidence and a safe next action. | Automatic merge or replacement. |
| unreachable | The last bounded observation could not connect. | Retain candidate and freshness/error evidence. | Failed, removed, or safe to replace. |
| revoked | An operator/source policy withdrew eligibility, or a key/credential was invalidated. | Block new operations and show tombstone. | That remote state was erased or cancelled. |

The state machine should be monotonic within one onboarding plan, while a fresh observation can move a host back to needs_review, unreachable, or version_mismatch. No reconnect should replay a mutation. A display of “last known running” must remain visibly stale when the host is disconnected, matching the roadmap's freshness boundary.[^8]

## 3. What comparable approaches actually provide

The following are source and inventory mechanisms, not interchangeable trust systems. Their useful contribution is the metadata and reachability path they can provide to Hydra's candidate adapter.

| Approach | Primary-source facts | Good fit for Hydra | Boundary or risk |
|---|---|---|---|
| **Explicit OpenSSH config and inventory** | ssh_config supports ordered Host/Match blocks, Hostname, User, Port, IdentityFile, and ProxyJump; BatchMode disables prompts. StrictHostKeyChecking=yes never auto-adds a new key and refuses a changed key. ssh-keyscan can gather keys in parallel, but cannot authenticate them and warns that output must be verified out of band.[^9][^10] | Best default for 1–50 hosts. It preserves the user's existing jump hosts, agent, certificates, ports, and per-host policy. Hydra can import aliases without touching private keys or known-hosts files. | Alias, hostname, IP, and inventory row are labels/coordinates, not stable identity. A key scan is candidate evidence only; automatic key acceptance would defeat Hydra's current trust boundary. |
| **mDNS/DNS-SD on the LAN** | RFC 6762 defines .local. names and multicast on the link-local mDNS addresses; names are meaningful only on the originating link. RFC 6763 enumerates service instances using PTR/SRV/TXT records and keeps TXT metadata intentionally small. The RFCs warn that mDNS name conflict handling assumes cooperating peers and that end-to-end cryptographic verification remains important.[^11][^12] | An opt-in “what is nearby?” view for laptops and small LAN labs. A future Hydra helper could advertise a non-authorizing _hydra-fleet._tcp service with version/protocol hints and an alias suggestion. | It does not cross normal VPN/router boundaries, is not an inventory of sleeping/offline machines, and is spoofable on an untrusted link. It must never create an SSH trust entry or run bootstrap by itself. |
| **Tailscale or another VPN inventory** | Tailscale exposes a device API; fine-grained devices:core:read credentials can list devices, while broader credentials can authorize/remove devices and change tags. CLI whois --json exposes machine name, node ID, addresses, tags, and capabilities. Tags are intended for non-human service devices and have owner/policy semantics; a device cannot simultaneously use a user identity and tag identity.[^13][^14][^15] | A strong source for 10–100 hosts: stable provider node IDs, tailnet addresses, tags, and reachability hints can prefilter candidates before SSH. Tags map naturally to Hydra labels such as linux, build, or region. | Requires an external account/API credential and an explicit source policy. VPN membership is not SSH authentication, host-key verification, Hydra installation, project mapping, or repository trust. Do not put a long-lived API token in Hydra's alias store. |
| **Cloud resource tags and managed-node inventory** | AWS Resource Groups can form groups whose membership is determined by matching resource tags. Systems Manager Inventory collects metadata such as applications, network configuration, instance details, services, tags, and custom inventory; it targets managed nodes and its shortest collection interval is 30 minutes.[^16][^17] | Useful for cloud fleets: query env, role, region, or hydra=true before attempting SSH. Provider resource IDs and tags are good source provenance and filtering fields. | A cloud resource can be stopped, replaced, missing an SSH path, or not managed by the inventory agent. Cloud metadata is not a verified host key or a Hydra capability. Provider API access and region/account scope need explicit operator configuration. |
| **Configuration-management inventory** | Ansible supports INI/YAML hosts and groups, aliases distinct from connection addresses, multiple inventory sources, host/group variables, and dynamic inventory plugins. Dynamic inventory is explicitly intended to combine cloud, LDAP, Cobbler, and CMDB sources; plugins are preferred over scripts, and inventory caches may be stale.[^18][^19] | A pragmatic bridge for existing operations: import groups and connection metadata, preserve source labels, and let the operator select a subset for Hydra qualification. It avoids inventing a second organization taxonomy. | Inventory variables may include sensitive connection data and can represent configuration-management authority beyond Hydra's scope. Import only an allowlist of non-secret fields; never execute playbooks or copy vault/private-key material as part of discovery. |

### Synthesis

**Inference.** The approaches divide cleanly into three layers:

1. **Coordinates:** SSH config, DNS-SD, VPN addresses, and cloud/private DNS tell Hydra where to try.
2. **Inventory metadata:** tags, groups, provider IDs, OS/region hints, and source timestamps help filter and batch.
3. **Trust and execution:** OpenSSH host-key verification, Unix authentication, Hydra handshake, pinned bootstrap, project trust, and task authorization remain Hydra/operator decisions.

Only the third layer can unlock an operation. The first two can make the third quicker and more legible, but no combination of discovery records is a substitute for it.

## 4. Recommended architecture

### 4.1 One candidate contract, many read-only adapters

Add a versioned candidate schema in the existing file-backed HYDRA_HOME/fleet area. Keep adapters narrow and replaceable:

- ssh-config: parse effective ssh -G results for selected aliases or explicit inventory rows; retain the source file/section and never read private key bytes.
- mdns: browse only a registered service type on selected interfaces; retain TTL, interface, service instance, SRV target/port, and TXT fields as untrusted hints.
- vpn: call a user-configured local CLI/API adapter, returning provider node ID, addresses, tags, and source freshness; do not store provider tokens in candidate records.
- cloud: call an explicitly configured provider inventory command/plugin, returning account/region/resource ID, tags, lifecycle state, and resolvable addresses.
- config-inventory: import an allowlisted JSON/YAML/INI inventory or execute an operator-selected dynamic inventory plugin in a bounded, isolated read-only process. Treat all output as data, not commands.

All adapters emit the same shape. A candidate record should be close to:

~~~json
{
  "schema_version": 1,
  "candidate_id": "cand_...",
  "source": {
    "kind": "ssh-config",
    "locator": "~/.ssh/config#Host build",
    "observed_at": "2026-09-09T08:00:00Z"
  },
  "labels": ["build", "linux"],
  "endpoints": [
    {"target": "build", "user": "ubuntu", "port": 22, "via": "jump"}
  ],
  "provider_identity": null,
  "transport": {
    "ssh_alias": "build",
    "host_key_fingerprints": [],
    "verification": "unverified"
  },
  "observations": {
    "reachability": "not_checked",
    "hydra_version": null,
    "platform": null,
    "capabilities": [],
    "projects": []
  },
  "status": "discovered",
  "last_error": null
}
~~~

The candidate schema must be closed on input and explicit about untrusted fields. Source output is not allowed to contain shell snippets, credentials, arbitrary environment assignments, or a requested mutation. A source adapter can suggest a label; it cannot grant a Hydra capability.

### 4.2 Stable identity and deduplication

Use a layered identity rather than guessing that a hostname is a machine:

1. **Local candidate identity:** a generated opaque ID for this candidate record; it is never used as an execution target.
2. **Source identity:** a stable tuple such as SSH alias plus inventory locator, VPN node ID, cloud account/region/resource ID, or mDNS instance/interface. Keep all source identities and aliases as provenance.
3. **Transport identity:** canonical SSH target (user, effective hostname/port, jump path) plus the verified host-key fingerprint(s). A changed key is a conflict, not an automatic merge.
4. **Hydra identity:** a persisted Hydra installation/host ID returned by a future handshake extension, bound to the installed state and not to a mutable hostname. Until that field exists, do not pretend that the current handshake's version and project list uniquely identify a host.

Deduplication rules should be conservative:

- Same verified SSH fingerprint and compatible endpoint aliases: one host with multiple labels, pending explicit alias choice.
- Same VPN/cloud source ID but different SSH fingerprints: one provider object with a transport conflict requiring review.
- Same hostname/IP but different fingerprint: two candidates or a key-rotation conflict; never silently merge.
- mDNS instance with a matching fingerprint: merge only after the SSH probe; before that, show a linked hint, not a merged identity.
- A source that disappears does not delete a candidate. Mark its source observation stale and retain a tombstone/freshness record.

**Recommendation.** Persist a dedupe decision and its evidence. A future source refresh must not silently rewrite a manually chosen alias, project path, key fingerprint, or revocation tombstone.

### 4.3 Extend handshake evidence, not authority

The existing handshake should remain the authority for what the remote Hydra installation can actually expose. Add fields only through a versioned protocol extension, for example:

- host_id — a receiver-generated opaque ID persisted in the remote Hydra fleet state or immutable installation, with an explicit reset/rotation event;
- platform — normalized OS/kernel/architecture/build target, with raw value bounded and retained for diagnosis;
- installation — selected Hydra path, version, package digest, and whether it is the pinned helper;
- prerequisites — Git, tmux, compiler/package support, disk floor, and headless/interactive distinctions;
- capability_evidence — capability name, protocol/version, observed-at, and confidence (advertised, qualified, live-checked);
- projects — canonical project ID/path and trust/config hash status, never trust records or credentials.

The coordinator must reject unknown required protocol versions rather than guessing, consistent with Hydra's public contract that machine interfaces do not silently downgrade or use a different format.[^20] “Executable exists” remains only an observation; provider authentication, prompt delivery, resume, and cancellation still require their specific checks.[^21]

### 4.4 Keep stores separate

Use file-backed JSON under the current fleet home, with mode-0700 directories and mode-0600 records, and atomic temporary-file/rename writes. Do not add a fleet database or daemon merely to hold candidates. Keep these records separate:

- **Candidates:** source observations and untrusted hints.
- **Aliases:** the existing explicit transport records, extended compatibly with selected candidate/source IDs and expected key fingerprints.
- **Onboarding plans:** immutable, digest-bound intended actions and per-host prerequisites.
- **Remote state:** receiver-owned Hydra/task state, never mirrored as a local cache authority.
- **Revocation/tombstones:** local policy decisions that block re-use until explicitly superseded.

The existing shell CLI remains the mutation authority; native C should validate, aggregate, and invoke public shell commands with explicit argv rather than writing live Hydra state.[^20][^22]

## 5. Operator journey at useful fleet sizes

### One host: near-automatic, with three visible decisions

A first-run path should make the common case short without hiding the trust boundaries:

1. hydra host discover --source ssh-config --alias build reads the effective OpenSSH configuration, records the alias and its provenance, and creates a candidate. It does not alter SSH configuration.
2. hydra host probe build performs a bounded BatchMode connection using the existing strict host-key policy. If the key is unknown or changed, the command stops with the fingerprint and the exact operator action required; it does not call ssh-keyscan and add the result.
3. The read-only probe runs the Hydra handshake and reports version/protocol compatibility, platform, installation path, prerequisites, capabilities, project mappings, and any stale evidence.
4. hydra host plan build --package hydra-package-DIGEST.tar writes an immutable plan. The plan names the exact candidate, verified key fingerprint, Unix principal, package digest, remote prefix, project mapping, and requested actions. It also records actions that are intentionally absent, such as PATH changes, credential copying, and task submission.
5. hydra host plan show is the review point. hydra host onboard apply PLAN requires the plan digest and an explicit confirmation. It may perform only the actions named in that plan.
6. The resulting alias and candidate records point to the same source and verified transport identity. A later task still needs its own project and operation authorization.

For a ready SSH alias and a package already built, the experience can be close to “select, inspect, confirm.” The number of decisions should be small, but each decision should be durable and inspectable: host key, principal/target, package digest, project path, and operation scope.

### Ten hosts: groups and per-host evidence

At ten hosts, an operator normally wants to work with a label or inventory group, but each host still needs its own row. The pipeline should:

- import the group as provenance and as a filter, not as a blanket trust grant;
- resolve all effective SSH options before probing;
- observe at most the configured worker bound, default four and never above the current 16-host observer limit in one batch;
- render a table with stable host order, state, last observation time, fingerprint, version, platform, project count, and typed error;
- let the operator exclude a host or acknowledge a key-rotation conflict without discarding the other nine;
- produce one plan containing per-host subplans and a plan digest over the ordered candidate identities and all action parameters.

A failed host remains in the plan as skipped/blocked evidence unless the operator explicitly regenerates the plan. A resumed apply must not silently widen the group because the source inventory changed.

### Fifty hosts: deterministic batches, not an unbounded fan-out

Fifty hosts should be handled as a collection of bounded batches. The discovery source may return all 50 candidates, but the transport and handshake stages should partition them deterministically, for example by sorted candidate ID, into batches no larger than 16. A batch can run in parallel with other batches only if the local resource budget and operator-selected concurrency allow it; the aggregate per-process worker limit remains enforced.

The user sees:

- a source snapshot ID and timestamp;
- 50 candidate rows, including duplicates and conflicts rather than silently collapsing them;
- batch IDs, each with an independent progress record;
- a summary such as 41 compatible, 4 unreachable, 2 host-key conflicts, 1 protocol mismatch, and 2 excluded by policy;
- an estimated next action based on observed evidence, not an availability promise.

Applying onboarding to 41 compatible hosts should not require regenerating the source or rerunning successful probes. Each subplan has its own idempotency key and records not_started, in_progress, succeeded, blocked, failed, or outcome_unknown. If a process exits, resume loads the exact plan and continues only where the receiver/local alias state makes that safe.

### One hundred hosts: inventory first, qualification second

At 100 hosts, a single interactive discovery command becomes a poor control surface. Use a two-stage workflow:

1. Import and normalize all source records locally, with source freshness and provider identifiers. This stage is cheap and does not contact hosts.
2. Select a filter, produce a snapshot, and qualify in deterministic batches. The user can run batches sequentially or choose a bounded number of concurrent batches; Hydra must surface the local file-descriptor, CPU, and SSH connection budget.

A 100-host report should be resumable and stream progress to a file-backed event log. It should support --since SNAPSHOT to qualify only candidates whose source identity, endpoint, key expectation, or relevant freshness changed. A no-change refresh should produce the same candidate and plan digests. A changed source should produce a new plan digest and an explicit diff.

Hydra should not promise all hosts are online, all credentials work, or all projects map uniformly. The useful large-fleet outcome is a trustworthy partition into ready, needs-review, blocked, unreachable, and excluded, with enough evidence to act on one partition at a time.

## 6. Proposed CLI and TUI

The command names below are recommendations, not current commands. They preserve the existing remote, fleet, and public shell mutation style while making discovery a separate namespace.

### CLI surface

~~~text
hydra host source list
hydra host source add ssh-config --path ~/.ssh/config
hydra host source add file --path inventory.yaml --format ansible
hydra host discover [--source NAME] [--group LABEL] [--json]
hydra host list [--state STATE] [--source NAME] [--stale-after DURATION]
hydra host inspect CANDIDATE...
hydra host verify-key CANDIDATE --fingerprint SHA256:...
hydra host probe CANDIDATE... [--workers 4] [--timeout 5s]
hydra host plan create CANDIDATE... [--package PATH] [--bootstrap]
hydra host plan show PLAN [--json]
hydra host plan diff PLAN OTHER-PLAN
hydra host onboard apply PLAN [--only STATE] [--resume]
hydra host operation status PLAN
hydra host operation reconcile PLAN CANDIDATE
hydra host revoke CANDIDATE [--reason TEXT]
hydra host restore CANDIDATE --tombstone ID
hydra host forget-source CANDIDATE --source NAME
~~~

The commands that read or probe may be run without a mutation confirmation. verify-key records the operator's explicit key decision but does not change known_hosts unless the user separately performs the established OpenSSH trust step. plan create is pure and should be safe to use in CI or review. onboard apply is the only command in this surface that can invoke bootstrap or mutate fleet aliases, and it should require a plan digest plus an explicit confirmation when any mutation exists.

Machine output should be a versioned JSON envelope with:

- schema_version;
- operation_id and, for a plan, plan_digest;
- source_snapshot_id;
- hosts, each with candidate ID, source IDs, state, freshness, evidence, error type, and next safe action;
- summary counts;
- warnings that are not hidden in human-only output.

Human output can be a table, but every row should have a stable candidate ID that can be copied into a follow-up command. Unknown fields from source adapters should be preserved under a bounded source_metadata object or dropped with a warning; they must never become command-line fragments.

### TUI

A fleet screen should have a filterable host table and a detail pane. Recommended columns are candidate ID, display name, source, state, key fingerprint suffix, target, Hydra version, platform, projects, age, and error. The detail pane should separate:

- source evidence;
- transport/effective SSH evidence;
- host-key decision;
- Hydra handshake evidence;
- project/path evidence;
- package/prerequisite evidence;
- selected plan actions;
- revocation or conflict history.

Actions should be state-gated. For example, probe is available for normalized candidates; accept key is available only when a fingerprint is displayed; create plan requires a compatible handshake; apply requires a reviewed plan; and reconcile is available for unknown outcomes. A keyboard shortcut must not accidentally turn a discovered row into a trusted alias.

Progress should show completed/total, current batch, last event timestamp, and a pause/cancel control. Cancel stops scheduling new read-only probes and lets already-started processes reach bounded termination. Cancel during a mutation records the local process interruption; it does not claim remote cancellation or success.

## 7. Pipeline and durable records

### Pipeline stages

A discovery run should be an explicit pipeline whose output at each stage is inspectable:

~~~text
source snapshot
  -> parse and schema validation
  -> endpoint normalization
  -> conservative deduplication
  -> policy filter
  -> bounded transport/key observation
  -> Hydra handshake
  -> prerequisite/project inspection
  -> immutable plan
  -> explicit apply
  -> local alias and receiver reconciliation
~~~

Each stage consumes a versioned record and emits a new record or an error event. It should not mutate the next stage's authority implicitly. For example, a source refresh can update candidate metadata but cannot update a verified key or alias target; a successful handshake can update compatibility evidence but cannot mark a project trusted.

### Plan contents

A plan should include:

- plan schema version and digest;
- source snapshot IDs and source freshness;
- ordered candidate IDs and deduplication decisions;
- effective SSH target references, without copying private key material;
- expected host-key fingerprints and the evidence/decision that accepted them;
- Hydra version/protocol requirements;
- package path and SHA-256 digest, or an explicit no_bootstrap action;
- intended remote prefix and installer arguments;
- project IDs/paths and any explicit trust/config hashes;
- per-host prerequisites and action list;
- idempotency key derivation and operation scope;
- policy version and user/account that created the plan.

The plan must be self-contained enough to explain what was approved, but it must not duplicate receiver-owned task state or contain secrets. Paths should be canonicalized before hashing. Array ordering must be deterministic. A plan is invalid if the package bytes, expected key, selected source identity, or relevant policy changes; the user must create a new plan rather than silently applying the old intent.

### Event and state files

Use a per-plan directory under the fleet state area:

~~~text
plan.json                 immutable intent and digest
hosts/CANDIDATE.json      per-host state and evidence
events.jsonl              append-only local progress events
summary.json              derived counts and last update
tombstones/               revocation and conflict markers
~~~

Write records with restrictive permissions, temporary files, fsync/rename where the existing state layer requires it, and bounded event fields. Never log private keys, passwords, API bearer tokens, full command lines containing secrets, or unbounded remote output. The receiver remains the authority for remote task state; local files say what Hydra observed and what it intends to reconcile.

## 8. Partial failures, retry, and resume

### Failure taxonomy

Every host result should use a stable type and retain a bounded diagnostic:

- source_invalid — adapter output did not satisfy the candidate schema;
- duplicate_conflict — source identities or transport identities disagree;
- dns_or_route — endpoint could not be resolved or routed;
- host_key_unknown — strict verification has no accepted key;
- host_key_changed — the observed key conflicts with the accepted fingerprint;
- auth_failed — the selected principal could not authenticate in BatchMode;
- timeout or offline — bounded observation expired or the host was unreachable;
- protocol_mismatch — required Hydra/fleet protocol is absent or incompatible;
- capability_missing — a requested capability was not advertised or qualified;
- prerequisite_missing — Git, tmux, platform, disk, or package requirement failed;
- project_conflict — no unique or trusted project mapping was found;
- bootstrap_failed — the receiver rejected or could not complete the pinned package install;
- interrupted — the local process ended before recording a result;
- outcome_unknown — a mutation may have reached the receiver but the client lacks a terminal result;
- policy_blocked — local source, trust, or revocation policy excludes the host.

Error type is machine-stable; human diagnostics can include safe context and a suggested next action. outcome_unknown is deliberately distinct from failed.

### Retry policy

Safe read-only stages may retry according to a bounded exponential schedule, with a maximum attempt count and an overall operation deadline. A retry should carry the same candidate ID and observation key, and it should record whether the source or endpoint changed between attempts.

For mutations:

- if no remote launch was accepted, the plan can be retried after inspecting the local evidence;
- if the transport died after a request may have been received, mark outcome_unknown;
- reconcile using the same receiver-owned idempotency key and exact plan/package digest;
- only after reconciliation proves no acceptance and the operator explicitly chooses to retry may Hydra create a new attempt;
- never use a fresh random key merely because the client lost a response;
- never infer success from a zero SSH exit status if the receiver's acceptance/result record is unavailable.

This is the onboarding equivalent of Hydra's remote-task rule: recovery is a read/compare/reconcile operation, not a blind replay.[^6]

### Resume and changed inputs

hydra host operation status should load the plan and report the last durable event for each host. --resume may continue a plan only if its digest, package digest, expected key, source identity, policy version, and relevant local alias record still match. If a source refresh finds a different endpoint or key, the host is held for review and the old plan remains immutable.

Successful read-only probes may be reused within their freshness window. A successful bootstrap may be reused only when the receiver reports the exact package digest and immutable prefix. A local alias update may be considered complete only after the resulting file is present and validates against the plan. A receiver-owned mutation is complete only after reconciliation.

## 9. Revocation, key rotation, and multiple users

### Revocation semantics

Revocation should be explicit and scoped:

- candidate revoke: block a candidate ID and its linked source identities from new plans;
- transport revoke: reject a specific fingerprint/endpoint combination while retaining other source evidence;
- package revoke: stop offering a package digest for new bootstrap plans; this does not erase an installed prefix;
- project revoke: remove one project mapping or trust decision without revoking the host;
- source revoke: disable a provider/inventory adapter or credential reference;
- operator revoke: record who made the decision, when, why, and which plan/policy it supersedes.

A revoke operation writes a tombstone and causes future discovery to show revoked or needs_review. It does not pretend to uninstall software, cancel a remote task, delete receiver state, or remove a machine from a cloud/VPN provider. Those actions require their own explicit product workflows.

### Host-key rotation

A changed key must stop onboarding and show old fingerprint, new fingerprint, observation source, and timestamp. The operator can:

1. confirm an approved rotation through the established OpenSSH/organizational process;
2. record the new fingerprint and rotation evidence;
3. create a new plan.

An mDNS, VPN, cloud, or Ansible match cannot override a changed SSH key. A key accepted for one alias cannot silently transfer to another endpoint merely because a name is reused.

### Multiple local users

The default model is per-user fleet state and per-user OpenSSH configuration. Hydra should not copy credentials into a shared store or pretend that one user's host-key acceptance authorizes another user's account.

If a team later needs shared fleet records, make it an explicit backend with:

- per-user read/write policy;
- separate source credentials and audit identity;
- conflict handling when two users accept different keys or aliases;
- an immutable plan owner and approver;
- no sharing of private keys or API bearer tokens;
- a clear distinction between observed by Alice and approved for Bob's operation.

A local 0700/0600 layout is the safe baseline. A machine-wide daemon or central database is not required for the discovery feature and should be justified by a separate requirement.

## 10. Measurable acceptance targets

These are proposed product targets, not measurements of the current checkout.

### Correctness and safety

- No source adapter can cause an SSH key acceptance, credential copy, bootstrap, alias mutation, project-trust mutation, or remote task submission without an explicit plan/apply action.
- Every discovered row has source provenance, observation time, stable candidate ID, and a typed state; every accepted transport has a recorded fingerprint.
- A changed host key is never auto-merged or auto-replaced.
- A plan digest changes whenever any candidate identity, expected key, package digest, project mapping, action, or policy input changes.
- A lost response to a mutation yields outcome_unknown and a reconcile action; the test suite demonstrates zero duplicate receiver acceptance records after client retries.
- No diagnostic or event artifact contains private keys, passwords, API bearer tokens, or unbounded remote output.

### User experience

- With a prepared SSH alias, accepted host key, working credentials, and an already-built package, one host reaches a reviewable plan in at most three explicit commands and one confirmation.
- A one-host probe reports a useful result or a typed failure within the configured timeout plus bounded process cleanup; it never hangs waiting for a password or host-key prompt.
- A ten-host run preserves all ten rows and their evidence even when some hosts fail.
- A 50-host run is partitioned into deterministic batches no larger than 16 for the current observer, and a restarted process resumes without rerunning successful mutations.
- A 100-host source import is local and bounded by input size; qualification can be resumed from a source snapshot without losing source provenance or silently widening the selection.
- Human output and JSON output identify the same candidate, state, error, and plan digest.

### Operational and regression checks

- Contract tests cover schema version rejection, deterministic ordering, dedupe conflicts, stale snapshots, and plan-digest invalidation.
- Integration tests use a local SSH fixture with known/changed keys, BatchMode auth failure, jump host, missing Git/tmux, protocol mismatch, interrupted client, and receiver reconciliation.
- Adapter tests cover valid and malformed SSH, mDNS, VPN, cloud, and Ansible records without contacting those providers.
- Security checks verify file modes, no secret leakage, argument allowlists, bounded output, and source fields treated as data.
- Acceptance records separate checked, skipped, blocked, unknown, and not-applicable hosts; a green summary cannot hide a blocked host.

The quality bar is an evidence-preserving operator workflow, not a claim that every network endpoint is healthy.

## 11. Staged implementation roadmap

### Stage 0 — contract and fixtures

Define the candidate, source snapshot, plan, event, and tombstone schemas; document state transitions and error codes; add fixture SSH configs and local receiver cases. Add no new discovery source yet. The exit criterion is deterministic parse/normalize/dedupe/plan output with schema rejection and no mutation.

### Stage 1 — SSH-config/static inventory

Implement the SSH-config adapter using effective OpenSSH options and explicit inventory files. Add host discover, host list, host inspect, and read-only probe/JSON output. Reuse the current bounded transport and handshake. Require the operator's existing strict host-key process; do not automate key acceptance.

Exit evidence:

- one host through a jump host;
- unknown/changed key stops safely;
- 10-host mixed success retains all rows;
- no alias or remote installation is modified by discovery/probe.

### Stage 2 — plan and explicit onboarding

Add immutable plan creation/show/diff and an apply engine that calls the existing pinned package/bootstrap and fleet-init/trust operations only when named in the plan. Record per-host progress, idempotency keys, unknown outcomes, and reconciliation. Add local plan/event/tombstone persistence and resume.

Exit evidence:

- package digest and installer bytes are bound to the plan;
- bootstrap reuse is proven by the receiver's immutable prefix/digest;
- interruption and lost response tests show no blind replay;
- partial failures can be retried without a new source snapshot.

### Stage 3 — source adapters

Add opt-in mDNS, VPN/Tailscale, cloud-tag, and configuration-management inventory adapters one at a time. Keep credentials outside candidate records and use the same schema, provenance, freshness, and policy filters. Each adapter must have a fixture or mocked provider response and an explicit stale/cache behavior.

The existing Hydra roadmap's T1/T2 headless/tmux work and V1–V4 observation/recovery work remain separate dependencies or follow-on integration points; discovery must not silently claim those milestones are complete.[^8]

### Stage 4 — scale and operator polish

Add deterministic 50/100-host batching, snapshot diffs, TUI filters/detail panes, bounded event streaming, and resource-budget controls. Measure throughput and failure classification on local fixtures before any real-fleet qualification. Keep the current 16-host observer limit per bounded aggregation and document how batches compose.

A release should require the repository's fleet and remote-task acceptance suites plus the new discovery/onboarding acceptance matrix, with main-branch commit and artifact provenance recorded separately from tested locally.[^23][^24]

## 12. Risks and design trade-offs

- **Convenience versus host trust.** Automatic discovery is valuable as a candidate feed; automatic key acceptance would be a security regression. Preserve the extra key decision.
- **Freshness versus reproducibility.** Live inventories are useful but change during a run. Freeze source snapshots and plan digests, then show diffs rather than mutating intent.
- **Provider richness versus dependency scope.** Cloud/VPN APIs can supply excellent IDs and labels, but each adds credentials, rate limits, and account scope. Ship the SSH path first and make providers opt-in.
- **Heterogeneous prerequisites.** macOS and Linux, interactive and headless hosts, and differing Git/tmux availability will produce a mixed fleet. Report prerequisites per host; do not broaden bootstrap in the discovery feature.
- **Name and identity ambiguity.** Hostnames, IPs, aliases, mDNS names, provider IDs, and fingerprints can disagree. Retain all evidence and force review on conflict.
- **Partial failure.** A batch summary that says some failed is insufficient when a mutation may have happened. Use typed states and receiver reconciliation.
- **Shared-user pressure.** Teams may ask for a central inventory before the trust and audit model exists. Keep the baseline per-user; make shared state an explicitly designed backend.
- **Overbuilding.** A candidate file store, bounded adapter pipeline, plan, and TUI are enough for the first release. A daemon, scheduler, automatic failover, or provider-wide controller would add authority without solving the onboarding boundary.
- **Privacy and leakage.** Inventory metadata may reveal hostnames, regions, projects, and tags. Restrict permissions, bound logs, and allow source-specific redaction before exporting reports.
- **False readiness.** A compatible handshake does not prove a project is trusted, a provider credential works, or a task can be resumed. Keep ready scoped to the exact requested operation.

## 13. Final recommendation

Build the feature as a candidate-to-plan pipeline around the existing explicit SSH fleet:

1. ship SSH-config/static discovery and deterministic candidate records;
2. reuse strict OpenSSH host-key verification and the existing bounded Hydra handshake;
3. show a reviewable, digest-bound plan before any alias, bootstrap, project trust, or other mutation;
4. apply in bounded batches with receiver-owned idempotency, per-host progress, typed partial failures, and reconcile-before-retry;
5. add mDNS, VPN, cloud, and configuration-management sources only as opt-in metadata adapters;
6. retain per-user file-backed state and explicit revocation, and measure the 1/10/50/100-host workflow with failure injection.

That yields the requested seamlessness at the operator boundary—discover, inspect, plan, confirm—without making a network sighting, provider membership, or SSH login into an unreviewed grant of execution authority.

## Sources

Hydra sources are pinned to commit 2c307c82e97b948ecd6965d8d33e7079a3701eca unless a path says otherwise.

[^1]: [Hydra fleet contract](https://github.com/Someblueman/hydra/blob/2c307c82e97b948ecd6965d8d33e7079a3701eca/docs/FLEET.md) — explicit aliases, strict SSH behavior, handshake, observation limits, fleet operations, and bootstrap boundary.
[^2]: [Hydra fleet CLI and schema](https://github.com/Someblueman/hydra/blob/2c307c82e97b948ecd6965d8d33e7079a3701eca/src/fleet/cli.c) and [fleet header](https://github.com/Someblueman/hydra/blob/2c307c82e97b948ecd6965d8d33e7079a3701eca/src/fleet/fleet.h) — current alias records and enumeration.
[^3]: [Hydra bounded aggregate transport](https://github.com/Someblueman/hydra/blob/2c307c82e97b948ecd6965d8d33e7079a3701eca/src/fleet/transport/aggregate.c) and [receiver server](https://github.com/Someblueman/hydra/blob/2c307c82e97b948ecd6965d8d33e7079a3701eca/src/fleet/transport/server.c) — worker bounds, timeouts, output limits, handshake and typed results.
[^4]: [Hydra bootstrap transport](https://github.com/Someblueman/hydra/blob/2c307c82e97b948ecd6965d8d33e7079a3701eca/src/fleet/transport/bootstrap.c) — package digest verification, staged install, allowlisted paths, immutable prefix, and strict SSH command.
[^5]: [Hydra onboarding acceptance tests](https://github.com/Someblueman/hydra/blob/2c307c82e97b948ecd6965d8d33e7079a3701eca/tests/test_onboarding.sh) — qualification, bootstrap, reuse, failure, and host-key behavior.
[^6]: [Hydra remote-task contract](https://github.com/Someblueman/hydra/blob/2c307c82e97b948ecd6965d8d33e7079a3701eca/docs/REMOTE_TASKS.md) — receiver-owned acceptance, idempotency, unknown outcome, durable owner, and no replay.
[^7]: [Hydra remote-task acceptance](https://github.com/Someblueman/hydra/blob/2c307c82e97b948ecd6965d8d33e7079a3701eca/docs/REMOTE_TASK_ACCEPTANCE.md) — result sealing, verification, collection, and separate promotion.
[^8]: [Hydra roadmap](https://github.com/Someblueman/hydra/blob/2c307c82e97b948ecd6965d8d33e7079a3701eca/docs/ROADMAP.md) — planned T1/T2, 9A, and V1–V4 work; these are not delivered by discovery.
[^9]: [OpenBSD ssh_config(5)](https://man.openbsd.org/ssh_config) — Host/Match, effective connection options, BatchMode, StrictHostKeyChecking, identity, and jump-host behavior.
[^10]: [OpenBSD ssh-keyscan(1)](https://man.openbsd.org/ssh-keyscan) — parallel key collection and the requirement for out-of-band verification.
[^11]: [RFC 6762: Multicast DNS](https://www.rfc-editor.org/rfc/rfc6762) — link-local .local. scope, multicast transport, and conflict/security considerations.
[^12]: [RFC 6763: DNS-Based Service Discovery](https://www.rfc-editor.org/rfc/rfc6763) — PTR/SRV/TXT service enumeration and metadata limits.
[^13]: [Tailscale API reference](https://tailscale.com/docs/reference/tailscale-api) — device inventory and scoped API access.
[^14]: [Tailscale trust credentials](https://tailscale.com/docs/reference/trust-credentials) — credential scopes and management authority.
[^15]: [Tailscale tags](https://tailscale.com/docs/features/tags) and [Tailscale CLI](https://tailscale.com/docs/reference/tailscale-cli) — service tags, owner semantics, and machine identity/address output.
[^16]: [AWS Resource Groups supported resources](https://docs.aws.amazon.com/ARG/latest/userguide/supported-resources.html) — tag-based resource groups and membership.
[^17]: [AWS Systems Manager Inventory](https://docs.aws.amazon.com/systems-manager/latest/userguide/systems-manager-inventory.html) — managed-node metadata, custom inventory, and collection cadence.
[^18]: [Ansible inventory introduction](https://docs.ansible.com/projects/ansible/latest/inventory_guide/intro_inventory.html) — static sources, aliases, groups, variables, and multiple inventories.
[^19]: [Ansible dynamic inventory](https://docs.ansible.com/projects/ansible/latest/inventory_guide/intro_dynamic_inventory.html) — plugins, external sources, caching, and dynamic inventory boundaries.
[^20]: [Hydra public contracts](https://github.com/Someblueman/hydra/blob/2c307c82e97b948ecd6965d8d33e7079a3701eca/docs/CONTRACTS.md) — versioned machine interfaces, strict validation, and no silent format downgrade.
[^21]: [Hydra security model](https://github.com/Someblueman/hydra/blob/2c307c82e97b948ecd6965d8d33e7079a3701eca/docs/SECURITY.md) — authentication, authorization, path/credential boundaries, and operational trust.
[^22]: [Hydra remote transport implementation](https://github.com/Someblueman/hydra/blob/2c307c82e97b948ecd6965d8d33e7079a3701eca/src/fleet/transport/remote.c) — strict remote invocation, bounded arguments/output, and public command boundary.
[^23]: [Hydra fleet acceptance](https://github.com/Someblueman/hydra/blob/2c307c82e97b948ecd6965d8d33e7079a3701eca/docs/FLEET_ACCEPTANCE.md) — required fleet qualification and acceptance evidence.
[^24]: [Hydra fleet tests](https://github.com/Someblueman/hydra/blob/2c307c82e97b948ecd6965d8d33e7079a3701eca/tests/test_fleet.sh) — current native/shell fleet behavior and regression coverage.
