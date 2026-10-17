# Trusted SSH fleets

Fleet coordinates up to 16 trusted Hydra hosts through OpenSSH. It provides pinned
bootstrap, observation, remote head and workflow operations, interactive attach,
explicit configuration/history transport, and a native fleet view.

`fleet discover` and `fleet qualify` inspect explicitly
selected OpenSSH aliases/static records and qualify them with a strict read-only
handshake. Candidate evidence stays separate from registered aliases and trust.
Opt-in [live sources](#discover-qualify-and-enroll-hosts) read the operator's own
SSH configuration, an Ansible hosts file or an mDNS client and diff each
acquisition against the previous private snapshot; [revocation](#revoke-decisions)
blocks a candidate, source, alias, key, package or project decision for good.

The `remote task interface` prepares exact source/input packages,
persists receiver-owned acceptance and execution, and supports disconnected
command/workflow runs, bounded logs, cancellation, and verified result collection.
Successful committed results enter the existing local integration and approval
flow. Existing direct remote workflow calls remain synchronous.

Fleet and remote tasks shipped in v2.1.0. Headless provider execution and durable
approval suspension are available in v2.2.0; see the [changelog](../CHANGELOG.md).
A remote workflow runs on one selected host. New remote tasks can be placed
automatically across eligible hosts (see [Automatic placement](#automatic-placement));
cross-host DAG coordination remains on the [roadmap](ROADMAP.md).

## Architecture and installation

The optional `hydra-fleet` executable is C. It owns JSON validation, SSH transport,
bounded child processes, aggregation, and fleet configuration/bundles. OpenSSH owns
host resolution, SSH credentials, host keys, and connection sharing. Head and workflow
mutations invoke the existing shell CLI with argv; they do not write live Hydra
state from C. There is no fleet daemon, database, scheduler, or mutation replay loop.

Build from source with a C99 compiler, `pkg-config`, and JSON-C development files:

```sh
# macOS: brew install json-c pkg-config
# Ubuntu: apt install libjson-c-dev pkg-config
make build-fleet
make test-fleet
```

JSON-C is linked statically into the fleet executable. Deployed hosts do not need
`jq`, Python, Go, or a JSON-C shared library. The normal shell-only CLI remains
available without the fleet executable or its build dependencies. `install.sh`
copies a prebuilt fleet executable when present; `HYDRA_INSTALL_FLEET=never` skips
it and `required` refuses an absent executable. Uninstall removes it with the
other native helpers. JSON-C's [license](licenses/json-c.txt) travels with packages.

## Guided remote setup

`hydra remote setup` takes a fresh SSH account to a verified alias one step at a
time and can be rerun at any point: it resumes from private state in
`$HYDRA_HOME/fleet/setup/NAME.json` (0600 in a 0700 directory, locked while a
command runs).

```sh
hydra remote setup ovh ubuntu@build-host      # or an SSH Host alias
hydra remote setup ovh --ssh-config /abs/config
hydra remote setup status ovh
hydra remote setup list                       # every setup: destination, status, next step
hydra remote setup remove ovh                 # forget an unfinished setup (asks first)
```

Steps run in order: host key, preflight, provision, agents, then install and
sign-in for each selected agent, verify, and alias. Each step also has its own
command that uses the same state: `hydra remote trust-key|preflight|provision|agents
NAME` and `hydra remote install-agent|sign-in NAME --agent A`. The alias record is
published last and never overwrites a different alias, so a half-configured host
never appears in `hydra fleet list`. Rerunning setup with a different destination
or SSH config for an existing NAME fails with `setup_binding_changed`; `hydra remote
setup remove NAME [--approve PLAN_SHA256] [--json]` (`remote-setup-remove`) deletes
the local record and its lock so the setup can start again. It never connects to the
remote: its plan lists what earlier steps left there (a Hydra install at its prefix,
agents, provider sign-ins) and in `known_hosts`, none of which is undone. Like other
plans it asks `y` on a terminal and otherwise returns `approval_required` (exit 3)
with the approving command. It fails with `setup_busy` while a step runs and with
`setup_complete` for a finished setup whose alias is published (remove the alias
with `hydra remote remove NAME`). Unknown options are usage errors. `status`,
`list` and `remove` are reserved and cannot be setup names. When SSH fails, the
error message ends with OpenSSH's own reason (also `data.ssh_error`), for example
`ssh: Could not resolve hostname ...`, and the recovery names the likely fix. `hydra remote setup list [--json]` (`remote-setup-list`) reads every record
read-only, like `setup status`: `data.setups[]` holds `name`, `destination`,
`ssh_config`, `status` (the first unfinished step's status, `done` when complete, or
`unreadable` with `error{code,message}` for a record it refuses), `remote_changed`
(a provision, agent installer or sign-in may have changed the remote), `complete`
and `next`. When an agent installer or sign-in that ran on your terminal fails, the
step keeps the error code and message, and `setup status NAME --json` reports it
as `steps[].error{code,message}` while the step is `failed` or
`outcome_unknown`. Setup does not prepare a remote project: once the alias
exists, clone or copy the project on the host yourself and initialise it with
`hydra fleet init NAME --project /abs/project -- --no-agent`, review its `.hydra`
configuration, then trust it explicitly (see [Operate explicitly](#operate-explicitly)).

Every step that changes something first shows a plan and asks for approval.
Interactive terminals answer `y`; trusting a host key requires typing `yes`. With
`--json`, without a terminal, or when `CI` or `HYDRA_NONINTERACTIVE` is set, Hydra
never prompts: it stops with `approval_required` (exit 3) and returns the plan,
`data.plan_sha256`, and `data.next.argv`, the exact command that approves that plan
(`--approve PLAN_SHA256`, or `--fingerprint SHA256:...` for a host key). A changed
plan no longer matches and fails with `approval_mismatch`. There is no blanket
yes flag.

**Host key.** Hydra reads the effective OpenSSH configuration (`ssh -G`, including
`HostKeyAlias`, port, `UserKnownHostsFile` and `HashKnownHosts`) and probes with
strict host key checking. It never accepts a changed key: it reports
`host_key_changed` with the new fingerprint and the `ssh-keygen -R` command to run
yourself once you have verified the change. Any other entry for the host, including
a key of a different type, is `host_key_ambiguous`. An unknown key is captured
without authenticating through your own SSH path (jump hosts included) and shown
as its SHA256 fingerprint; compare it with one obtained out of band. After
approval Hydra appends exactly that line to the first `UserKnownHostsFile` (created
0600 in a 0700 directory if absent; refused if it is a symlink, not yours, or group
or world writable) and proves it with a second strict probe. It never edits or
removes existing lines, and every refusal leaves known_hosts byte-identical.

**Preflight** is read-only and needs no Hydra on the host. A fixed POSIX script
reports the platform, HOME, PATH, umask, tools, existing Hydra installs and pins,
and agent executables in common install directories (`remote-preflight` schema 1).
Blocking requirements are a supported platform, git, sha256sum or shasum, mktemp,
head and tail, a writable HOME and 50 MB free; missing ones fail with
`prerequisite_missing` and `data.missing[]`. Linux x86_64 and aarch64 have pinned
release helpers. macOS (x86_64 or aarch64) has none, so it is a warning when an
unpinned helper will be used, either `hydra remote setup NAME --binary FILE` or
this host's own `hydra-fleet` when this host has exactly the remote's platform;
otherwise, and for any other platform, preflight fails with `platform_unsupported`.
tmux 3.0, curl (only for agent installers) and a group-writable umask are warnings.
Hydra never uses sudo; install missing packages yourself. On Linux, a requirement
that is not ok may carry a `suggestion`, an example command with Debian/Ubuntu
package names (for example `sudo apt-get install git`) next to its `detail`; Hydra
never runs it. An existing Hydra of the
same version that passes the fleet handshake is reported as reusable; other
installs are left untouched.

**Agent installers.** `hydra remote install-agent NAME --agent A` runs the
provider's official installer over `ssh -t` as your user after you approve a plan
that shows the exact command. That command is `umask 022; ` followed by the
recipe, so an account whose umask is 002 (the Ubuntu default) does not end up
with a group-writable agent executable, which location records and probes
refuse. The umask applies only to that installer session.

**Upgrading an enrolled alias.** `hydra remote setup NAME` for an alias that already
exists (and has no setup state) upgrades it. The destination and SSH config come
from the alias; giving a different DEST or `--ssh-config` fails with
`alias_conflict`, because setup never re-points an alias. The host key step accepts
a host that strict SSH already trusts, but still refuses a key that differs from
the alias's recorded one. Provisioning installs a new pin next to the old one. The
final step shows a plan with the old and new Hydra path and version, states that
the previous install stays installed, and after approval (`hydra remote setup NAME
--approve PLAN_SHA256` when non-interactive) rewrites only the alias's `hydra`
path, plus `accepted_host_key` if it was not recorded before; every other alias
field is preserved and the record is replaced atomically. If the alias already
uses a compatible install of this version, provisioning and the alias change are
skipped. A new setup whose NAME is taken by an alias created meanwhile still fails
with `alias_conflict`.

Exit statuses: 0 done, 1 error, 3 approval required, 4 outcome unknown (reconcile
before retrying; nothing is replayed), and 128+n when interrupted by signal n.
Without `--json` the commands print one line per step on stderr followed by the
next command.

**Unconfirmed provisioning.** If the install response is lost (for example the
connection drops), the provision step is `outcome_unknown` (exit 4). Rerunning
`hydra remote provision NAME` never installs again: it checks the recorded prefix
against the private package in `$HYDRA_HOME/fleet/packages/`. If that package was
deleted, Hydra rebuilds it from this installation and the recorded helper source
(pass the same `--binary FILE` if one was used), and uses the result only if its
digest equals the recorded one. When this Hydra has changed since the plan and
cannot rebuild it, the step stays `outcome_unknown` with the message "its local
package is gone and this Hydra cannot rebuild it" and reports the installed
prefix as `data.prefix`. Either run the rerun with the Hydra version that made
the plan, or, after checking that no alias in `hydra remote list` uses that
path, remove the prefix yourself and rerun provisioning twice:

```sh
ssh ubuntu@build-host "rm -rf -- '/home/ubuntu/.local/share/hydra/fleet/DIGEST'"  # data.prefix
hydra remote provision ovh    # nothing at the prefix: install_failed
hydra remote provision ovh    # shows a fresh plan to approve
```

**From the control centre.** `hydra tui` (not only `hydra fleet tui`) has a Hosts
tab (`H`). `A`, or Enter on "+ Add a host", asks for the SSH destination, a name
(the host name by default) and an optional SSH config file, then runs the same
`hydra remote setup ... --json` commands and shows their steps. Unfinished setups
are listed there (from `hydra remote setup list --json`); Enter continues one, `e`
edits one that has not changed the remote yet (the form opens with its destination,
name and SSH config; keeping the name removes the old record, after you approve its
removal plan, and starts again) and `x` removes one's record after showing what stays
on the remote. A name that already has an unfinished setup is caught in the form:
Enter continues that setup instead, `e` edits and `x` removes it. The SSH config field
takes `~/...`. While a step runs the screen shows it with its elapsed time; Esc or `c`
cancels it (Hydra then shows the recorded status) and `b` keeps it running in the
background. Each plan is shown before anything changes:
a new host key needs `yes` typed and Enter, other plans `y` (Enter alone never
approves; `n` or Esc declines and runs nothing). Hydra then reruns exactly the
command the CLI returned, and only if its `--approve` hash or `--fingerprint` is the
plan on screen. A changed or ambiguous host key shows the manual `ssh-keygen`
recovery and has no accept action. Blocking requirements are listed with their
suggested fixes; Enter checks again once they are installed. Of the warnings only a
missing or older tmux pauses setup, because heads cannot run on the host without
tmux 3.0: the requirements are shown with the suggested fix and Enter continues
anyway. Other warnings (curl, a group-writable umask, ...) do not stop setup and
stay visible in the step list. After provisioning
the agent inventory is shown: choose "Install A" to review that agent's installer
plan, "Sign in to A" for one already there, or Continue setup without agents
(Enter on the done "Find agents" step shows the inventory again later). Agent
installers and provider sign-in take over the terminal and return to Hydra when
they finish (on failure after Enter, so the output can be read); a failure is
explained from the error the CLI recorded for that step. An unknown outcome offers Enter
to reconcile, which never replays the earlier attempt.

A development build has no pinned release helper for its version, so its
control centre cannot provision from the Hosts tab on its own: set
`HYDRA_REMOTE_SETUP_BINARY=/abs/path/to/hydra-fleet` (the `--binary FILE` the
CLI's `asset_unavailable` recovery names, for example the output of `make
build-fleet-static`) and the Hosts tab passes it to `hydra remote setup`, shows
the helper as unpinned in the plan and reruns only the provisioning command
that names that same file. Release builds leave it unset.

`make test-u10-loopback` runs the whole guided flow without a VPS: a private
sshd on a loopback port serves a throwaway account (created with `useradd`
when the test runs as root or under passwordless sudo; otherwise it prints
SKIP), the control centre is driven over a PTY through host-key trust, preflight,
provisioning from this checkout's helper, the deterministic agent fixture
installed as `codex` by a private `agent-recipes.json`, a sign-in killed
mid-way and resumed without being repeated, a sign-in whose verification fails
and is retried, a real fixture-agent task launched on the host and found again
after reconnecting, a second setup of the same account, a host whose sshd
hides `git`, untouched unrelated files and no copied credential. Its screens
and logs land in `build/u10-loopback-evidence/`.

## Discover, qualify and enroll hosts

```sh
hydra fleet discover --ssh build --ssh mac --json
hydra fleet discover --inventory /path/hosts.json --select build --json
hydra fleet qualify --ssh build --require list --timeout 5 > qualification.json
hydra fleet enroll review --input qualification.json --candidate cand_6275696c64 \
  --project /srv/project --output enrollment.json
hydra fleet enroll apply --input enrollment.json --confirm INTENT_SHA256
```

`discover` resolves explicitly selected targets with `ssh -G` and makes no
connection; `qualify` adds one strict read-only protocol-1 handshake per selected
candidate and records the authenticated peer fingerprint. `enroll review` binds
selected compatible candidates, their source, principal, fingerprint, policy
digest, project and optional pinned package into a digest-confirmed intent;
`enroll apply` performs the reviewed steps with durable per-host progress. The
candidate/result, inventory, intent and progress schemas are in
[CONTRACTS.md](CONTRACTS.md#fleet-coordination-since-v210). Discovery never
accepts a host key, creates an alias, installs Hydra or submits work.

**Live sources.** `--source KIND[:LOCATOR]` acquires one explicitly selected
inventory through its normal client or the operator's own files and feeds the
same candidate importer. At most four distinct sources per invocation, each run
once (Hydra never polls), each bounded to `--timeout` seconds of client time,
1 MiB of file or client output and 100 records; beyond those the report is
`partial`. Every record is `{name, target, labels}`; credentials never enter a
record. The report carries the scope the records belong to (`account:USER@HOST`
for local files, `network:DOMAIN via HOST` for mDNS) and the client used.

| Source | Reads | Scope and limits |
| --- | --- | --- |
| `ssh-config[:FILE]` | Literal `Host` names from `~/.ssh/config` (or FILE) and its literal `Include` files; each becomes a candidate that the normal `ssh -G` resolution then explains. Patterns (`*`, `?`, `!`), `Match` blocks and glob includes are reported as skipped, never expanded. | The local account; at most 16 files, two include levels. |
| `ansible-inventory:FILE` | Host lines of plain `[group]` sections in an INI hosts file; groups become labels, `ansible_host`/`ansible_user` form the target and `ansible_port` a `port:N` label. `[group:vars]` and `[group:children]` are ignored, ranges and non-SSH connections skipped, and any variable that looks like a secret (`pass`, `token`, `secret`, `private_key`, `become`, `vault`, `credential`) is counted in `credentials_omitted` and dropped. | The local account. |
| `dns-sd[:DOMAIN]` | `_ssh._tcp` services from one terminating `avahi-browse -rtpk` run (port 22 only; names are unescaped). Without `avahi-browse` the source is `unavailable` with reason `client_missing` (`client_unsupported` when only macOS `dns-sd` is present): Hydra never browses the network itself. | `network:DOMAIN` (default `local`). Live multicast acquisition is unqualified; see the roadmap. |

Each acquisition is compared with the previous private snapshot of the same
source (`$HYDRA_HOME/fleet/discovery/sources/`, owner-only) before any
selection: the report shows the cache age and the added, removed, changed and
conflicting records (`--diff` lists them; counts are always present). The cache
is replaced only by a complete or partial acquisition. When the file is gone,
the client is missing or times out, or the source was revoked, the report is
`unavailable`/`revoked`, the command fails with `source_unavailable`/`revoked`,
the stale cache is shown with its age and record count, and nothing from it is
imported. Only records of a complete current acquisition become candidates by
default; `--select NAME` imports named records, also from a partial acquisition,
and `qualify` over a source always requires `--select`. Records that conflict
(two names for one target, one name for two targets, or a target that another
name held in the previous snapshot) or whose identity is revoked are reported
with provenance and never imported; selecting one is an error. A larger or
changed acquisition changes the qualification progress binding, so a resumed
batch cannot silently reuse results for a different membership.

`make test-discovery` covers these adapters with fixture files and a fixture
`avahi-browse` only. No live mDNS network, Ansible installation or provider was
observed; that qualification needs a separately authorized operator run.

## Revoke decisions

```sh
hydra fleet revoke transport build --reason "host key rotated 2026-10-17"
hydra fleet revoke candidate cand_6275696c64 --reason "retired"
hydra fleet revoke source ansible-inventory:/etc/ansible/hosts --reason "moved"
hydra fleet revoke package PACKAGE_SHA256 --reason "superseded build"
hydra fleet revoke project /srv/project --reason "decommissioned"
hydra fleet revoke list [--scope transport] --json
```

A revocation is an explicit local decision about one identity: a candidate (by
`cand_` ID or exact target), a source (`KIND:LOCATOR`), a transport binding (an
alias name together with its recorded accepted host key), a package digest or a
project path. The private record (`$HYDRA_HOME/fleet/revocations/SCOPE/`,
0600 in 0700 directories) keeps who, when, the reason and the superseded
binding: for an alias, the alias record with its target, principal, project and
accepted key; for a source, its cached snapshot summary; for a project, the
aliases mapped to it. The live alias record stays on disk for inspection and is
never edited, and a second revocation of the same identity is refused with
`already_revoked`. Revocation is final for that identity; there is no
un-revoke, so a rotated host is enrolled again under a fresh review and a new
alias.

Revoked bindings block new operations and invalidate pending intents: a revoked
alias cannot be loaded by any `fleet` operation, `fleet task` submission, agent
sign-in or workflow binding (typed `revoked`, with the record's detail), shows
as a `revoked` row in `fleet list`, cannot be re-added, re-set-up or published by
enrollment under the same name, and no endpoint change or source refresh reaches
the host through it. A revoked candidate shows as `revoked` in `discover`,
`qualify` and resumed progress (its earlier evidence is retained under the
status), is reported but not imported from any inventory or live source, and is
refused by `enroll review`. A revoked source is not acquired. A revoked package
digest is refused by `fleet bootstrap` and `enroll review`; a revoked project by
`enroll review` and every `--project` operation. `enroll apply` marks a reviewed
host whose candidate, alias, accepted key, package or project was revoked after
review as `revoked` before any connection; rows already `enrolled` stay as they
are. A host that still presents a revoked alias's accepted key is refused by
review even under a new alias.

Revocation alone cancels no task, uninstalls no package, deletes no remote state
and rewrites no `known_hosts`; those actions keep their own explicit commands.

**Changed host keys.** When strict OpenSSH refuses a changed key during
`qualify`, the candidate's `host_key_changed` result adds `error.host_key`: the
presented fingerprint, key type and offending `known_hosts` file and line as
OpenSSH reported them, the fingerprints Hydra has recorded for that target
(alias `accepted_host_key`, revoked transports' superseded keys, earlier
qualification results) each with its source, and the acceptance path. `enroll
apply` reports `host_key` with the reviewed and the presented fingerprint and
where each was observed; `remote trust-key` adds the alias's recorded
fingerprint next to the presented one. Hydra never accepts the new key or edits
`known_hosts`: verify it out of band, remove the old entry with your own
`ssh-keygen -R`, re-qualify, and review enrollment afresh. The old binding can
then be revoked explicitly so nothing silently re-points to the rotated host.

## Register and bootstrap a host

```sh
hydra remote add ovh ubuntu@build-host
hydra remote list
hydra remote remove ovh
```

Use an ordinary SSH `Host` alias for ports, identities, jump hosts, and connection
settings. Hydra enforces batch authentication and strict host-key verification;
verify the host key through your trusted process first. Aliases are private JSON
records under `$HYDRA_HOME/fleet/remotes/`. `remote add` replaces that alias's record.
Optional `--hydra /absolute/bin/hydra` selects an existing installation and `--home
/absolute/state-directory` isolates remote Hydra state. `--ssh-config
/absolute/file` binds the alias to one OpenSSH client configuration (`ssh -F`), as
a finished guided setup records; every transport for that alias, including
workflow task dispatch, uses it. `--multiplex` enables
OpenSSH ControlMaster/ControlPersist with private control sockets; existing SSH
multiplexing configuration is otherwise respected.

A package contains the shell CLI/libraries, a target-platform fleet executable,
and licenses. It excludes repository configuration, credentials, and live state.
Build the executable on the intended platform, then package and pin the exact
bytes. The executable must match the packaged shell source. Package output names
must be new files.

```sh
hydra fleet package --source /path/to/hydra \
  --binary /path/to/linux/hydra-fleet --output /tmp/hydra-linux.json
# Copy the returned sha256 exactly:
hydra fleet bootstrap ovh --input /tmp/hydra-linux.json --sha256 HASH
```

Bootstrap requires existing Git. Tmux 3.0 or newer is needed for interactive
terminal operations, not bootstrap or headless tasks. It verifies the package hash, transfers
a hash-verified installer executable, rejects non-allowlisted paths, qualifies the
shell and fleet handshakes in isolated state, and publishes an immutable directory
at `~/.local/share/hydra/fleet/HASH`. Only then does it update the local alias's
executable path. Reusing a pin checks its installed bytes. It neither changes the
host's default PATH nor upgrades another installation. Source versions and archive
checksums are release identities; do not bootstrap unreviewed packages.

Fleet headless tasks support Antigravity (`agy`), Cursor Agent, OpenCode, Claude
Code, Codex, and Pi when the selected CLI and credentials are available on that
host. See [agent profiles and inputs](USAGE.md#agent-profiles-and-inputs) for the
headless/interactive matrix and resolution order. Implemented adapters and live
qualification are separate claims; provider sign-in must be checked on the host.

## Authenticate agents

Use `hydra fleet auth login HOST --agent NAME` for native sign-in, or preview and
explicitly copy a supported local credential. SSH access and agent authentication
are separate. Native sign-in keeps provider tokens on the remote Unix account;
credential copy uses an approval-bound preview, transfers only the selected private
file over SSH stdin, and never stores credentials in packages, logs, or Hydra state.
Codex, Pi, OpenCode, and Claude support portable-file copy; Antigravity and Cursor
use native sign-in only. An interrupted transfer is unconfirmed and must be checked
with host/provider status before another explicit copy.

## Observe and reconcile

```sh
hydra fleet handshake --json              # this installation
hydra fleet attention --json               # read-only exact task attention rollup
hydra fleet review KIND PROJECT HOST TASK RUN STEP ATTEMPT HEAD INSTANCE REQUEST BINDING REVISION_SHA256 IDENTITY_SHA256 [REFERENCES_JSON]
hydra fleet review-data KIND PROJECT HOST TASK RUN STEP ATTEMPT HEAD INSTANCE REQUEST BINDING REVISION_SHA256 IDENTITY_SHA256 [REFERENCES_JSON]
hydra fleet list --json --timeout 5 --jobs 4
hydra fleet doctor ovh --json
hydra fleet reconcile --json
hydra fleet watch --interval 5 --timeout 5 --jobs 4
```

The handshake exposes Hydra version, fleet/state/event/JSON protocols, native
protocol expectations and executable presence, project mappings, capabilities,
and supported signals. Native presence does not certify helper compatibility.
The coordinator requires Hydra 2.x, fleet protocol 1, state 2, event 1, JSON 1,
and the requested capability. Fleet is available from 2.1.0; build and bootstrap a qualified
fleet helper before using these commands.

List returns canonical durable head records, including remote project paths.
Desired state is not live agent progress. Doctor retains the remote diagnostic
output and never accepts `--fix`. Aggregate results contain `data.hosts`; each row
has `host`, `ok`, and data or a structured error. Partial failures preserve good
results and return nonzero. Empty fleets produce a successful empty array.

`fleet attention --json` projects the same bounded observations into deterministic
read-only items. Approval rows carry the exact host/task/run/step/attempt/spec
identity and request ID, joined to one unique observed step attempt. Expired
numeric approval records are emitted as `approval_expired` with no action route;
zero means no expiry, while missing, null, or malformed expiry values are
explicit `unknown` observations. Ready result rows carry a `task-result`
inspection route and remain review candidates until
Hydra verification accepts them. Cached or stale rows remain visible with
`route.fresh_action` false; approvals never expose a fresh action route. Missing
identity, missing or malformed expiry, ambiguous approval bindings, unsupported waiting states,
offline hosts, and malformed observations produce explicit `unknown` rows. The
`revision` field is a canonical semantic object string that includes identity,
request expiry/state, execution/result/verification state, and excludes observation
timestamps, so repeated polls and reordered or duplicate requests do not create
new revisions. Provider questions remain unclassified and unsupported observations
stay `unknown`; this producer does not infer provider semantics. The native client
adds per-client seen markers and attention/review navigation, while local workflow
approval remains an explicit shell action and is never implied by seen state.

`fleet review` and `fleet review-data` are read-only exact-subject routes. Pass all
13 identity fields from `fleet attention-data` (use `-` for absent values); the
requested revision and identity hashes bind the selected row and are distinct from
the currently observed revision. `review` emits the versioned JSON envelope;
`review-data` emits the bounded framed text document used by the native TUI. The
review includes verified result, diff, artifact, check, provenance, and current
approval context where available. It never approves, resumes, cancels, pushes,
merges, or opens a reference as evidence.

References are explicit and bounded: at most 16 `transcript`, `log`, or `pr`
objects, with local previews capped at 4096 bytes. URLs remain supplied and
unopened. Diff previews are capped at 128 KiB per head bundle and artifact/evidence
previews share a 96 KiB budget; truncation and unavailable states remain visible.
Review has a fixed 45-second overall producer deadline and uses bounded 5-second
result observations. The native Fleet capture allows 60 seconds for that public
producer; the local native capture allows 13 seconds. The general Fleet
`--timeout` (1–300 seconds, default 5 seconds) applies to observation/SSH commands,
not as a review readiness control. These bounds limit collection time and output;
they do not establish remote/provider parity.

Timeouts bound each SSH invocation, including command execution. Observation uses
one handshake and one operation, each with its own deadline. Workers fill available
slots as hosts finish. Defaults are 4 workers and 5 seconds per observation call;
`--jobs` accepts 1–16 and `--timeout` accepts 1–300 seconds. Mutation calls default
to 300 seconds after negotiation. Output is bounded to 8 MiB per stream.

The native fleet TUI gives each remote request a 3-second deadline. Its snapshot
adapter allows 13 seconds for the four serial request phases (list and overview,
each with a handshake and action) plus one second of local capture overhead.

Errors distinguish `host_key_failed`, `authentication_failed`, `offline`, `timeout`,
`version_mismatch`, `capability_unavailable`, malformed responses, command failures,
and interrupted/unknown outcomes. OpenSSH uses exit 255 for many failures: known
English diagnostics are classified under `LC_ALL=C`; other transport failures keep
their stderr as `offline`. No stronger network diagnosis is inferred.

Reconcile makes a fresh observation from each host's authoritative state. Watch
emits JSON Lines and continues after offline results, refreshing again after
network recovery or sleep. The TUI likewise polls fresh observations. No stale
remote cache is authoritative, and no command is replayed on reconnect. If a
mutation loses its response, inspect heads or workflow runs before deciding what
to do next; an unknown outcome is not permission to repeat it.

## Operate explicitly

Remote arguments after `--` are a JSON argv array on SSH stdin. They are never
interpolated into a remote shell command. An absolute remote project is required
for project operations; Hydra does not clone projects or copy source implicitly.

```sh
hydra fleet init ovh --project '/srv/project with spaces' -- --no-agent --json
# Review the remote .hydra configuration before explicitly trusting it:
hydra fleet init ovh --project '/srv/project with spaces' -- --trust --json
hydra fleet spawn ovh --project '/srv/project with spaces' -- feature-a --no-agent
hydra fleet list ovh --json
# Use current_instance from the observation:
hydra fleet signal ovh --project '/srv/project with spaces' \
  --instance INSTANCE -- feature-a INT
hydra fleet cancel ovh --project '/srv/project with spaces' \
  --instance INSTANCE -- feature-a
hydra fleet attach ovh --project '/srv/project with spaces' \
  --instance INSTANCE -- feature-a
```

Head signal/cancel delivers foreground `INT` (tmux `C-c`) after rechecking the
observed instance under the existing lifecycle lock. A stale instance is refused.
The response means delivered, not task completion. It preserves the head,
worktree, and dirty files. Only `INT` is supported by the direct interrupt command. Use workflow
cancel for whole-workflow cancellation. Attach first checks the advertised
receiver capability, then opens an interactive SSH session with the saved
`HYDRA_HOME`, project, branch, and instance arguments. The receiver validates the
current lifecycle and evaluates the exact project/head/instance environment in a
same-server tmux format-and-attach command. The session ID helps target that
server command but is never an identity proof; replacement sessions and restarted
servers are refused. Receivers must support `session_id` and `fleet-local attach`;
older receivers fail closed and must be upgraded for interactive attachment. A
usable terminal and TERM are required.

```sh
hydra fleet workflow ovh --project /srv/project -- run /srv/plan.yml
# In another terminal while it runs:
hydra fleet workflow ovh --project /srv/project -- runs
hydra fleet workflow ovh --project /srv/project -- status RUN_ID --json
hydra fleet workflow ovh --project /srv/project -- cancel RUN_ID
hydra fleet workflow ovh --project /srv/project -- resume RUN_ID
```

Workflow list/show/validate/dry-run/run/status/cancel/resume delegate to local
workflow policy. `runs` lists recorded run IDs/states so callers can inspect an
in-flight run. Trust, idempotency, recovery, and cancellation remain host-local.
Run is synchronous; use another terminal for status/cancel.

## Explicit configuration and historical bundles

```sh
hydra fleet export ovh --project /srv/project --output /tmp/config.json \
  -- config.yml workflows/check.yml
hydra fleet import ovh --project /srv/other-project --input /tmp/config.json
hydra fleet export ovh --project /srv/project --run RUN_ID --output /tmp/run.json
hydra fleet import ovh --project /srv/other-project --input /tmp/run.json
```

Configuration export selects only `config.yml` or YAML files directly under
`.hydra/workflows`, `profiles`, or `templates`. No implicit file discovery, hooks,
local overrides, credentials, symlinks, or trust records travel. Configuration
import requires `.hydra` to be absent and leaves the result untrusted. Explicitly
selected configuration may itself contain sensitive values: inspect it before
sharing. Existing configuration is never merged or overwritten.

History export requires a terminal workflow and selects its resolved definition,
graph, identity, timestamps, base commit, recorded state, and supported step
results. Live mappings, locks, owner PIDs, and active workflows are excluded.
History import goes under `$HYDRA_HOME/fleet/history` as an inert archive; it is
never imported into runtime workflow state or used as a resume shortcut.

## Admission inspection

Inspect receiving-host admission without changing policy:

```sh
hydra fleet admission build -- status --summary
hydra fleet admission build -- inspect task_ID
```

Initialized-host snapshots include capacity and observation timestamps. These are
observations; the receiving shell authority grants every reservation against live
policy. See [Contracts](CONTRACTS.md#durable-state-v2) for limits, labels, queue
deadlines, and retained claims after owner loss.

## Automatic placement

Remote task submission pins one host by default: the package's immutable `host`
must be the alias you submit to, and nothing is recorded beyond the receiver's
acceptance. Automatic placement (since 3.0) chooses the receiver for **new,
unassigned work** from the registered aliases and records why:

```sh
hydra fleet task prepare --source . --spec task.json --output /tmp/task.json
hydra fleet task submit --placement auto --input /tmp/task.json --key build-42
hydra fleet task submit alpha --input /tmp/task.json --key build-43 --placement alpha
hydra fleet placement explain task_ID --json     # the recorded decision
hydra fleet placement replay task_ID             # re-derives it; must be identical
hydra fleet placement policy                     # effective knobs and drained hosts
hydra fleet placement drain ovh                  # keep new work off a host
hydra fleet placement undrain ovh
```

With `--placement auto` the positional alias is omitted; the package's `host` is
only the prepared template and the chosen host receives a copy whose `spec.host`
and `spec_sha256` are rebound (bundle, inputs and limits are byte-identical).
`--trust-spec` names the digest you reviewed (the prepared package); the
host-bound digest the receiver trusts is reported as `bound_spec_sha256` and is
what `hydra fleet task start HOST --id ID --trust-spec` needs later. `--placement
HOST` is an explicit pin that also records a decision. `--separate-from TASK_ID`
(repeatable, at most 8) excludes the current host of another placed task, which is
how a validator stays off its producer's host.

**Eligibility.** Every registered alias is observed with one handshake and one
`admission status --json` call (serially, each bounded by the task's
`transport_seconds`). A host is excluded, with every applicable reason recorded,
when it is drained, offline or incompatible (`offline`, `timeout`,
`version_incompatible`, ...), lacks `task-accept`/`task-withdraw` or an adapter
capability the spec names (`capability_missing`), does not map the spec's
project path (`project_unmapped`), differs from a `platform.OS-ARCH` capability
(`platform_mismatch`), lacks a required `label.X` in its admission policy
(`label_unsupported`), has no admission observation (`admission_unobserved`),
reports an observation older than `max_observation_age_seconds` or ahead of the
submitter's clock by more than that (`stale_observation`), has a full queue
(`queue_full`) or more queued requests than `max_queued` (`queue_bound`), is
below its disk floor or the task's log/artifact bytes (`disk_floor`,
`disk_insufficient`), would consume validator headroom (`validator_reserved`),
already recorded this task ID (`previous_generation`), or holds a task named by
`--separate-from` (`validator_separation`). An idle host that fails any filter is
not eligible.

**Ranking.** Eligible hosts are ordered by capacity-normalized load
`(reserved + queued) * 1000 / capacity` where `capacity` is the host's
`host_limit`, or 1 when the host advertises no limit (an unbounded host looks
fully loaded as soon as it holds one reservation; configure `host_limit` for
balanced placement), then by reserved slots of the same task class (requests
with the same required-label set), then by queue depth, then by alias order.
The first host receives the task. The receiver's admission remains final: a
definitive refusal (`capability_unavailable`, `unmapped_project`,
`missing_dependency`, `invalid_package`, or a start that fails with `queue_full`)
is recorded and the next-ranked host is tried; an ambiguous dispatch
(`outcome_unknown`) leaves the task assigned to that host and a later submission
with the same key reconciles with that host only, never a second one. Submissions
for one key serialize on the record, so concurrent submitters cannot dispatch the
same task twice. Each candidate also carries typed, versioned `estimates`
(`queue_delay_seconds`, `measured_duration_seconds`, `transfer_cost_bytes`,
estimates schema 1) that are recorded as unavailable and never used in ranking
until measured evidence exists.

**Policy.** `$HYDRA_HOME/fleet/placement-policy.json` (optional, closed schema,
policy version 1) holds `max_observation_age_seconds` (default 60),
`max_queued` (default 8, 0..128) and `validator_reserve_slots` (default 0,
0..64): the number of free slots per host, out of its `host_limit`, kept for
`role.validator` tasks so fan-out of producers cannot fill every host ahead of
validation. The default reserves nothing; raise it where validators demonstrably
wait behind producers. Receivers keep FIFO queue order; placement never
reorders a receiver's queue. Task classes come from capability names:
`role.producer`, `role.validator` (otherwise `general`); receivers accept and
record `role.*` and check `platform.OS-ARCH` against their own platform.

**Record.** Every `--placement` submission writes
`$HYDRA_HOME/fleet/placements/TASK_ID/placement.json` (`fleet-placement` schema
1, digest-sealed): the inputs derived from the spec, the policy used, and for
each assignment generation every round's observations (with ages), candidates
(eligibility, reasons, score, rank, estimates), the decision, and every dispatch
attempt with its reservation outcome (`accepted`, `refused` with the receiver's
code, `unknown`, `error`). `placement explain` prints it; `placement replay`
re-runs the ranking from the recorded observations and policy and fails with
`replay_mismatch` unless the result is identical. A record whose digest does not
match is refused (`not_found`). Drain markers live in
`$HYDRA_HOME/fleet/placement-drained/HOST` and affect only automatic placement.

**Rebalancing.** Only accepted work that has not started moves:

```sh
hydra fleet placement withdraw task_ID        # the current receiver records the withdrawal
hydra fleet task submit --placement auto --input /tmp/task.json --key build-42   # generation 2
```

`withdraw` sends `task withdraw` to the assigned host. The receiver takes the
launch owner lock, records `withdrawal.json` and the durable `withdrawn` state
only for an accepted task without a launch claim, and refuses anything else
(`execution_owned`); a withdrawn task never starts there (`withdrawn`). Nothing
is released because admission is requested only at launch. The submitter then
opens the next generation on the next submission, excluding every host that
recorded the task. A lost withdrawal response marks the generation
`withdrawal: unknown` and blocks transfer (`withdrawal_unconfirmed`) until a
rerun confirms it; running agents are never migrated and uncertain work is never
replayed as balancing. Receivers that advertise `task-withdraw` record the
submitter's `assignment_generation` at acceptance and reject a start, resume or
re-submission that names another generation (`stale_generation`); result
collection admits a snapshot only from the current generation and host
(`stale_generation`), and promotion consumes only collected results, so a late
result from a superseded generation can never be integrated. This fence covers
Hydra's own records; a task's external side effects (pushes, API calls, provider
sessions) need their own idempotency or fencing contract, and expiry of a queue
deadline or lease never authorizes a second execution.

Placement is qualified here with local receivers behind a controlled SSH
boundary. Heterogeneous-host trials comparing explicit and balanced placement
(queue wait, time to verified result, throughput, transfer bytes, starvation)
need real hosts and have not been run; no performance improvement is claimed.

## Task telemetry

Since 3.0 the task owner records what it measured about the execution phase in
the task's `state.json` as an optional, additive `telemetry` object (telemetry
block 1). The block travels with the receipt: `task status`, `task observation`
and the coordinator's `attempt-N/remote/observation.json` carry it under
`runtime.telemetry`, and the sealed `result.json` keeps it under
`receipt.runtime.telemetry`. Receivers without it, and tasks that never started,
carry no block; a reader never treats its absence as zero.

```json
"telemetry": {
  "schema_version": 1,
  "measured_by": "task_owner",
  "scope": "execution_phase",
  "clock": "monotonic",
  "cpu_source": "getrusage_children",
  "rss_source": "getrusage_children_maxrss",
  "invocations": 1,
  "wall_seconds": 12,
  "cpu_user_microseconds": 300000,
  "cpu_system_microseconds": 50000,
  "unmeasured": {"peak_rss_bytes": "rss_not_distinguishable"}
}
```

The provenance fields say who measured and how: the owner process measures the
`hydra workflow run` or `hydra exec` child and every descendant it waited for,
from the moment it launches that child until it reaps it, with `CLOCK_MONOTONIC`
for `wall_seconds` and the difference of `getrusage(RUSAGE_CHILDREN)` for the CPU
times. Preparation (bundle clone, checkout, head spawn), result sealing and
transport are outside the scope. `peak_rss_bytes` is present only when the
execution child raised the owner's children maximum RSS above what earlier
children (git, the head spawn) had reached; otherwise its peak is known to be no
larger than theirs but not itself, and the field is listed under `unmeasured` as
`rss_not_distinguishable`. The other recorded reasons are
`execution_not_started` (the deadline passed before launch), `child_not_reaped`
(the child could not be collected after a cancellation; its CPU is unknown),
`owner_measurement_unavailable` (the owner's clock or rusage call failed),
`prior_invocation_unmeasured` and `overflow`. A resumed task adds a new
invocation's wall and CPU to the recorded totals and keeps the larger RSS;
`invocations` counts the owner runs that contributed. Provider tokens, model and
cost are not part of this block: they come from the exec receipt (`agent.json`)
the result collects as evidence, and only when the provider reported them.
Transport bytes and calls come from the coordinator's `transport-metrics.json`,
which records no time.

No daemon or poller is involved: the measurement is taken once, by the process
that already waits for the child, and written with the state it already writes.
`hydra workflow statistics-data` (schema 5), `statistics-json` (export schema 3)
and the native Statistics view read these records per workflow step through the
read-only `workflow-task remote-tsv` helper and group remote steps into cohorts
by host, placement mode, assignment generation and provider; see
[Contracts](CONTRACTS.md#tui-protocols-and-parity) for the rows and
[workflows](workflows.md#reviewing-a-finished-plan) for the screen. Everything
the receipt does not carry stays unknown with its reason. The block is qualified
here with local receivers behind the local transport and a controlled SSH
loopback; measurements from real remote hosts are operator observations and no
provider or performance claim is made from them.

## Remote work in the control centre

```sh
make build-tui build-fleet
hydra tui                      # local and remote work in one workspace
hydra tui --location remote    # the same, opened on remote work only
hydra fleet tui                # compatibility entry: the same, filtered to remote work
```

`hydra tui` observes every registered remote host beside the local project. A
remote head is a row `host/branch` of the same Work and Overview lists as a
local head, a remote task (receiver-owned, no head on this machine) a row
`task ID`; from 95 columns a `WHERE` column names the machine, and every
summary and detail names the host, the desired state the host recorded and the
observation it comes from (`fresh, 3s old`, `stale, 300s old`, `unknown`). `L`
cycles the location filter (all work, local, remote, then each host) and Esc
shows all work again; the Hosts tab (`H`) lists the observed hosts with their
connection, head count and observation age, Enter there follows one host's
work and `r` asks for a fresh observation of it.

Details, attention (`I`) and review (`r`) use the same keys for remote work.
Select a current interactive remote head and press `a` to open its pane in the
workspace: the selection carries host, project, branch, head and current
instance into `hydra fleet attach`, the remote shell rechecks that composite
identity and the live tmux session before handing the client to tmux, and the
pane behaves like a local one (`Ctrl-B Tab` leaves input, `Ctrl-B x` closes
the client while the owner session lives on, `Ctrl-B r` reconnects, `Ctrl-B n`
switches agents, `Ctrl-B q` quits). `Ctrl-B [` scrolls the host's own tmux
history: Hydra forwards the host tmux's default prefix and `[` through the
client, so the host's copy mode handles PgUp/PgDn and arrows and `q` (or
`Ctrl-B ]`) leaves it; a host tmux configured with another prefix receives
those keys instead, which Hydra cannot verify. `x` on a remote head interrupts
its agent through `hydra fleet cancel` after a confirmation that names the host
and branch (`:` offers `interrupt remote head`); removal, grouping and the
local palette actions run on the host, not here. On a remote task row `Y`,
`N`, `R` and `X` dispatch `hydra fleet task decide|resume|cancel` with the
exact task, request and bound spec after a typed confirmation.

Attach, interrupt and task controls are authorized only by a fresh observation
of a host that answers. A host that stops answering is `disconnected`: its
heads and tasks stay listed as retained rows from the last observation, with
that observation's age, and refuse every action until the host answers again.
When the remote observation itself fails the footer says `Remote: STALE, last
good observation Ns ago` with the reason, every remote status reads `stale`,
and `r` retries. Nothing in the control centre stops, completes or replays
remote work because an observation is missing; remote liveness is never
observed, and a remote head is never counted as running.

`hydra fleet tui` keeps working as a compatibility entry: it opens the same
control centre filtered to remote work and reads the fleet helper's own
`tui-visual-data` and `attention-data` streams. Paths and identifiers that
cannot be represented within native text bounds require the CLI.

Qualification evidence is host- and provider-specific; local fixtures and controlled
SSH failures do not establish live-provider or external-host qualification.
