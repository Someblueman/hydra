#!/bin/sh
# Strict reader for Hydra static workflow schema version 1.

workflow_repo_root() {
    git rev-parse --show-toplevel 2>/dev/null
}

workflow_dir() {
    _wd_root="$(workflow_repo_root)" || {
        cli_error workflow not_in_repository "workflow IDs require a Git repository" "pass an explicit workflow path"
        return 1
    }
    printf '%s/.hydra/workflows\n' "$_wd_root"
}

workflow_resolve() {
    _wr_ref="$1"
    case "$_wr_ref" in
        */*|*.yml|*.yaml)
            _wr_file="$_wr_ref"
            ;;
        *)
            case "$_wr_ref" in ''|*[!a-z0-9_-]*|[!a-z]*|*__*|*--*)
                cli_error workflow invalid_reference "invalid workflow ID '$_wr_ref'" "use lowercase letters, digits, single hyphens or underscores"
                return 1
            esac
            _wr_dir="$(workflow_dir)" || return 1
            if [ -f "$_wr_dir/$_wr_ref.yml" ]; then
                _wr_file="$_wr_dir/$_wr_ref.yml"
            else
                _wr_file="$_wr_dir/$_wr_ref.yaml"
            fi
            ;;
    esac
    [ -f "$_wr_file" ] || { cli_error workflow definition_not_found "workflow definition not found: $_wr_file" "pass a file path or add .hydra/workflows/<id>.yml"; return 1; }
    _wr_parent="$(cd "$(dirname "$_wr_file")" 2>/dev/null && pwd)" || return 1
    printf '%s/%s\n' "$_wr_parent" "$(basename "$_wr_file")"
}

workflow_require_trust() {
    _wrt_file="$1"
    _wrt_root="$(workflow_repo_root 2>/dev/null || true)"
    [ -n "$_wrt_root" ] || return 0
    case "$_wrt_file" in
        "$_wrt_root"/.hydra/*)
            if ! project_is_trusted "$_wrt_root"; then
                cli_error workflow trust_required "repository workflow is not trusted or changed: $_wrt_file" "review .hydra and run hydra init --trust"
                return 1
            fi
            ;;
    esac
}

workflow_list() {
    _wl_dir="$(workflow_dir)" || return 1
    [ -d "$_wl_dir" ] || return 0
    find "$_wl_dir" -type f \( -name '*.yml' -o -name '*.yaml' \) -maxdepth 1 2>/dev/null |
        LC_ALL=C sort | while IFS= read -r _wl_file; do
            workflow_require_trust "$_wl_file" || exit 1
            workflow_parse "$_wl_file" identity || exit 1
        done
}

workflow_parse() {
    _wp_file="$1"
    _wp_mode="$2"
    awk -v file="$_wp_file" -v mode="$_wp_mode" '
    function fail(field,msg) { printf "Error: %s: %s: %s\n", file, field, msg > "/dev/stderr"; bad=1; exit 1 }
    function trim(s) { sub(/^[ ]+/,"",s); sub(/[ ]+$/,"",s); return s }
    function scalar(s, field,    q) {
        s=trim(s); if (s=="") fail(field,"value is required")
        if (s ~ /^[&*!]|[ ][&*!][A-Za-z0-9_-]/ || s ~ /[{}]/) fail(field,"unsupported YAML construct")
        q=substr(s,1,1)
        if (q=="\"" || q=="\047") {
            if (substr(s,length(s),1)!=q || length(s)<2) fail(field,"unterminated quoted scalar")
            s=substr(s,2,length(s)-2)
            if (q=="\"" && s ~ /\\/) fail(field,"escape sequences are unsupported")
        }
        return s
    }
    function valid_id(s) { return s ~ /^[a-z][a-z0-9]*([-_][a-z0-9]+)*$/ && length(s)<=64 }
    function number(s, field, lo, hi) {
        if (s !~ /^[0-9]+$/ || s+0<lo || s+0>hi) fail(field,"must be an integer from " lo " to " hi)
        return s+0
    }
    function boolean(s, field) { if (s!="true" && s!="false") fail(field,"must be true or false"); return s }
    function list(s, field,    inner,n,a,i,out,v) {
        s=trim(s); if (s !~ /^\[.*\]$/) fail(field,"must be an inline list")
        inner=trim(substr(s,2,length(s)-2)); if (inner=="") return ""
        n=split(inner,a,","); out=""
        for(i=1;i<=n;i++){ v=scalar(a[i],field); if(!valid_id(v)) fail(field,"contains invalid ID " v); out=out (out?",":"") v }
        return out
    }
    function value_list(s, field,    inner,n,a,i,out,v) {
        s=trim(s); if (s !~ /^\[.*\]$/) fail(field,"must be an inline list")
        inner=trim(substr(s,2,length(s)-2)); if (inner=="") fail(field,"must not be empty")
        n=split(inner,a,","); out=""
        for(i=1;i<=n;i++){ v=scalar(a[i],field); if(v=="")fail(field,"contains an empty value"); out=out (out?",":"") v }
        return out
    }
    function set_top(k,v) {
        if (seen_top[k]++) fail(k,"duplicate field")
        if (k=="version") version=number(v,k,1,1)
        else if(k=="id"){ id=scalar(v,k); if(!valid_id(id)) fail(k,"invalid workflow ID") }
        else if(k=="description") description=scalar(v,k)
        else if(k=="parallelism") parallelism=number(v,k,1,16)
        else fail(k,"unknown top-level key")
    }
    function set_resource(k,v, field) {
        field="resources." k; if(seen_resource[k]++) fail(field,"duplicate field")
        if(k=="disk_mb") disk=number(v,field,1,1048576)
        else if(k=="max_heads") maxheads=number(v,field,1,64)
        else fail(field,"unknown resource key")
    }
    function set_step(k,v, field) {
        field="steps[" step "]." k; if(seen_step[step,k]++) fail(field,"duplicate field")
        if(k=="id"){ sid[step]=scalar(v,field); if(!valid_id(sid[step])) fail(field,"invalid step ID") }
        else if(k=="kind") kind[step]=scalar(v,field)
        else if(k=="needs") needs[step]=list(v,field)
        else if(k=="retry") retry[step]=number(v,field,0,10)
        else if(k=="idempotent") idem[step]=boolean(scalar(v,field),field)
        else fail(field,"unknown step key")
    }
    function set_arg(k,v, field) {
        field="steps[" step "].args." k; if(seen_arg[step,k]++) fail(field,"duplicate field")
        if(k=="argv") argv[step]=value_list(v,field)
        else if(k=="timeout") arg[step,k]=number(v,field,1,86400)
        else if(k=="force" || k=="allow_shell") arg[step,k]=boolean(scalar(v,field),field)
        else if(k ~ /^(head|branch|group|profile|command|message|name|by|reason|completion_policy)$/) {
            if(k=="command" && trim(v) ~ /^\[/) fail(field,"use argv for a command list")
            arg[step,k]=scalar(v,field)
        }
        else fail(field,"unknown argument key")
    }
    function req(i,k) { if(arg[i,k]=="") fail("steps[" sid[i] "].args." k,"required for " kind[i]) }
    function edge(i,j,    a,n,x) { n=split(needs[i],a,","); for(x=1;x<=n;x++) if(a[x]==sid[j]) return 1; return 0 }
    function visit(i,    j) {
        if(mark[i]==1) fail("steps[" sid[i] "].needs","dependency cycle")
        if(mark[i]==2) return
        mark[i]=1; for(j=1;j<=step;j++) if(edge(i,j)) visit(j); mark[i]=2; order[++on]=i
    }
    function args_text(i,    k,out,sep) {
        out=""; sep=""
        for(k=1;k<=argc;k++) if(arg[i,argkeys[k]]!=""){out=out sep argkeys[k] "=" arg[i,argkeys[k]];sep=", "}
        if(argv[i]!=""){out=out sep "argv=[" argv[i] "]"}
        return out
    }
    BEGIN { parallelism=1; disk=10240; maxheads=16; argc=11; split("head branch group profile command message name by reason completion_policy timeout",argkeys," ") }
    {
        sub(/\r$/,""); raw=$0
        if(raw ~ /\t/) fail("line " NR,"tabs are unsupported")
        line=raw; sub(/[ ]+#.*$/, "", line); if(line ~ /^[ ]*#/ || line ~ /^[ ]*$/) next
        match(line,/^ */); ind=RLENGTH; text=substr(line,ind+1)
        if(text ~ /(^|[ :])([&*!][A-Za-z_]|<<:)/ || text ~ /[{}]|^[>|]/) fail("line " NR,"unsupported YAML construct")
        if(ind==0){ ctx=""; if(text=="steps:"){ctx="steps";next} if(text=="resources:"){ctx="resources";next}
            if(text !~ /^[a-z_]+:[ ]*/) fail("line " NR,"expected top-level key"); k=text;sub(/:.*/,"",k);v=text;sub(/^[^:]*:[ ]*/,"",v);set_top(k,v);next }
        if(ctx=="resources" && ind==2 && text ~ /^[a-z_]+:/){k=text;sub(/:.*/,"",k);v=text;sub(/^[^:]*:[ ]*/,"",v);set_resource(k,v);next}
        if(ctx=="steps" && ind==2 && text ~ /^-[ ]+[a-z_]+:/){step++; delete dummy; k=text;sub(/^[ ]*-[ ]*/,"",k);v=k;sub(/^[^:]*:[ ]*/,"",v);sub(/:.*/,"",k);set_step(k,v);next}
        if(ctx=="steps" && step && ind==4 && text=="args:"){inargs=1;next}
        if(ctx=="steps" && step && ind==4 && text ~ /^[a-z_]+:/){inargs=0;k=text;sub(/:.*/,"",k);v=text;sub(/^[^:]*:[ ]*/,"",v);set_step(k,v);next}
        if(ctx=="steps" && step && inargs && ind==6 && text ~ /^[a-z_]+:/){k=text;sub(/:.*/,"",k);v=text;sub(/^[^:]*:[ ]*/,"",v);set_arg(k,v);next}
        fail("line " NR,"unsupported indentation or YAML syntax")
    }
    END {
        if(bad) exit 1
        if(!seen_top["version"]) fail("version","required")
        if(!seen_top["id"]) fail("id","required")
        if(step<1) fail("steps","at least one step is required")
        for(i=1;i<=step;i++){
            if(sid[i]=="") fail("steps[" i "].id","required"); if(ids[sid[i]]++) fail("steps[" sid[i] "].id","duplicate ID")
            if(kind[i] !~ /^(spawn|wait|exec|message|gate|approve|kill)$/) fail("steps[" sid[i] "].kind","unsupported step kind " kind[i])
            if(retry[i]=="") retry[i]=0; if(idem[i]=="") idem[i]="true"
            if(retry[i]>0 && idem[i]!="true") fail("steps[" sid[i] "].retry","non-idempotent steps cannot retry")
            if(kind[i]=="spawn") req(i,"branch"); else if(kind[i]=="wait") req(i,"head"); else if(kind[i]=="message"){req(i,"head");req(i,"message")}
            else if(kind[i]=="gate"){req(i,"head");req(i,"name"); if(arg[i,"command"]=="" && argv[i]=="") fail("steps[" sid[i] "].args.command","command or argv is required")}
            else if(kind[i]=="approve"){req(i,"head");req(i,"name");req(i,"by")}
            else if(kind[i]=="kill") req(i,"head")
            else if(kind[i]=="exec" && arg[i,"command"]=="" && argv[i]=="") fail("steps[" sid[i] "].args.command","command or argv is required")
            if(arg[i,"command"]!="" && argv[i]!="") fail("steps[" sid[i] "].args","command and argv are ambiguous")
        }
        for(i=1;i<=step;i++){n=split(needs[i],a,",");for(j=1;j<=n && a[j]!="";j++){if(a[j]==sid[i])fail("steps[" sid[i] "].needs","self-dependency");if(!ids[a[j]])fail("steps[" sid[i] "].needs","missing dependency " a[j])}}
        for(i=1;i<=step;i++) visit(i)
        if(mode=="validate") exit 0
        if(mode=="identity"){print id;exit 0}
        if(mode=="normalized"){
            print "version: 1"; print "id: " id; if(description!="") print "description: " description; print "parallelism: " parallelism
            print "resources:"; print "  disk_mb: " disk; print "  max_heads: " maxheads; print "steps:"
            for(x=1;x<=on;x++){i=order[x];print "  - id: " sid[i];print "    kind: " kind[i];printf "    needs: [%s]\n",needs[i];print "    retry: " retry[i];print "    idempotent: " idem[i];print "    args:";for(k=1;k<=argc;k++)if(arg[i,argkeys[k]]!="")print "      " argkeys[k] ": " arg[i,argkeys[k]];if(argv[i]!="")print "      argv: [" argv[i] "]"}
            exit 0
        }
        if(mode=="runtime"){
            printf "workflow\t%s\t%d\t%d\t%d\n",id,parallelism,disk,maxheads
            for(x=1;x<=on;x++){
                i=order[x]
                printf "step\t%s\t%s\t%s\t%d\t%s",sid[i],kind[i],(needs[i]==""?"-":needs[i]),retry[i],idem[i]
                for(k=1;k<=argc;k++) printf "\t%s",(arg[i,argkeys[k]]==""?"-":arg[i,argkeys[k]])
                printf "\t%s\n",(argv[i]==""?"-":argv[i])
            }
            exit 0
        }
        print "Workflow " id " (schema 1)"; print "Limits: parallelism=" parallelism ", disk_mb=" disk ", max_heads=" maxheads
        for(x=1;x<=on;x++){i=order[x];dep=needs[i];if(dep=="")dep="root";printf "%d. %s [%s] after %s; retry=%d; idempotent=%s",x,sid[i],kind[i],dep,retry[i],idem[i];at=args_text(i);if(at!="")printf "; %s",at;if(idem[i]=="false")printf "; NON-IDEMPOTENT BOUNDARY";print ""}
        for(i=1;i<=step;i++){n=split(needs[i],a,",");if(n>1)print "Join: " sid[i] " waits for " needs[i]}
        for(i=1;i<=step;i++){fans="";count=0;for(j=1;j<=step;j++)if(edge(j,i)){fans=fans (fans?",":"") sid[j];count++}if(count>1)print "Fan-out: " sid[i] " -> " fans}
        print "No commands will be executed; Git, tmux, worktrees, and run state are unchanged."
    }' "$_wp_file"
}

