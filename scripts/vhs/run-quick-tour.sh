#!/bin/sh
# Prepare an isolated demo environment, run the quick-tour VHS, then clean up.
# POSIX-compliant; requires: git, tmux, vhs, hydra installed.

set -eu

# Resolve script dir
SCRIPT_DIR=$(CDPATH= cd -- "$(dirname "$0")" && pwd)
REPO_ROOT=$(CDPATH= cd -- "$SCRIPT_DIR/../.." && pwd)
TAPE="$SCRIPT_DIR/quick-tour.tape"

if ! command -v vhs >/dev/null 2>&1; then
    echo "Error: vhs is not installed. See https://github.com/charmbracelet/vhs" >&2
    exit 1
fi
if ! command -v hydra >/dev/null 2>&1; then
    echo "Error: hydra is not installed or not in PATH." >&2
    echo "Hint: run 'sudo make install' from the repo root." >&2
    exit 1
fi

# Create temp workspace
BASE="$(mktemp -d -t hydra-vhs-XXXX)"
DEMO="$BASE/demo"
HY_HOME="$BASE/home"
FAKEBIN="$BASE/bin"
mkdir -p "$DEMO" "$HY_HOME" "$FAKEBIN" "$BASE/xdg" "$BASE/zdot"

# Optionally fake AI CLIs to avoid launching real tools in tmux (default: on)
FAKE_AI="${HYDRA_VHS_FAKE_AI:-1}"
if [ "$FAKE_AI" = "1" ]; then
  cat >"$FAKEBIN/ai-noop" <<'EOF'
#!/bin/sh
:
exit 0
EOF
  chmod +x "$FAKEBIN/ai-noop"
  for name in claude aider gemini copilot cursor codex; do
      ln -sf "ai-noop" "$FAKEBIN/$name"
  done
fi

# Export environment for the demo
export HYDRA_HOME="$HY_HOME"
export HYDRA_NONINTERACTIVE=1
export HYDRA_DISABLE_HOTKEYS=1
export HYDRA_SKIP_AI=1
export HYDRA_DASHBOARD_NO_ATTACH=1
export HOME="$HY_HOME"
export XDG_CONFIG_HOME="$BASE/xdg"
export ZDOTDIR="$BASE/zdot"
mkdir -p "$ZDOTDIR" "$XDG_CONFIG_HOME"

# Prefer repo hydra to ensure HYDRA_SKIP_AI support
if [ -x "$REPO_ROOT/bin/hydra" ]; then
  cat >"$FAKEBIN/hydra" <<EOF
#!/bin/sh
exec "$REPO_ROOT/bin/hydra" "\$@"
EOF
  chmod +x "$FAKEBIN/hydra"
fi

# Minimal tmux config and shim to isolate from user config
TMUX_CONF="$BASE/tmux.conf"
SH_RC="$BASE/shrc"
ZSH_BIN="$(command -v zsh 2>/dev/null || true)"

# Choose shell for panes (prefer zsh, then bash, then sh)
if [ -n "$ZSH_BIN" ]; then
  SHELL_BIN="$ZSH_BIN"
elif [ -x /bin/bash ]; then
  SHELL_BIN="/bin/bash"
else
  SHELL_BIN="/bin/sh"
fi

# Tiny interactive shell setup for nicer prompt/colors (bash-friendly)
cat >"$SH_RC" <<EOF
# Force a consistent shell and PATH inside panes
export SHELL="$SHELL_BIN"
export PATH="$FAKEBIN:""
$PATH
"  # keep indentation newline-safe

# Simple colored prompt: branch only (clean)
if [ -n "\$BASH_VERSION" ]; then
  _git_branch() {
    git rev-parse --is-inside-work-tree >/dev/null 2>&1 || return
    b=\$(git symbolic-ref --quiet --short HEAD 2>/dev/null || git rev-parse --short HEAD 2>/dev/null)
    [ -n "\$b" ] && printf "%s" "\$b"
  }
  PS1='\[\e[1;33m\]\$(_git_branch)\[\e[0m\] \$ '
  export PS1
