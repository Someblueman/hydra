#!/bin/sh
# Sourced by test_agent_execution.sh. Real public supervision, synthetic providers.
# shellcheck disable=SC2154
mkdir "$fixture/builtin-bin"
cat > "$fixture/builtin-bin/provider" <<'PROVIDER'
#!/bin/sh
set -eu
provider="${0##*/}"
case " $* " in
    *' --help '*|*' --version '*) printf '%s\n' 'fixture-1 --print --conversation --resume --session --format --output-format --disable-slash-commands stream-json'; exit 0 ;;
esac
mode=start
session=fixture-session
prompt=''
while [ "$#" -gt 0 ]; do
    case "$1" in
        run) [ "$provider" = opencode ]; shift ;;
        --format|--output-format) case "$2" in json|stream-json) ;; *) exit 64 ;; esac; shift 2 ;;
        --disable-slash-commands) [ "$provider" = agy ]; shift ;;
        --conversation|--resume|--session) mode=resume; session="$2"; shift 2 ;;
        --print) if [ "$provider" = agy ]; then prompt="$2"; shift 2; else shift; fi ;;
        --) shift; [ "$#" -eq 1 ]; prompt="$1"; shift ;;
        *) exit 64 ;;
    esac
done
[ -n "$prompt" ]
if [ "$mode" = resume ]; then [ "$session" = "$(cat "$provider.session")" ]; else printf %s "$session" > "$provider.session"; fi
printf %s "$prompt" > "$provider.prompt"
printf x >> "$provider.starts"
case "$provider" in cursor-agent) adapter=cursor ;; *) adapter="$provider" ;; esac
case "$prompt" in
    partial-provider) printf '{'; exit 0 ;;
    failed-provider)
        case "$adapter" in
            agy) printf '%s\n' '{"event":"result","result":{"status":"WAITING"}}' ;;
            cursor) printf '%s\n' '{"type":"result","is_error":true}' ;;
            opencode) printf '%s\n' '{"type":"error"}' ;;
        esac
        exit 0 ;;
esac
cat "$HYDRA_PROVIDER_FIXTURES/$adapter-jsonl.jsonl"
PROVIDER
chmod +x "$fixture/builtin-bin/provider"
builtin_path="$PATH"
PATH="$fixture/builtin-bin:$PATH"
HYDRA_PROVIDER_FIXTURES="$root/tests/fixtures/agents"
export PATH HYDRA_PROVIDER_FIXTURES
for builtin in agy cursor opencode; do
    case "$builtin" in cursor) executable=cursor-agent ;; *) executable="$builtin" ;; esac
    ln -s provider "$fixture/builtin-bin/$executable"
    # An option-shaped prompt stays data through each provider's literal recipe.
    # shellcheck disable=SC2016
    printf '%s\n%s' '--resume; $(touch injected)' 'quoted "prompt"' > "$fixture/builtin-prompt"
    hydra exec --branch agent-fixture --profile "$builtin" --prompt-file "$fixture/builtin-prompt" \
        --require prompt,observations,resume --result-file "$builtin-answer.txt" --exit-code --json > "$fixture/builtin-run"
    [ "$(cat "$worker/$builtin-answer.txt")" = 'fixture artifact' ]
    cmp "$fixture/builtin-prompt" "$worker/$executable.prompt"
    [ ! -e "$worker/injected" ]
    builtin_run="$(sed -n 's/.*"run_id":"\([^"]*\)".*/\1/p' "$fixture/builtin-run")"
    hydra exec --branch agent-fixture --profile "$builtin" --prompt-file "$fixture/builtin-prompt" \
        --resume-run "$builtin_run" --result-file "$builtin-resumed.txt" --exit-code --json > "$fixture/builtin-resumed"
    cmp "$worker/$builtin-answer.txt" "$worker/$builtin-resumed.txt"
    grep -q 'fixture-session' "$fixture/builtin-resumed"
    if [ "$builtin" = cursor ]; then
        starts="$(cat "$worker/$executable.starts")"
        if hydra exec --branch agent-fixture --profile cursor --prompt-file "$fixture/builtin-prompt" --require usage > "$fixture/no-cursor-usage" 2>&1; then exit 1; fi
        [ "$(cat "$worker/$executable.starts")" = "$starts" ]
    fi
    for boundary in partial-provider failed-provider; do
        printf %s "$boundary" > "$fixture/builtin-prompt"
        if hydra exec --branch agent-fixture --profile "$builtin" --prompt-file "$fixture/builtin-prompt" --exit-code --json > "$fixture/builtin-error"; then exit 1; else code=$?; fi
        if [ "$boundary" = partial-provider ]; then [ "$code" -eq 125 ]; else [ "$code" -eq 1 ]; fi
    done
done
PATH="$builtin_path"
export PATH
printf 'Builtin agents: literal prompts, exact recorded resume, unknown Cursor usage, partial events and provider failures passed\n'
