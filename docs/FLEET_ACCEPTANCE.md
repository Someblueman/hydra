# Fleet pilot acceptance — 5 September 2026

This records local qualification on `codex/fleet`, plus real SSH acceptance on the
user-provided Ubuntu VPS. It is not a release or hosted-CI claim. No Git commit,
push, merge, tag, or release was performed during this implementation.

## Build and automated verification

- `make test-all` passed, including shell, C fleet, native TUI, PTY, parity,
  installation, packaging, workflow, and onboarding checks.
- `make sanitize` passed with the repository's supported platform flags.
- The fleet C unit executable also passed AddressSanitizer/UBSan on Linux, compiled
  with `-fsanitize=address,undefined -fno-omit-frame-pointer` and strict C99 warnings.
- `make lint` and `git diff --check` passed.
- Native deterministic/PTY tests include fleet desired-state labeling and remote
  action hints. The PTY suite passed 46 checks.
- Local `otool -L build/hydra-fleet` listed only libSystem; JSON-C is statically
  linked. Builds used JSON-C 0.19 on macOS and the VPS's packaged development library.

An additional macOS AddressSanitizer run stalled before `main` in ASan/dyld malloc
initialization. A process sample established that location; the test process was
stopped. Linux ASan and the repository's normal macOS UBSan qualification passed.

## Real-host scenario

SSH authenticated using the supplied Ubuntu account with batch mode and strict
host-key checking. The host initially had Git, tmux, and a compiler, but no Hydra.
JSON-C development packages were downloaded and extracted inside the disposable
`/tmp/hydra-fleet-acceptance.EzTpHB` directory; no system packages were installed.
The Linux executable was built there and copied back for packaging.

The final test package SHA-256 was:

```text
e2b7b79688ba116a2770e2283167eebc603243362e3967204c6e1b8a44615950
```

It contained the tested Linux fleet executable and current shell source. The
public package/bootstrap commands installed it at the matching immutable prefix
under `/home/ubuntu/.local/share/hydra/fleet/`. Repeating bootstrap with that same
pin validated the existing bytes and succeeded. The alias used isolated remote
state at `/tmp/hydra-fleet-acceptance.EzTpHB/state`.

These were the substantive CLI operations (`HYDRA_HOME` selected the isolated
local acceptance aliases throughout):

```sh
hydra fleet package --source "$repo" --binary "$linux_binary" --output "$package"
hydra fleet bootstrap ovh --input "$package" --sha256 "$pin"
hydra fleet init ovh --project "$project" -- --no-agent --json
hydra fleet spawn ovh --project "$project" -- fleet-check --no-agent \
  --prompt 'literal $(touch /tmp/hydra-fleet-injection) ; spaces'
# Refused as untrusted. After reviewing the generated configuration:
hydra fleet init ovh --project "$project" -- --no-agent --trust --json
hydra fleet spawn ovh --project "$project" -- fleet-check --no-agent \
  --prompt 'literal $(touch /tmp/hydra-fleet-injection) ; spaces'
hydra fleet list --json
hydra fleet cancel ovh --project "$project" --instance "$instance" -- fleet-check
hydra fleet signal ovh --project "$project" --instance instance_wrong -- fleet-check INT
hydra fleet attach ovh --project "$project" --instance "$instance" -- fleet-check
```

`project` was `/tmp/hydra-fleet-acceptance.EzTpHB/repo with space`. Spawn succeeded
only after explicit trust. The head was `head_13d26e85be47ce3124a7`, instance
`instance_54c3d9f65e8e9d62b245`. The command-substitution sentinel was absent.
A current-instance interrupt returned `delivered:true`; the stale request returned
`stale_instance`. Interactive attach opened the remote tmux session; ordinary tmux
detach returned successfully. TERM was explicitly `xterm-256color` for the PTY.

```sh
hydra fleet workflow ovh --project "$project" -- run "$workflow"
hydra fleet workflow ovh --project "$project" -- runs
hydra fleet workflow ovh --project "$project" -- status "$run" --json
hydra fleet workflow ovh --project "$project" -- cancel "$run"
hydra fleet export ovh --project "$project" --output "$config_bundle" -- config.yml
hydra fleet import ovh --project "$import_project" --input "$config_bundle"
hydra fleet export ovh --project "$project" --run "$finished_run" --output "$history_bundle"
hydra fleet import ovh --project "$import_project" --input "$history_bundle"
```

The first workflow (`run_3ef39b3c3270a7c1c6b5`) succeeded. The long-running workflow
`run_4695bed117f799d037d6` was cancelled while active; both its run and step status
reported `cancelled`. `workflow ... runs` listed both IDs with recorded states.
Configuration import created a new `.hydra` directory in the disposable import
repository. Historical import created an inert directory under `fleet/history`,
not runtime workflow state. Unit tests refused traversal, existing configuration,
and an owner-PID history record.

## Native view and recovery

`hydra fleet tui-data` returned the real remote head and project mapping. A
headless native render showed `running` under **recorded desired state**, without
asserting liveness. In an interactive PTY, the native fleet view opened; `c` required
`yes`, invoked the remote instance-bound interrupt, received `delivered:true`, and
returned to the view. `q` exited with status 0 and restored terminal state.

Watch was exercised against the same real host with a controlled local transport
interruption. Its JSON-line success sequence was `[true, false, true]`: the middle
invocation returned an SSH transport error, then real SSH observations resumed
without restarting watch or replaying a mutation. This simulates a disconnected
transport; it is not a physical laptop-sleep test.

After acceptance, the disposable head was removed through the remote shell CLI.
The cleanup checked the worktree list and absence of that exact tmux session.
The final pinned installation is retained for review; it does not change remote
PATH or replace a system installation. Temporary source/dependency/test state and
the earlier test pins were removed after evidence was captured.

## Remaining release qualification

The working tree is uncommitted. Hosted checks on a reviewed/merged commit,
release-time version assignment, final archive production, and publication remain
explicit next actions under [the release policy](VERSIONING.md). Pilot
bounds are 16 hosts, INT-only head signalling, and explicit selected bundles;
there is no distributed scheduler or automatic mutation retry.
