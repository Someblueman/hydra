#!/bin/sh
# Minimal YAML support for Hydra session configuration
# Supports a tiny subset:
# config.(yml|yaml):
# windows:
#   - name: editor
#     panes:
#       - nvim
#       - bash
#   - name: server
#     panes:
#       - npm run dev
# startup:
#   - echo hello

# Find YAML config file in precedence order
# Usage: locate_yaml_config <worktree> <repo_root>
locate_yaml_config() {
    wt="$1"; repo="$2"
    for base in "$wt/.hydra" "$repo/.hydra" "${HYDRA_HOME:-}"; do
        [ -z "$base" ] && continue
        if [ -f "$base/config.yml" ]; then
            echo "$base/config.yml"; return 0
        fi
        if [ -f "$base/config.yaml" ]; then
            echo "$base/config.yaml"; return 0
        fi
    done
    return 1
}

# Apply YAML config: create windows/panes and send startup commands
# Supports window dir and env, pane split/dir/env
# Usage: apply_yaml_config <config_path> <session> <worktree> <repo_root>
apply_yaml_config() {
    cfg="$1"; session="$2"; wt="$3"; repo="$4"
    [ -f "$cfg" ] || return 0

    awk '
      function ltrim(s){ sub(/^\s+/,"",s); return s }
      function rtrim(s){ sub(/\s+$/,"",s); return s }
      function trim(s){ return rtrim(ltrim(s)) }
      function indent(s){ match(s,/^[ ]*/); return RLENGTH }
      function flush_pane(){
        if(p_cmd!=""){ printf("PANE\t%s\t%s\t%s\t%s\n", p_cmd, p_split, p_dir, p_env); p_cmd=""; p_split=""; p_dir=""; p_env="" }
      }
      BEGIN{ ctx=""; subctx=""; penv_mode=0; wenv_mode=0 }
      /^[[:space:]]*#/ { next }
      /^[[:space:]]*$/ { next }
      { i=indent($0); line=$0 }
      /^windows:/ { flush_pane(); ctx="windows"; next }
      /^startup:/ { flush_pane(); ctx="startup"; wenv_mode=0; penv_mode=0; next }

      # Windows entries
      ctx=="windows" && i>=2 && $0 ~ /- name:/ {
        flush_pane();
        # Emit previous pending window by resetting state
        w_name=$0; sub(/.*- name:[[:space:]]*/,"",w_name); w_name=trim(w_name);
        # Reset window-level attributes
        w_dir=""; w_env=""; subctx="window"; penv_mode=0; wenv_mode=0;
        print "WIN\t" w_name "\t" w_dir "\t" w_env;
        next
      }
      ctx=="windows" && subctx=="window" && i>=4 && $0 ~ /^[[:space:]]*dir:/ {
        w_dir=$0; sub(/.*dir:[[:space:]]*/,"",w_dir); w_dir=trim(w_dir);
        print "WATTR\tDIR\t" w_dir;
        next
      }
      ctx=="windows" && subctx=="window" && i>=4 && $0 ~ /^[[:space:]]*layout:/ {
        w_layout=$0; sub(/.*layout:[[:space:]]*/,"",w_layout); w_layout=trim(w_layout);
        print "WATTR\tLAYOUT\t" w_layout;
        next
      }
      ctx=="windows" && subctx=="window" && i>=4 && $0 ~ /^[[:space:]]*env:/ {
        wenv_mode=1; next
      }
      ctx=="windows" && wenv_mode==1 && i>=6 && $0 ~ /:/ {
        kv=$0; sub(/^[[:space:]]*/,"",kv); k=kv; sub(/:.*/,"",k); sub(/^[^:]*:[[:space:]]*/,"",kv);
        if(w_env!=""){ w_env=w_env ";" }
        w_env=w_env k "=" kv;
        print "WATTR\tENV\t" k "=" kv;
        next
      }
      ctx=="windows" && i>=4 && $0 ~ /^[[:space:]]*panes:/ {
        subctx="panes"; penv_mode=0; next
      }
      ctx=="windows" && subctx=="panes" && i>=6 && $0 ~ /^[[:space:]]*-/ {
        # Flush previous pane if any
        flush_pane();
        ent=$0; sub(/^[[:space:]]*-[[:space:]]*/,"",ent); ent=trim(ent);
        if(ent ~ /^cmd:/){ sub(/^cmd:[[:space:]]*/,"",ent) }
        p_cmd=ent; p_split=""; p_dir=""; p_env=""; penv_mode=0;
        next
      }
      ctx=="windows" && subctx=="panes" && i>=8 && $0 ~ /^[[:space:]]*split:/ {
        p_split=$0; sub(/.*split:[[:space:]]*/,"",p_split); p_split=trim(p_split);
        next
      }
      ctx=="windows" && subctx=="panes" && i>=8 && $0 ~ /^[[:space:]]*dir:/ {
        p_dir=$0; sub(/.*dir:[[:space:]]*/,"",p_dir); p_dir=trim(p_dir);
        next
      }
      ctx=="windows" && subctx=="panes" && i>=8 && $0 ~ /^[[:space:]]*env:/ {
        penv_mode=1; next
      }
      ctx=="windows" && subctx=="panes" && penv_mode==1 && i>=10 && $0 ~ /:/ {
        kv=$0; sub(/^[[:space:]]*/,"",kv); k=kv; sub(/:.*/,"",k); sub(/^[^:]*:[[:space:]]*/,"",kv);
        if(p_env!=""){ p_env=p_env ";" }
        p_env=p_env k "=" kv;
        next
      }
      # Startup commands list
      ctx=="startup" && i>=2 && $0 ~ /^[[:space:]]*-/ {
        scmd=$0; sub(/^[[:space:]]*-[[:space:]]*/,"",scmd); scmd=trim(scmd);
        print "START\t" scmd;
        next
      }
      END{ flush_pane() }
    ' "$cfg" | while IFS=$(printf '\t') read -r kind f1 f2 f3; do
        case "$kind" in
          WIN)
            window_name="$f1"; window_dir="$f2"; window_env="$f3"; window_layout=""
            if [ -z "${window_index:-}" ]; then window_index=0; else window_index=$((window_index+1)); fi
            if [ "$window_index" -eq 0 ]; then
                [ -n "$window_name" ] && tmux rename-window -t "$session:0" "$window_name" 2>/dev/null || true
            else
                base_dir="${window_dir:-$wt}"
                tmux new-window -t "$session" -n "${window_name:-win$window_index}" -c "$base_dir" 2>/dev/null || true
            fi
            current_pane_index=0
            ;;
          WATTR)
            case "$f1" in
              DIR) window_dir="$f2" ;;
              ENV) if [ -n "$window_env" ]; then window_env="$window_env;$f2"; else window_env="$f2"; fi ;;
              LAYOUT) window_layout="$f2" ;;
            esac
            ;;
          PANE)
            pane_cmd="$f1"; pane_split="$f2"; pane_dir="$f3"; pane_env="$4" # $4 may be empty
            # Determine dir and env
            run_dir="${pane_dir:-$window_dir}"
            [ -z "$run_dir" ] && run_dir="$wt"
            prefix=""
            # Compose env prefix as exports to persist in shell
            if [ -n "$window_env" ]; then
                IFS=';' ; for kv in $window_env; do unset IFS; [ -n "$kv" ] && prefix="$prefix export $kv;"; done
            fi
            if [ -n "$pane_env" ]; then
                IFS=';' ; for kv in $pane_env; do unset IFS; [ -n "$kv" ] && prefix="$prefix export $kv;"; done
            fi
            if [ "$current_pane_index" -eq 0 ]; then
                tmux select-window -t "$session:$window_index" 2>/dev/null || true
                tmux send-keys -t "$session:$window_index.0" "$prefix $pane_cmd" Enter 2>/dev/null || true
            else
                split_flag="-h"
                [ "$pane_split" = "v" ] && split_flag="-v"
                tmux split-window -t "$session:$window_index" $split_flag -c "$run_dir" 2>/dev/null || true
                tmux send-keys -t "$session:$window_index" "$prefix $pane_cmd" Enter 2>/dev/null || true
            fi
            current_pane_index=$((current_pane_index+1))
            tmux select-layout -t "$session:$window_index" tiled 2>/dev/null || true
            if [ -n "$window_layout" ]; then
                tmux select-layout -t "$session:$window_index" "$window_layout" 2>/dev/null || true
            fi
            ;;
          START)
            tmux send-keys -t "$session" "$f1" Enter 2>/dev/null || true
            ;;
        esac
      done
    return 0
}
