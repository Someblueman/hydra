#!/bin/sh
# YAML config demo: prepare a .hydra/config.yml, spawn, attach to the session, show windows/panes, then clean up.
set -eu

SCRIPT_DIR=$(CDPATH= cd -- "$(dirname "$0")" && pwd)
REPO_ROOT=$(CDPATH= cd -- "$SCRIPT_DIR/../.." && pwd)
TAPE="$SCRIPT_DIR/yaml-demo.tape"

command -v vhs >/dev/null 2>&1 || { echo "vhs not installed" >&2; exit 1; }
command -v hydra >/dev/null 2>&1 || { echo "hydra not installed" >&2; exit 1; }

BASE="$(mktemp -d -t hydra-vhs-yaml-XXXX)"
DEMO="$BASE/demo"
HY_HOME="$BASE/home"
FAKEBIN="$BASE/bin"
ZDOTDIR="$BASE/zdot"
XDG="$BASE/xdg"
mkdir -p "$DEMO" "$HY_HOME" "$FAKEBIN" "$ZDOTDIR" "$XDG"

# Optional AI stubs
FAKE_AI="${HYDRA_VHS_FAKE_AI:-1}"
if [ "$FAKE_AI" = "1" ]; then
  cat >"$FAKEBIN/ai-noop" <<'EOF'
#!/bin/sh
:
exit 0
EOF
  chmod +x "$FAKEBIN/ai-noop"
  for name in claude aider gemini copilot cursor codex; do ln -sf ai-noop "$FAKEBIN/$name"; done
fi

# Prefer repo hydra
if [ -x "$REPO_ROOT/bin/hydra" ]; then
  cat >"$FAKEBIN/hydra" <<EOF
#!/bin/sh
exec "$REPO_ROOT/bin/hydra" "\$@"
EOF
  chmod +x "$FAKEBIN/hydra"
fi

# Minimal tmux config + shim
TMUX_CONF="$BASE/tmux.conf"
REAL_TMUX="$(command -v tmux)"
TMUX_LABEL="$(basename "$BASE")"
ZSH_BIN="$(command -v zsh 2>/dev/null || true)"
if [ -n "$ZSH_BIN" ] && [ -f "$REPO_ROOT/scripts/vhs/zshrc-demo/.zshrc" ]; then
  cp "$REPO_ROOT/scripts/vhs/zshrc-demo/.zshrc" "$ZDOTDIR/.zshrc"
  cat >"$TMUX_CONF" <<EOF
set -g default-shell $ZSH_BIN
set -g default-terminal "tmux-256color"
set -g status off
set -g mouse off
set -g default-command "env ZDOTDIR=\"$ZDOTDIR\" FAKEBIN=\"$FAKEBIN\" PATH=\"$FAKEBIN:$PATH\" $ZSH_BIN -l"
EOF
else
  cat >"$TMUX_CONF" <<'EOF'
set -g default-shell /bin/sh
set -g default-terminal "tmux-256color"
set -g status off
set -g mouse off
set -g default-command "/bin/sh -i"
EOF
fi
cat >"$FAKEBIN/tmux" <<EOF
#!/bin/sh
exec "$REAL_TMUX" -f "$TMUX_CONF" -L "$TMUX_LABEL" "\$@"
EOF
chmod +x "$FAKEBIN/tmux"
export PATH="$FAKEBIN:$PATH"

# Demo env
export HYDRA_HOME="$HY_HOME"
export HYDRA_NONINTERACTIVE=1
export HYDRA_DISABLE_HOTKEYS=1
export HYDRA_SKIP_AI=1
export HYDRA_DASHBOARD_NO_ATTACH=1
export HYDRA_NO_SWITCH=1
export HOME="$HY_HOME"
export ZDOTDIR
export XDG_CONFIG_HOME="$XDG"

# Init repo and YAML config
(
  cd "$DEMO"
  git -c init.defaultBranch=main -c advice.defaultBranchName=false init >/dev/null 2>&1
  git config user.name 'Hydra Demo'
  git config user.email demo@example.com
  mkdir -p .hydra
  cat > .hydra/config.yml <<'YAML'
windows:
  - name: editor
    panes:
      - cmd: bash -lc 'echo editor-left'
      - cmd: bash -lc 'echo editor-right'
        split: v
  - name: server
    panes:
      - cmd: bash -lc 'for i in 1 2 3 4 5; do echo server: $i; sleep 1; done'
startup:
  - echo "YAML setup complete"
YAML
  git add .
  git commit -m 'init with yaml' >/dev/null
)

# Prepare session name for tape attach
(
  cd "$DEMO"
  : > session-name.txt
)

# Link assets
rm -f "$DEMO/assets" 2>/dev/null || true
ln -s "$REPO_ROOT/assets" "$DEMO/assets"

# Run tape
( cd "$DEMO" && vhs "$TAPE" )

# Cleanup
if [ -f "$HYDRA_HOME/map" ]; then
  hydra kill --all --force >/dev/null 2>&1 || true
fi
rm -rf "$BASE"
echo "YAML demo done. GIF at $REPO_ROOT/assets/demos/yaml-demo.gif"
