#!/bin/sh
# Record the README's quick tour (quick-tour.gif, quick-tour.mp4) from this
# checkout's native control centre. Builds the TUI, makes a throwaway Git
# repository with real heads, plan and workflow runs under a private
# HYDRA_HOME and tmux socket, drives `hydra-tui` over a pseudo-terminal with
# record.py and renders the captured bytes through kettle's own terminal
# model. Nothing is created inside this checkout except the two outputs (and
# the build directory). See README.md beside this script.
#
#   sh assets/demos/record.sh            # writes assets/demos/quick-tour.{gif,mp4}
#   HYDRA_DEMO_OUTPUT=dir sh assets/demos/record.sh   # writes them to dir
#   HYDRA_DEMO_FRAMES=dir ...            # also keep every rendered frame (PNG)
set -eu
here=$(CDPATH='' cd -- "$(dirname -- "$0")" && pwd)
repo=$(CDPATH='' cd -- "$here/../.." && pwd)
# BUILD_DIR goes to make as given: the dependency files name objects by that
# spelling, so an absolute path for a relative build would miss header changes.
make_build=${BUILD_DIR:-build}
case $make_build in /*) build=$make_build ;; *) build=$repo/$make_build ;; esac
output=${HYDRA_DEMO_OUTPUT:-$here}
export LC_ALL=C.UTF-8 LANG=C.UTF-8

for tool in git tmux make cc python3 ffmpeg fc-match ps awk; do
    command -v "$tool" >/dev/null 2>&1 || { echo "record.sh: $tool is required" >&2; exit 1; }
done
python=$(command -v python3)
"$python" -c 'import PIL' 2>/dev/null || { echo 'record.sh: Python needs Pillow (python3 -m pip install pillow)' >&2; exit 1; }

echo 'Building the control centre...'
make -s -C "$repo" BUILD_DIR="$make_build" build-core build-tui "$make_build/kettle-cells" >/dev/null

real_tmux=$(command -v tmux)
real_git=$(command -v git)
scratch=$(mktemp -d "${TMPDIR:-/tmp}/hydra-demo.XXXXXX")
# tmux socket paths are limited to about 100 bytes, so they get a short directory.
sockets=$(mktemp -d /tmp/hydra-demo-tmux.XXXXXX)

# Stops every process whose arguments name $1 (the heads' tmux panes, the
# agents, anything a run left behind), TERM first and KILL after a moment.
stop_named() {
    for signal in TERM KILL; do
        pids=$(ps -eo pid=,args= | awk -v s="$1" -v me="$$" 'index($0, s) && $1 != me && $2 !~ /awk$/ { print $1 }')
        [ -n "$pids" ] || return 0
        # shellcheck disable=SC2086 # one argument per pid
        kill "-$signal" $pids 2>/dev/null || :
        sleep 1
    done
}

cleanup() {
    status=$?
    trap - EXIT INT TERM HUP
    TMUX_TMPDIR=$sockets "$real_tmux" kill-server 2>/dev/null || :
    stop_named "$scratch"
    stop_named "$sockets"
    case $scratch in */hydra-demo.*) rm -rf "$scratch" ;; esac
    case $sockets in /tmp/hydra-demo-tmux.*) rm -rf "$sockets" ;; esac
    exit "$status"
}
trap cleanup EXIT
trap 'exit 130' INT
trap 'exit 143' TERM
trap 'exit 129' HUP

# A private world: home, Hydra state, Git identity and tmux server.
for name in $(env | sed -n 's/^\(HYDRA_[A-Za-z0-9_]*\)=.*/\1/p'); do
    case $name in HYDRA_DEMO_*) ;; *) unset "$name" ;; esac