workflow_runs_dir() {
    _wrd_project="$(hydra_get_project_id)" || {
        cli_error workflow not_initialized "Hydra project identity is unavailable" "run hydra init"
        return 1
    }
    _wrd_project_dir="$(state_v2_project_dir "$_wrd_project")" || return 1
    printf '%s/workflows/runs\n' "$_wrd_project_dir"
}

workflow_atomic_scalar() {
    mkdir -p "$(dirname "$1")" || return 1
    state_v2_write_scalar "$1" "$2"
}

workflow_event() {
    _we_dir="$1" _we_step="$2" _we_type="$3" _we_detail="${4:-}"
    _we_file="$_we_dir/events.jsonl"
    _we_seq="$(awk 'END { print NR + 1 }' "$_we_file" 2>/dev/null || printf 1)"
    _we_now="$(date -u +%Y-%m-%dT%H:%M:%SZ)"
    printf '{"schema_version":1,"sequence":%s,"occurred_at":"%s","run_id":"%s","step_id":%s,"type":"%s","detail":"%s"}\n' \
        "$_we_seq" "$_we_now" "$(json_escape "$(sed -n '1p' "$_we_dir/run-id")")" \
        "$(if [ -n "$_we_step" ]; then printf '"%s"' "$(json_escape "$_we_step")"; else printf null; fi)" \
        "$(json_escape "$_we_type")" "$(json_escape "$_we_detail")" >> "$_we_file"
}

