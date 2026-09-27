#!/bin/sh
# Synthetic Codex-style worker: changes and commits files in its worktree, then
# reports a final message and token usage in Codex JSONL. Never a live agent.
set -eu
case " $* " in
    *' --version '*) printf 'codex-cli 0.99.0-fixture\n'; exit 0 ;;
    *' --help '*) printf 'fixture exec --json resume\n'; exit 0 ;;
esac
cat > /dev/null
mkdir -p lib
# shellcheck disable=SC2016
printf '#!/bin/sh\nkill_dry_run() { printf "would stop %%s\\n" "$1"; }\n' > lib/kill_dry.sh
printf 'hydra kill --dry-run previews the targets\n' >> README.md
git add lib/kill_dry.sh README.md
git -c user.name=Worker -c user.email=worker@example.invalid -c commit.gpgSign=false commit -qm 'feat(kill): add --dry-run preview'
printf 'It never asks for confirmation.\n' >> README.md
git add README.md
git -c user.name=Worker -c user.email=worker@example.invalid -c commit.gpgSign=false commit -qm 'docs: describe kill --dry-run'
printf '%s\n' '{"type":"thread.started","thread_id":"fixture-session"}'
printf '%s\n' '{"type":"item.completed","item":{"type":"agent_message","text":"Implemented hydra kill --dry-run.\nThe preview lists the targets it would stop without changing state."}}'
printf '%s\n' '{"type":"turn.completed","usage":{"input_tokens":3850000,"output_tokens":23000,"cached_input_tokens":3740000}}'
