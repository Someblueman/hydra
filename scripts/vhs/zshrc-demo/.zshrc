# Minimal, fast demo zshrc with a clean prompt and git branch.

setopt PROMPT_SUBST
autoload -U colors && colors

parse_git_branch() {
  command git rev-parse --is-inside-work-tree >/dev/null 2>&1 || return
  local b
  b=$(command git symbolic-ref --quiet --short HEAD 2>/dev/null || command git rev-parse --short HEAD 2>/dev/null) || return
  [[ -n $b ]] && printf '%s' "$b"
}

# Prompt: branch only (clean and compact)
PROMPT='%F{yellow}$(parse_git_branch)%f %# '
RPROMPT=''
PROMPT_EOL_MARK=''

# Safer PATH prepend (FAKEBIN can be set by wrapper)
if [[ -n $FAKEBIN ]]; then
  path=($FAKEBIN $path)
fi

export EDITOR=vi
