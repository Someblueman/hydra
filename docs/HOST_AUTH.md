# Agent authentication on fleet hosts

SSH access and agent authentication are separate. Register and bootstrap the host
first, then authenticate each agent under the remote Unix account that runs tasks.
Hydra provides native sign-in and an explicit copy flow for portable credential
files. Credential presence does not prove a valid login; run an agent task to check
that the provider accepts it.

## Native sign-in

```sh
hydra fleet auth login ovh --agent codex
hydra fleet auth login ovh --agent claude
hydra fleet auth login ovh --agent opencode
hydra fleet auth login ovh --agent pi
hydra fleet auth login ovh --agent agy
hydra fleet auth login ovh --agent cursor
```

These open an SSH terminal running, respectively, `codex login --device-auth`,
`claude auth login`, `opencode auth login`, `pi` (enter `/login`), `agy` (follow
startup sign-in), or `cursor-agent login`. Follow the
provider's prompts. `--executable /absolute/path/to/agent` selects a privately
installed CLI. The agent owns the login, browser/device interaction, and token
refresh. Hydra does not record the terminal output or copy keychain credentials.
Native sign-in is also available when the remote Hydra lacks the copy protocol.

Antigravity and Cursor support native sign-in only through Hydra. Their credential
stores are not copied or inspected by `fleet auth status|preview|copy`; those
operations support the four agents in the table below. On the execution host,
use `cursor-agent status` or an actual Antigravity task to check native readiness.
See [supported agents](PROFILES.md) for execution capabilities.

## Copy a local credential

```sh
hydra fleet auth status ovh --agent codex
hydra fleet auth preview ovh --agent codex
# Review the destination, create/replace action, and account being granted.
hydra fleet auth copy ovh --agent codex --approve EXACT_APPROVAL_SHA256

hydra fleet auth preview ovh --agent pi --provider anthropic
hydra fleet auth copy ovh --agent pi --provider anthropic \
  --approve EXACT_APPROVAL_SHA256
```

The preview contains paths and hashes, never tokens. Its approval hash binds the
SSH alias and target, Hydra executable and home, source bytes, selected credential,
and destination path and existing bytes. Copy recomputes this preview. Changed
source or destination bytes require a fresh preview and approval. `--source
/absolute/private/file.json` explicitly selects a different local source. Use the
same source and provider options for preview and copy. A copy grants that remote
account use of the selected provider account; previews do not transfer credentials.

| Agent | Default credential file | Copy selection |
| --- | --- | --- |
| Codex | `~/.codex/auth.json` (`CODEX_HOME`) | Whole authentication cache |
| Pi | `~/.pi/agent/auth.json` (`PI_CODING_AGENT_DIR`) | Required `--provider NAME` |
| OpenCode | `~/.local/share/opencode/auth.json` (`XDG_DATA_HOME`) | Required `--provider NAME` |
| Claude Code | `~/.claude/.credentials.json` (`CLAUDE_CONFIG_DIR`) | Whole portable authentication file |

Environment overrides are resolved independently in the local and remote process
environments. An override directory must already exist. `HYDRA_HOME` isolates
Hydra state; it does not relocate provider credentials. The provider processes must
use the same credential location as the remote authentication commands.

Pi and OpenCode merge only the selected entry and preserve other remote providers.
Static API keys and supported OAuth entries are accepted; Pi command/environment
key recipes beginning with `!` or `$` are refused and never evaluated. This does
not transfer models, profiles, tools, or broader configuration. Codex keyring-only
and macOS Claude keychain credentials require native host sign-in or an explicitly
provided portable file. Hydra does not search or export the system keychain.

## Storage and recovery

Copy requires the remote `agent-auth` capability and authentication protocol 1
before sending a credential. Transfer uses SSH stdin, never command arguments.
Subprocess transport uses private unlinked temporary input files. Credentials are
not written to Hydra task packages, logs, result bundles, configuration exports,
or durable preview files. Remote errors are reduced to fixed messages so an SSH
failure that echoes its input cannot disclose a transferred credential.

Credential JSON is bounded to 64 KiB. Source and existing destination files must be
regular, owned by the current account, private, and have no extra hard links.
Credential path components reject symlinks after resolving the configured root;
parents must have safe ownership and permissions. New directories are mode 0700
and replacement files are mode 0600. Existing permissions are never relaxed or
repaired automatically. No backup credential copies are retained.

The receiver serializes Hydra copies with a private lock, checks the old digest
again before atomic installation, and syncs the file and directory. Provider
clients do not share this lock: perform setup while those clients are idle. Hydra
does not promise a transaction against simultaneous provider token refresh, sync
refreshed tokens back to another host, or refresh OAuth tokens itself.

An interrupted transfer has an unconfirmed outcome. Inspect host status and the
provider's own login status before another explicit copy; Hydra never replays it.
Revocation and logout remain provider operations on each host.