workflow_pid_alive() {
    case "$1" in ''|*[!0-9]*) return 1 ;; esac
    kill -0 "$1" 2>/dev/null
}

workflow_bindings_match() {
    _wbm_dir="$1"
    [ "$(sed -n '1p' "$_wbm_dir/schema-version")" = 1 ] &&
    [ "$(sed -n '1p' "$_wbm_dir/project-id")" = "$(hydra_get_project_id)" ] &&
    [ "$(sed -n '1p' "$_wbm_dir/base-commit")" = "$(git rev-parse HEAD)" ] &&
    [ "$(git hash-object "$_wbm_dir/resolved.yml")" = "$(sed -n '1p' "$_wbm_dir/definition-hash")" ]
}

workflow_step_command() {
    _wsc_kind="$1"; shift
    _wsc_head="$1" _wsc_branch="$2" _wsc_group="$3" _wsc_profile="$4" _wsc_command="$5"
    _wsc_message="$6" _wsc_name="$7" _wsc_by="$8" _wsc_reason="$9"; shift 9
    _wsc_policy="$1" _wsc_timeout="$2" _wsc_force="$3" _wsc_allow="$4" _wsc_argv="$5"
    [ "$_wsc_head" != - ] || _wsc_head=""; [ "$_wsc_branch" != - ] || _wsc_branch=""
    [ "$_wsc_group" != - ] || _wsc_group=""; [ "$_wsc_profile" != - ] || _wsc_profile=""
    [ "$_wsc_command" != - ] || _wsc_command=""; [ "$_wsc_message" != - ] || _wsc_message=""
    [ "$_wsc_name" != - ] || _wsc_name=""; [ "$_wsc_by" != - ] || _wsc_by=""
    [ "$_wsc_reason" != - ] || _wsc_reason=""; [ "$_wsc_policy" != - ] || _wsc_policy=""
    [ "$_wsc_timeout" != - ] || _wsc_timeout=""; [ "$_wsc_force" != - ] || _wsc_force=""
    [ "$_wsc_allow" != - ] || _wsc_allow=""; [ "$_wsc_argv" != - ] || _wsc_argv=""
    case "$_wsc_kind" in
        spawn)
            set -- spawn "$_wsc_branch" --no-agent
            [ -z "$_wsc_group" ] || set -- "$@" --group "$_wsc_group"
            [ -z "$_wsc_profile" ] || set -- "$@" --profile "$_wsc_profile"
            [ -z "$_wsc_policy" ] || set -- "$@" --completion-policy "$_wsc_policy"
            ;;
        wait) set -- wait "$_wsc_head"; [ -z "$_wsc_timeout" ] || set -- "$@" --timeout "$_wsc_timeout" ;;
        message) set -- send "$_wsc_head" "$_wsc_message" ;;
        approve) set -- gate approve "$_wsc_head" "$_wsc_name" --by "$_wsc_by"; [ -z "$_wsc_reason" ] || set -- "$@" --reason "$_wsc_reason" ;;
        kill) set -- kill "$_wsc_head"; [ "$_wsc_force" != true ] || set -- "$@" --force ;;
        exec)
            if [ -n "$_wsc_argv" ]; then
                _wsc_oldifs="$IFS"
                IFS=,
                # shellcheck disable=SC2086
                set -- $_wsc_argv
                IFS="$_wsc_oldifs"
                set -- exec -- "$@"
            else
                set -- exec --shell "$_wsc_command" --allow-shell
            fi
            if [ -n "$_wsc_timeout" ]; then shift; set -- exec --timeout "$_wsc_timeout" "$@"; fi
            if [ -n "$_wsc_head" ]; then shift; set -- exec --branch "$_wsc_head" "$@"; fi
            ;;
        gate)
            set -- gate run "$_wsc_head" "$_wsc_name"
            # shellcheck disable=SC2086
            if [ -n "$_wsc_argv" ]; then _wsc_oldifs="$IFS"; IFS=,; set -- "$@" -- $_wsc_argv; IFS="$_wsc_oldifs"; else set -- "$@" --shell "$_wsc_command"; [ "$_wsc_allow" != true ] || set -- "$@" --allow-shell; fi
            ;;
        *) return 2 ;;
    esac
    "$HYDRA_BIN_PATH" "$@"
}

