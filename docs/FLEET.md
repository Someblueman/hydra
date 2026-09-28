# Trusted SSH fleets

Fleet coordinates up to 16 trusted Hydra hosts through OpenSSH. It provides pinned
bootstrap, observation, remote head and workflow operations, interactive attach,
explicit configuration/history transport, and a native fleet view.

`fleet discover` and `fleet qualify` inspect explicitly
selected OpenSSH aliases/static records and qualify them with a strict read-only
handshake. Candidate evidence stays separate from registered aliases and trust.

The `remote task interface` prepares exact source/input packages,
persists receiver-owned acceptance and execution, and supports disconnected
command/workflow runs, bounded logs, cancellation, and verified result collection.
Successful committed results enter the existing local integration and approval
flow. Existing direct remote workflow calls remain synchronous.

Fleet and remote tasks shipped in v2.1.0. Headless provider execution and durable
approval suspension are available in v2.2.0; see the [changelog](../CHANGELOG.md).
A remote workflow runs on one selected host. Cross-host DAG coordination and
automatic placement remain on the [roadmap](ROADMAP.md).

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
```

Steps run in order: host key, preflight, provision, agents, then install and
sign-in for each selected agent, verify, and alias. Each step also has its own
command that uses the same state: `hydra remote trust-key|preflight|provision|agents
NAME` and `hydra remote install-agent|sign-in NAME --agent A`. The alias record is
published last and never overwrites a different alias, so a half-configured host
never appears in `hydra fleet list`. Rerunning setup with a different destination
or SSH config for an existing NAME fails with `setup_binding_changed`. Unknown
options are usage errors. `status` and `list` are reserved and cannot be setup
names. `hydra remote setup list [--json]` (`remote-setup-list`) reads every record
read-only, like `setup status`: `data.setups[]` holds `name`, `destination`,
`status` (the first unfinished step's status, `done` when complete, or
`unreadable` with `error{code,message}` for a record it refuses), `complete` and
`next`. When an agent installer or sign-in that ran on your terminal fails, the
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
are listed there; Enter continues one. Each plan is shown before anything changes:
a new host key needs `yes` typed and Enter, other plans `y` (Enter alone never
approves; `n` or Esc declines and runs nothing). Hydra then reruns exactly the
command the CLI returned, and only if its `--approve` hash or `--fingerprint` is the
plan on screen. A changed or ambiguous host key shows the manual `ssh-keygen`
recovery and has no accept action. Blocking requirements are listed with their
suggested fixes; Enter checks again once they are installed. Agent installers and
provider sign-in take over the terminal and return to Hydra when they finish (on
failure after Enter, so the output can be read). An unknown outcome offers Enter
to reconcile, which never replays the earlier attempt.

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
/absolute/state-directory` isolates remote Hydra state. `--multiplex` enables
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

## Native fleet view

```sh
make build-tui
hydra fleet tui
```

The native view displays host-qualified heads and recorded desired state. `j/k`,
search, and views work as usual; recovery shows offline hosts. Select a current
interactive head and press `a` to open its pane in the operator workspace. The
selection includes host, project, branch, head, and current instance; attachment
is refused for stale or unreachable hosts, headless heads, missing identities,
and replaced instances. The remote shell rechecks that composite identity and
the live tmux session before handing the client to tmux.

Inside an attached pane, `Ctrl-B Tab` leaves input (focus goes back to Hydra),
`Ctrl-B x` closes the selected client while leaving the owner session alive, and
`Ctrl-B q` exits the operator view. `c` requests a confirmed interrupt and `q`
restores the terminal and exits when no pane is focused. Actions carry host, project, and
observed instance through the public CLI. Local mutation shortcuts and local
pane preview are disabled in fleet mode. Paths/identifiers that cannot be
represented safely within native text bounds require the CLI.

Qualification evidence is host- and provider-specific; local fixtures and controlled
SSH failures do not establish live-provider or external-host qualification.
