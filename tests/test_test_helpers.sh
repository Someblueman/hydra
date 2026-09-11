#!/bin/sh
# Exercise batching across PATH entries and concurrent-runner failure reporting.
set -eu
root=$(CDPATH='' cd -- "$(dirname "$0")/.." && pwd)
fixture=$(mktemp -d)
fixture=$(CDPATH='' cd -- "$fixture" && pwd -P)
trap 'rm -rf "$fixture"' 0
trap 'exit 130' INT
trap 'exit 143' TERM
mkdir -p "$fixture/first path" "$fixture/second" "$fixture/tools"
for tool in mkdir ln; do
    ln -s "$(command -v "$tool")" "$fixture/tools/$tool"
done
i=0
while [ "$i" -lt 205 ]; do
    printf '#!/bin/sh\nexit 0\n' > "$fixture/first path/tool-$i"
    i=$((i + 1))
done
for name in duplicate 'space name' .hidden tmux; do
    printf '#!/bin/sh\nexit 0\n' > "$fixture/first path/$name"
done
chmod +x "$fixture/first path/"* "$fixture/first path/.hidden"
cp "$fixture/first path/duplicate" "$fixture/second/duplicate"
ln -s "$fixture/first path/duplicate" "$fixture/first path/alias"
touch "$fixture/first path/not-executable"
mkdir "$fixture/first path/directory"
(
    # shellcheck source=/dev/null
    . "$root/tests/headless_path.sh"
    PATH="$fixture/first path:$fixture/second:$fixture/tools"
    headless_path "$fixture/result"
    [ "$(command -v duplicate)" = "$fixture/result/duplicate" ]
    ! command -v tmux >/dev/null 2>&1
)
[ "$(readlink "$fixture/result/duplicate")" = "$fixture/first path/duplicate" ]
for name in tool-0 tool-99 tool-100 tool-199 tool-204 'space name' .hidden alias; do
    [ -x "$fixture/result/$name" ]
done
[ ! -e "$fixture/result/tmux" ]
[ ! -e "$fixture/result/not-executable" ]
[ ! -e "$fixture/result/directory" ]
sh "$root/scripts/run-test.sh" "$fixture/logs" success sh -c 'echo successful' > "$fixture/pass"
grep -q '^PASS success ' "$fixture/pass"
grep -qx successful "$fixture/logs/success.log"
code=0
sh "$root/scripts/run-test.sh" "$fixture/logs" failure sh -c 'echo deliberate failure >&2; exit 7' > "$fixture/fail" 2>&1 || code=$?
[ "$code" -eq 7 ]
grep -q '^FAIL failure ' "$fixture/fail"
grep -qx 'deliberate failure' "$fixture/logs/failure.log"
grep -qx 'deliberate failure' "$fixture/fail"
cat > "$fixture/dry.mk" <<'MAKEFILE'
parent:
	+@sh "$(TEST_RUNNER)" "$(TEST_LOGS)" dry $(MAKE) -f "$(TEST_MAKEFILE)" child
child:
	@echo real-child
MAKEFILE
make -n -f "$fixture/dry.mk" TEST_RUNNER="$root/scripts/run-test.sh" \
    TEST_LOGS="$fixture/logs" TEST_MAKEFILE="$fixture/dry.mk" parent > "$fixture/dry.out"
[ ! -f "$fixture/logs/dry.log" ]
grep -q 'echo real-child' "$fixture/dry.out"
if grep -q '^PASS' "$fixture/dry.out"; then exit 1; fi
printf 'PASS test helpers\n'