workflow_refresh_states() {
    _wrs_dir="$1" _wrs_changed=1
    while [ "$_wrs_changed" -eq 1 ]; do
        _wrs_changed=0
        while IFS="$(printf '\t')" read -r _wrs_tag _wrs_id _wrs_kind _wrs_needs _wrs_retry _wrs_idem _wrs_rest; do
            [ "$_wrs_tag" = step ] || continue
            _wrs_sd="$_wrs_dir/steps/$_wrs_id"; _wrs_state="$(sed -n '1p' "$_wrs_sd/state")"
            [ "$_wrs_state" = queued ] || continue
            _wrs_ready=1 _wrs_failed=0
            _wrs_oldifs="$IFS"; IFS=,
            for _wrs_dep in $_wrs_needs; do
                [ "$_wrs_dep" != - ] || continue
                _wrs_ds="$(sed -n '1p' "$_wrs_dir/steps/$_wrs_dep/state")"
                case "$_wrs_ds" in succeeded) ;; failed|cancelled) _wrs_failed=1 ;; *) _wrs_ready=0 ;; esac
            done
            IFS="$_wrs_oldifs"
            if [ "$_wrs_failed" -eq 1 ]; then workflow_atomic_scalar "$_wrs_sd/state" cancelled; workflow_event "$_wrs_dir" "$_wrs_id" step.cancelled dependency_failed; _wrs_changed=1
            elif [ "$_wrs_ready" -eq 1 ]; then workflow_atomic_scalar "$_wrs_sd/state" ready; workflow_event "$_wrs_dir" "$_wrs_id" step.ready; _wrs_changed=1
            fi
        done < "$_wrs_dir/graph.tsv"
    done
}