else
  PS1='demo$ '
  export PS1
fi
EOF

# If zsh is available, use a dedicated demo zshrc via ZDOTDIR
if [ -n "$ZSH_BIN" ]; then
  # Prepare a dedicated demo ZDOTDIR with a minimal zshrc
  if [ -f "$REPO_ROOT/scripts/vhs/zshrc-demo/.zshrc" ]; then
    cp "$REPO_ROOT/scripts/vhs/zshrc-demo/.zshrc" "$ZDOTDIR/.zshrc"
  fi
  cat >"$TMUX_CONF" <<EOF
set -g default-shell $ZSH_BIN
set -g default-terminal "tmux-256color"
set -g status off
set -g mouse off
set -g default-command "env ZDOTDIR=\"$ZDOTDIR\" FAKEBIN=\"$FAKEBIN\" PATH=\"$FAKEBIN:$PATH\" $ZSH_BIN -l"
EOF
elif [ "$SHELL_BIN" = "/bin/bash" ]; then
  cat >"$TMUX_CONF" <<EOF
set -g default-shell /bin/bash
set -g default-terminal "tmux-256color"
set -g status off
set -g mouse off
set -g default-command "/bin/bash --noprofile --norc --rcfile \"$SH_RC\" -i"
EOF
else
  cat >"$TMUX_CONF" <<EOF
set -g default-shell /bin/sh
set -g default-terminal "tmux-256color"
set -g status off
set -g mouse off
set -g default-command "/bin/sh -i -c '. \"$SH_RC\" 2>/dev/null || :; exec /bin/sh -i'"
EOF
fi

REAL_TMUX="$(command -v tmux)"
TMUX_LABEL="$(basename "$BASE")"
cat >"$FAKEBIN/tmux" <<EOF
#!/bin/sh
exec "$REAL_TMUX" -f "$TMUX_CONF" -L "$TMUX_LABEL" "\$@"
EOF
chmod +x "$FAKEBIN/tmux"

# Ensure our shims (tmux/hydra and optional AI stubs) are first in PATH
export PATH="$FAKEBIN:$PATH"

# Debug info for wrapper environment
echo "[vhs-quick-tour] Wrapper SHELL=$SHELL"
echo "[vhs-quick-tour] Hydra binary: $(command -v hydra)"
echo "[vhs-quick-tour] tmux real: $REAL_TMUX, label: $TMUX_LABEL"
echo "[vhs-quick-tour] Pane shell: $SHELL_BIN, rc: $SH_RC, tmux.conf: $TMUX_CONF"

# Initialize demo git repo
(
  cd "$DEMO"
  # Initialize quietly and set default branch to avoid hint noise
  git -c init.defaultBranch=main -c advice.defaultBranchName=false init >/dev/null 2>&1
  git config user.name 'Hydra Demo'
  git config user.email demo@example.com
  printf "hello\n" > README.md
  git add .
  git commit -m 'init' >/dev/null
)

# Point demo's assets/ to repo assets/ so Output path lands in repo
rm -f "$DEMO/assets" 2>/dev/null || true
ln -s "$REPO_ROOT/assets" "$DEMO/assets"

# Run VHS from inside the demo repo
( cd "$DEMO" && vhs "$TAPE" )

OUT="$REPO_ROOT/assets/demos/quick-tour.gif"
if [ ! -f "$OUT" ]; then
  echo "Warning: expected output not found: $OUT" >&2
  echo "Debug: listing demo and repo assets paths:" >&2
  ls -la "$DEMO/assets/demos" 2>/dev/null || true
  ls -la "$REPO_ROOT/assets/demos" 2>/dev/null || true
fi

# Best-effort cleanup of any hydra sessions created
if [ -f "$HYDRA_HOME/map" ]; then
  while IFS=' ' read -r branch session _; do
    [ -n "$session" ] && tmux kill-session -t "$session" 2>/dev/null || true
  done < "$HYDRA_HOME/map"
fi

# Clean files
rm -rf "$BASE"

echo "Quick tour demo complete. GIF at $REPO_ROOT/assets/demos/quick-tour.gif"
