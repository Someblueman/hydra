# Fleet pilot

Fleet coordinates up to 16 trusted Hydra hosts through OpenSSH. It provides pinned
bootstrap, observation, remote head and workflow operations, interactive attach,
explicit configuration/history transport, and a native fleet view.

The in-progress [remote task interface](REMOTE_TASKS.md) adds source package
preparation, durable receiver acceptance, submission-key deduplication, and task
status. Explicit digest authorization starts a detached receiving-host owner for
command or workflow execution. Cancellation and result collection are still under
implementation. Existing direct remote workflow calls remain synchronous.

## Architecture and installation

The optional `hydra-fleet` executable is C. It owns JSON validation, SSH transport,
bounded child processes, aggregation, and fleet configuration/bundles. OpenSSH owns
host resolution, credentials, host keys, and connection sharing. Head and workflow
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

Bootstrap requires existing Git and tmux. It verifies the package hash, transfers
a hash-verified installer executable, rejects non-allowlisted paths, qualifies the
shell and fleet handshakes in isolated state, and publishes an immutable directory
at `~/.local/share/hydra/fleet/HASH`. Only then does it update the local alias's
executable path. Reusing a pin checks its installed bytes. It neither changes the
host's default PATH nor upgrades another installation. Source versions and archive
checksums are release identities; do not bootstrap unreviewed packages.

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
and the requested capability. Released 2.0.0 predates fleet; bootstrap a qualified
fleet build before using these commands.

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
worktree, and dirty files. Only `INT` is supported in this pilot. Use workflow
cancel for whole-workflow cancellation. Attach resolves that instance's session,
then runs ordinary interactive `ssh -t ... tmux attach-session`; tmux detach works
normally. A usable terminal and TERM are required.

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

## Native fleet view

```sh
make build-tui
hydra fleet tui
```

The native view displays host-qualified heads and recorded desired state. `j/k`,
search, and views work as usual; recovery shows offline hosts. `a` attaches, `c`
requests a confirmed interrupt, and `q` restores the terminal and exits. Actions
carry host, project, and observed instance through the public CLI. Local mutation
shortcuts and local pane preview are disabled in fleet mode. Paths/identifiers
that cannot be represented safely within native text bounds require the CLI.

See [fleet acceptance](FLEET_ACCEPTANCE.md) for exact qualification evidence.