workflow_drive() {
    _wd_dir="$1"
    workflow_atomic_scalar "$_wd_dir/owner-pid" "$$" || return 1
    workflow_atomic_scalar "$_wd_dir/heartbeat-at" "$(date +%s)" || return 1
    workflow_atomic_scalar "$_wd_dir/state" running || return 1
    workflow_event "$_wd_dir" "" run.running
    while :; do
        workflow_refresh_states "$_wd_dir"
        if [ -f "$_wd_dir/cancel-requested" ]; then
            while IFS= read -r _wd_sd; do [ -n "$_wd_sd" ] || continue; _wd_ss="$(sed -n '1p' "$_wd_sd/state")"; case "$_wd_ss" in queued|ready|retrying) workflow_atomic_scalar "$_wd_sd/state" cancelled; workflow_event "$_wd_dir" "$(basename "$_wd_sd")" step.cancelled request ;; esac; done <<EOF
$(find "$_wd_dir/steps" -mindepth 1 -maxdepth 1 -type d 2>/dev/null | LC_ALL=C sort)
EOF
        fi
        _wd_next="$(awk -F '\t' '$1=="step" {print $2}' "$_wd_dir/graph.tsv" | while IFS= read -r _wd_id; do [ "$(sed -n '1p' "$_wd_dir/steps/$_wd_id/state")" = ready ] && { printf '%s\n' "$_wd_id"; break; }; done)"
        if [ -z "$_wd_next" ]; then
            _wd_nonterminal="$(find "$_wd_dir/steps" -name state -exec sed -n '1p' {} \; | grep -Ec '^(queued|ready|running|retrying)$' || true)"
            [ "$_wd_nonterminal" -gt 0 ] && continue
            if find "$_wd_dir/steps" -name state -exec sed -n '1p' {} \; | grep -q '^failed$'; then _wd_final=failed
            elif [ -f "$_wd_dir/cancel-requested" ]; then _wd_final=cancelled
            else _wd_final=succeeded; fi
            workflow_atomic_scalar "$_wd_dir/state" "$_wd_final"; workflow_event "$_wd_dir" "" "run.$_wd_final"; return "$([ "$_wd_final" = succeeded ] && printf 0 || printf 1)"
        fi
        _wd_line="$(awk -F '\t' -v id="$_wd_next" '$1=="step" && $2==id {print;exit}' "$_wd_dir/graph.tsv")"
        _wd_oldifs="$IFS"
        IFS="$(printf '\t')"
        # shellcheck disable=SC2086
        set -- $_wd_line
        IFS="$_wd_oldifs"
        shift
        _wd_id="$1" _wd_kind="$2" _wd_needs="$3" _wd_retry="$4" _wd_idem="$5"; shift 5
        _wd_sd="$_wd_dir/steps/$_wd_id"; _wd_attempt="$(sed -n '1p' "$_wd_sd/attempts")"; _wd_attempt=$((_wd_attempt + 1))
        workflow_atomic_scalar "$_wd_sd/attempts" "$_wd_attempt"; workflow_atomic_scalar "$_wd_sd/state" running; workflow_atomic_scalar "$_wd_sd/started-at" "$(date +%s)"; workflow_event "$_wd_dir" "$_wd_id" step.running "attempt=$_wd_attempt"
        mkdir -p "$_wd_sd/attempt-$_wd_attempt"
        if workflow_step_command "$_wd_kind" "$@" >"$_wd_sd/attempt-$_wd_attempt/stdout" 2>"$_wd_sd/attempt-$_wd_attempt/stderr"; then _wd_code=0; else _wd_code=$?; fi
        workflow_atomic_scalar "$_wd_sd/attempt-$_wd_attempt/exit-code" "$_wd_code"; workflow_atomic_scalar "$_wd_sd/attempt-$_wd_attempt/completed-at" "$(date +%s)"
        if [ "$_wd_code" -eq 0 ]; then workflow_atomic_scalar "$_wd_sd/state" succeeded; workflow_atomic_scalar "$_wd_sd/authoritative-attempt" "$_wd_attempt"; workflow_event "$_wd_dir" "$_wd_id" step.succeeded "attempt=$_wd_attempt"
        elif [ "$_wd_attempt" -le "$_wd_retry" ] && [ "$_wd_idem" = true ]; then workflow_atomic_scalar "$_wd_sd/state" retrying; workflow_event "$_wd_dir" "$_wd_id" step.retrying "attempt=$_wd_attempt"; workflow_atomic_scalar "$_wd_sd/state" ready
        else workflow_atomic_scalar "$_wd_sd/state" failed; workflow_atomic_scalar "$_wd_sd/authoritative-attempt" "$_wd_attempt"; workflow_event "$_wd_dir" "$_wd_id" step.failed "attempt=$_wd_attempt exit=$_wd_code"; fi
        workflow_atomic_scalar "$_wd_dir/heartbeat-at" "$(date +%s)"
    done
}