done
unset TMUX TMUX_PANE NO_COLOR ENV BASH_ENV
mkdir "$scratch/home" "$scratch/bin" "$scratch/search"
cat > "$scratch/bin/tmux" <<EOF
#!/bin/sh
exec '$real_tmux' -f /dev/null "\$@"
EOF
ln -s "$real_git" "$scratch/bin/git"
cp "$here/demo-agent.sh" "$scratch/bin/scripted"
chmod 755 "$scratch/bin/tmux" "$scratch/bin/scripted"
export HOME="$scratch/home" HYDRA_HOME="$scratch/hydra-home" TMUX_TMPDIR="$sockets"
# Only the system directories: Hosts lists the agent CLIs found on PATH, and
# the recording should not depend on (or show) what this machine has installed.
user_path=$PATH
export PATH="$scratch/bin:$repo/bin:/usr/bin:/bin"
export HYDRA_ROOT="$repo" HYDRA_CORE="$build/hydra-core" HYDRA_TUI_BIN="$build/hydra-tui"
export HYDRA_NONINTERACTIVE=1 HYDRA_NO_SWITCH=1
export GIT_CONFIG_GLOBAL=/dev/null GIT_CONFIG_NOSYSTEM=1
export GIT_AUTHOR_NAME='Hydra Demo' GIT_AUTHOR_EMAIL=demo@example.invalid
export GIT_COMMITTER_NAME='Hydra Demo' GIT_COMMITTER_EMAIL=demo@example.invalid
export GIT_AUTHOR_DATE='2026-11-02T09:00:00Z' GIT_COMMITTER_DATE='2026-11-02T09:00:00Z'

echo 'Setting up a throwaway project...'
project="$scratch/search"
cd "$project"
git init -q -b main
mkdir src tests
printf '# search\n\nA small C service that indexes and searches local text files.\n' > README.md
printf 'test:\n\tsh tests/search_test.sh\n' > Makefile
printf '/* Scans every file for each query. */\nint search(const char *query);\n' > src/search.c
printf '/* Orders results by score. */\nint rank(void);\n' > src/rank.c
printf '#!/bin/sh\necho 42 passed\n' > tests/search_test.sh
git add .
git commit -qm 'Initial search service'

# Runs a setup command quietly; its output is shown only when it fails.
quiet() {
    "$@" > "$scratch/step.log" 2>&1 || { cat "$scratch/step.log" >&2; echo "record.sh: failed: $*" >&2; exit 1; }
}
quiet hydra init --trust --worktree-root "$scratch/heads"
# "scripted" is demo-agent.sh: an agent profile like any other, but canned.
quiet hydra agent init scripted --executable "$scratch/bin/scripted" --prompt-mode task-file
quiet hydra spawn search-index --profile scripted --prompt 'Plan an inverted index'
quiet hydra plan propose "$here/plan.json" --branch search-index
quiet hydra spawn fix-tests --profile scripted --prompt 'Fix the flaky rank test'
quiet hydra spawn docs-refresh --profile scripted --prompt 'Refresh the README examples'
# The recording never shows the clock or the host name of tmux's status line.
tmux set-option -g status off
# A terminal that stopped: its worktree stays and Inbox offers a restart.
tmux kill-session -t docs-refresh

# Real recorded workflow runs: ci-check succeeds after one retry, docs-check
# fails and release-notes waits for an approval (exit 3).
run_workflow() {
    if hydra workflow run "$here/workflows/$1.yml" >/dev/null 2>"$scratch/$1.err"; then status=0; else status=$?; fi
    case $2:$status in 0:0 | 3:3 | fail:[!03]*) return 0 ;; esac
    cat "$scratch/$1.err" >&2
    echo "record.sh: workflow $1 exited $status (expected $2)" >&2
    exit 1
}
run_workflow ci-check 0
run_workflow docs-check fail
run_workflow release-notes 3

# The agents have printed their transcripts once their prompt is up.
for head in search-index fix-tests; do
    tries=0
    until tmux capture-pane -p -t "$head" 2>/dev/null | grep -q '^> *$'; do
        tries=$((tries + 1))
        [ "$tries" -lt 100 ] || { echo "record.sh: $head never showed its prompt" >&2; exit 1; }
        sleep 0.1
    done
    # The conversation is the agent's own output, not the shell that started it.
    tmux clear-history -t "$head"
done

echo 'Recording the tour...'
mkdir "$scratch/capture"
HYDRA_DEMO_HYDRA="$repo/bin/hydra" HYDRA_DEMO_REMOTE_FIXTURE="$here/fixtures/remote.tsv" \
    "$python" "$here/record.py" capture --tui "$build/hydra-tui" --hydra "$here/hydra-shim.sh" \
    --cells "$build/kettle-cells" --cwd "$project" --out "$scratch/capture"

# Every process of the session ends before the slow part.
TMUX_TMPDIR=$sockets "$real_tmux" kill-server 2>/dev/null || :
stop_named "$scratch"

echo 'Rendering frames and encoding...'
mkdir -p "$output"
PATH=$user_path "$python" "$here/record.py" render --cells "$build/kettle-cells" --capture "$scratch/capture" \
    --output "$output" ${HYDRA_DEMO_FRAMES:+--frames "$HYDRA_DEMO_FRAMES"}
