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

Inside an attached pane, `Ctrl-B Tab` changes focus back to Hydra, `Ctrl-B x`
closes the selected client while leaving the owner session alive, and `Ctrl-B q`
exits the operator view. `c` requests a confirmed interrupt and `q` restores the
terminal and exits when no pane is focused. Actions carry host, project, and
observed instance through the public CLI. Local mutation shortcuts and local
pane preview are disabled in fleet mode. Paths/identifiers that cannot be
represented safely within native text bounds require the CLI.

Qualification evidence is host- and provider-specific; local fixtures and controlled
SSH failures do not establish live-provider or external-host qualification.
