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
    BEGIN { parallelism=1; disk=10240; maxheads=16; argc=13; split("head branch group profile command message name by reason completion_policy timeout force allow_shell",argkeys," ") }
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
            if(retry[i]=="") retry[i]=0
            if(idem[i]=="") fail("steps[" sid[i] "].idempotent","required")
            if(retry[i]>0 && idem[i]!="true") fail("steps[" sid[i] "].retry","non-idempotent steps cannot retry")
            if(kind[i]=="spawn"){req(i,"branch"); spawn_count++} else if(kind[i]=="wait") req(i,"head"); else if(kind[i]=="message"){req(i,"head");req(i,"message")}
            else if(kind[i]=="gate"){req(i,"head");req(i,"name"); if(arg[i,"command"]=="" && argv[i]=="") fail("steps[" sid[i] "].args.command","command or argv is required")}
            else if(kind[i]=="approve"){req(i,"head");req(i,"name");req(i,"by")}
            else if(kind[i]=="kill") req(i,"head")
            else if(kind[i]=="exec"){req(i,"head"); if(arg[i,"command"]=="" && argv[i]=="") fail("steps[" sid[i] "].args.command","command or argv is required")}
            if(arg[i,"command"]!="" && argv[i]!="") fail("steps[" sid[i] "].args","command and argv are ambiguous")
            if(arg[i,"command"]!="" && arg[i,"allow_shell"]!="true") fail("steps[" sid[i] "].args.allow_shell","must be true for command strings")
        }
        if(spawn_count>maxheads) fail("resources.max_heads","is smaller than the number of spawn steps")
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


