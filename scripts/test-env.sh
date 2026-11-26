#!/bin/sh
# Test environment that does not depend on the invoking user's configuration.
# Source it, then call test_env_apply <scripts-directory> <private-directory>:
#
#   . "$scripts/test-env.sh"
#   test_env_apply "$scripts" "$case_root"
#
# git: HOME and XDG_CONFIG_HOME point into an empty private directory and
#   GIT_CONFIG_NOSYSTEM drops the system file, so ~/.gitconfig, the XDG config
#   and the default global excludes file are all absent: no diff.mnemonicPrefix,
#   fetch.prune, commit signing, aliases, or a global ignore list that makes
#   fixture repositories skip .env or *.pem. GIT_CONFIG_GLOBAL is not used
#   because review (src/fleet/review_contract.c) refuses to run while it is set.
#   No identity is configured: cases name their own, as they must on CI.
# tmux: scripts/test-env/tmux goes first on PATH and starts every server
#   without a configuration file (see that file). The private HOME keeps dotfiles
#   such as ~/.zshrc, which tmux panes read, and ~/.hydra out of the run too.
#
# The private directory belongs to one run; the caller removes it.
test_env_apply() {
    (umask 077 && mkdir -p "$2/home/.config") || return 1
    HOME="$2/home"
    XDG_CONFIG_HOME="$2/home/.config"
    GIT_CONFIG_NOSYSTEM=1
    # The real tmux, which the wrapper runs: cases link the wrapper into
    # directories of their own and run with PATHs that hold no other tmux.
    # Kept when already set, so a nested apply never records the
    # wrapper itself.
    if [ -z "${HYDRA_TEST_REAL_TMUX:-}" ]; then
        HYDRA_TEST_REAL_TMUX=$(command -v tmux 2>/dev/null) || HYDRA_TEST_REAL_TMUX=
        case $HYDRA_TEST_REAL_TMUX in "$1/test-env/"*) HYDRA_TEST_REAL_TMUX= ;; esac
    fi
    PATH="$1/test-env:$PATH"
    export HOME XDG_CONFIG_HOME GIT_CONFIG_NOSYSTEM PATH HYDRA_TEST_REAL_TMUX
}
