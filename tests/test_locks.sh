#!/bin/sh
# Unit tests for lib/locks.sh
# POSIX-compliant test framework

# Test framework setup
test_count=0
pass_count=0
fail_count=0

# Set up test environment
test_home="$(mktemp -d "${TMPDIR:-/tmp}/hydra-test.XXXXXX")"
HYDRA_HOME="$test_home"
export HYDRA_HOME

# Source the library under test
# shellcheck source=../lib/locks.sh
# shellcheck disable=SC1091
. "$(dirname "$0")/../lib/locks.sh"

# Common test helpers
# shellcheck source=./helpers.sh
# shellcheck disable=SC1091
. "$(dirname "$0")/helpers.sh"

# Cleanup function
cleanup_test_locks() {
    rm -rf "$test_home"
}

# Test try_lock function
test_try_lock() {
    echo "Testing try_lock..."

    # Test acquiring a lock
    try_lock "test_lock_1"
    assert_success $? "try_lock should succeed acquiring new lock"

    # Verify lock directory exists
    if [ -d "$HYDRA_HOME/locks/test_lock_1.lock" ]; then
        echo "[PASS] Lock directory should exist after try_lock"
        pass_count=$((pass_count + 1))
    else
        echo "[FAIL] Lock directory should exist after try_lock"
        fail_count=$((fail_count + 1))
    fi
    test_count=$((test_count + 1))

    assert_equal "$$" "$(sed -n '1p' "$HYDRA_HOME/locks/test_lock_1.lock/pid")" "lock records owner PID"
    assert_equal "test_lock_1" "$(sed -n '1p' "$HYDRA_HOME/locks/test_lock_1.lock/operation")" "lock records operation"

    # Test acquiring same lock fails (already held)
    try_lock "test_lock_1"
    assert_failure $? "try_lock should fail when lock already held"

    # Clean up
    release_lock "test_lock_1"
}

# Test release_lock function
test_release_lock() {
    echo "Testing release_lock..."

    # Acquire a lock first
    try_lock "test_lock_2"
    assert_success $? "Setup: acquire lock"

    # Release the lock
    release_lock "test_lock_2"
    assert_success $? "release_lock should succeed"

    # Verify lock directory is removed
    if [ ! -d "$HYDRA_HOME/locks/test_lock_2.lock" ]; then
        echo "[PASS] Lock directory should be removed after release"
        pass_count=$((pass_count + 1))
    else
        echo "[FAIL] Lock directory should be removed after release"
        fail_count=$((fail_count + 1))
    fi
    test_count=$((test_count + 1))

    # Releasing non-existent lock should be safe
    release_lock "non_existent_lock"
    assert_success $? "release_lock should succeed for non-existent lock"
}

test_acquire_lock_fail_closed() {
    echo "Testing acquire_lock fail-closed..."

    HYDRA_LOCK_RETRIES=1
    export HYDRA_LOCK_RETRIES
    try_lock "held"
    acquire_lock "held"
    assert_failure $? "acquire_lock should fail when lock is held"
    release_lock "held"
    acquire_lock "held"
    assert_success $? "acquire_lock should succeed when lock is free"
    release_lock "held"
    unset HYDRA_LOCK_RETRIES
}

# Writes a lock as try_lock leaves it, for an owner chosen by the test.
# Usage: plant_lock <name> <pid> <host>
plant_lock() {
    mkdir -p "$HYDRA_HOME/locks/$1.lock"
    : > "$HYDRA_HOME/locks/$1.lock/.claim"
    [ -z "$2" ] || printf '%s\n' "$2" > "$HYDRA_HOME/locks/$1.lock/pid"
    [ -z "$3" ] || printf '%s\n' "$3" > "$HYDRA_HOME/locks/$1.lock/host"
    date +%s > "$HYDRA_HOME/locks/$1.lock/created_at"
    printf 'planted\n' > "$HYDRA_HOME/locks/$1.lock/operation"
}

# Names in the lock directory that belong to one lock: the lock itself and
# anything a breaker left beside it.
lock_entries() {
    find "$HYDRA_HOME/locks" -mindepth 1 -maxdepth 1 -name "$1.lock*" -exec basename {} \; | sort | tr '\n' ' '
}

# A holder that was interrupted or killed never releases. Its lock names a
# dead process on this host, and the next acquirer takes it over.
test_acquire_lock_recovers_dead_owner() {
    echo "Testing acquire_lock recovers a lock whose owner died..."

    (exit 0) &
    dead_pid=$!
    wait "$dead_pid"
    HYDRA_LOCK_RETRIES=1
    export HYDRA_LOCK_RETRIES

    plant_lock dead-owner "$dead_pid" "$(hostname)"
    acquire_lock dead-owner "recovery test"
    assert_success $? "acquire_lock takes a lock whose owner is dead on this host"
    assert_equal "$$" "$(sed -n '1p' "$HYDRA_HOME/locks/dead-owner.lock/pid" 2>/dev/null)" "the recovered lock names its new owner"
    assert_equal "recovery test" "$(sed -n '1p' "$HYDRA_HOME/locks/dead-owner.lock/operation" 2>/dev/null)" \
        "acquire_lock records the operation it was given"
    assert_equal "dead-owner.lock " "$(lock_entries dead-owner)" "breaking a lock leaves nothing beside it"
    acquire_lock dead-owner
    assert_failure $? "the recovered lock is held"
    release_lock dead-owner
    assert_equal "" "$(lock_entries dead-owner)" "the recovered lock is released like any other"

    acquire_lock unnamed-operation
    assert_equal "unnamed-operation" "$(sed -n '1p' "$HYDRA_HOME/locks/unnamed-operation.lock/operation" 2>/dev/null)" \
        "acquire_lock records the lock name when no operation is given"
    release_lock unnamed-operation
    unset HYDRA_LOCK_RETRIES
}

# Evidence that the owner is gone is required: a live process, another host,
# or an owner record that cannot be read keeps the lock.
test_acquire_lock_keeps_owned_locks() {
    echo "Testing acquire_lock never breaks a lock that may be owned..."

    (exit 0) &
    dead_pid=$!
    wait "$dead_pid"
    sleep 60 &
    live_pid=$!
    HYDRA_LOCK_RETRIES=2
    export HYDRA_LOCK_RETRIES

    plant_lock live-owner "$live_pid" "$(hostname)"
    acquire_lock live-owner
    assert_failure $? "acquire_lock refuses a lock whose owner is alive"
    remove_stale_lock_dir "$HYDRA_HOME/locks/live-owner.lock"
    assert_failure $? "a lock whose owner is alive is not stale"
    assert_equal "$live_pid" "$(sed -n '1p' "$HYDRA_HOME/locks/live-owner.lock/pid" 2>/dev/null)" "a live owner keeps its lock"

    plant_lock foreign-owner "$dead_pid" different-host
    acquire_lock foreign-owner
    assert_failure $? "acquire_lock refuses a lock owned on another host"
    assert_equal "$dead_pid" "$(sed -n '1p' "$HYDRA_HOME/locks/foreign-owner.lock/pid" 2>/dev/null)" "another host's lock is kept"
    assert_equal "foreign-owner.lock " "$(lock_entries foreign-owner)" "refusing another host's lock leaves nothing beside it"

    plant_lock no-owner "" "$(hostname)"
    acquire_lock no-owner
    assert_failure $? "acquire_lock refuses a lock without an owner record"
    plant_lock no-host "$dead_pid" ""
    acquire_lock no-host
    assert_failure $? "acquire_lock refuses a lock without a host record"
    if [ -d "$HYDRA_HOME/locks/no-owner.lock" ] && [ -d "$HYDRA_HOME/locks/no-host.lock" ]; then
        assert_success 0 "locks with an incomplete owner record are kept"
    else
        assert_success 1 "locks with an incomplete owner record are kept"
    fi

    # Another live process has announced that it is breaking this lock: it
    # may be about to delete it, so this caller must not.
    plant_lock being-broken "$dead_pid" "$(hostname)"
    : > "$HYDRA_HOME/locks/being-broken.lock.breaking.$live_pid"
    acquire_lock being-broken
    assert_failure $? "acquire_lock leaves a stale lock to the live process that is breaking it"
    assert_equal "$dead_pid" "$(sed -n '1p' "$HYDRA_HOME/locks/being-broken.lock/pid" 2>/dev/null)" "the lock being broken elsewhere is untouched"
    # A breaker that died left its claim behind: it no longer counts.
    mv "$HYDRA_HOME/locks/being-broken.lock.breaking.$live_pid" "$HYDRA_HOME/locks/being-broken.lock.breaking.$dead_pid"
    acquire_lock being-broken
    assert_success $? "a dead breaker's claim does not keep a stale lock held"
    assert_equal "$$" "$(sed -n '1p' "$HYDRA_HOME/locks/being-broken.lock/pid" 2>/dev/null)" "the lock a dead breaker left is taken over"
    release_lock being-broken

    kill "$live_pid" 2>/dev/null
    wait "$live_pid" 2>/dev/null
    rm -rf "$HYDRA_HOME/locks/live-owner.lock" "$HYDRA_HOME/locks/foreign-owner.lock" \
        "$HYDRA_HOME/locks/no-owner.lock" "$HYDRA_HOME/locks/no-host.lock" \
        "$HYDRA_HOME/locks/being-broken.lock.breaking.$dead_pid"
    unset HYDRA_LOCK_RETRIES
}

# Several processes find the same dead owner at once. Each must hold the lock
# alone: a waiter that deleted the lock another had just taken over would let
# two of them into the critical section together.
test_acquire_lock_breaks_once_under_contention() {
    echo "Testing concurrent acquirers of a lock whose owner died..."

    (exit 0) &
    dead_pid=$!
    wait "$dead_pid"
    contend_dir="$test_home/contend"
    mkdir -p "$contend_dir"
    cat > "$contend_dir/contender.sh" <<'CONTENDER'
#!/bin/sh
# contender.sh <locks.sh> <dir>: take the lock, be alone, release.
HYDRA_LOCK_RETRIES=600
. "$1"
if ! acquire_lock contended "contention test"; then
    echo timeout >> "$2/timeouts"
    exit 0
fi
if mkdir "$2/critical" 2>/dev/null; then
    sleep 0.01 2>/dev/null || true
    rmdir "$2/critical"
else
    echo overlap >> "$2/overlaps"
fi
echo acquired >> "$2/acquired"
release_lock contended
CONTENDER
    plant_lock contended "$dead_pid" "$(hostname)"
    contenders=""
    for _ in 1 2 3 4 5 6 7 8; do
        sh "$contend_dir/contender.sh" "$(dirname "$0")/../lib/locks.sh" "$contend_dir" &
        contenders="$contenders $!"
    done
    # shellcheck disable=SC2086 # A list of PIDs.
    wait $contenders
    assert_equal 8 "$(grep -c acquired "$contend_dir/acquired" 2>/dev/null)" "every contender acquired the dead owner's lock in turn"
    assert_equal "" "$(cat "$contend_dir/overlaps" "$contend_dir/timeouts" 2>/dev/null)" "no two contenders held the lock together"
    assert_equal "" "$(lock_entries contended)" "contention leaves no lock, claim or leftover behind"
}

# Test cleanup_stale_locks function
test_cleanup_stale_locks() {
    echo "Testing cleanup_stale_locks..."

    # Create locks directory
    mkdir -p "$HYDRA_HOME/locks"

    # Create a "stale" lock (we'll test the function runs without error)
    mkdir -p "$HYDRA_HOME/locks/stale_test.lock"

    # Run cleanup (won't actually clean new lock, just tests the function)
    cleanup_stale_locks
    assert_success $? "cleanup_stale_locks should succeed"

    # Clean up
    rmdir "$HYDRA_HOME/locks/stale_test.lock" 2>/dev/null || true
}

test_cleanup_dead_local_lock() {
    echo "Testing evidence-based stale lock cleanup..."

    dead_lock="$HYDRA_HOME/locks/dead-local.lock"
    foreign_lock="$HYDRA_HOME/locks/foreign.lock"
    mkdir -p "$dead_lock" "$foreign_lock"
    printf '99999999\n' > "$dead_lock/pid"
    hostname > "$dead_lock/host"
    printf '99999999\n' > "$foreign_lock/pid"
    printf 'different-host\n' > "$foreign_lock/host"

    cleanup_stale_locks
    if [ ! -d "$dead_lock" ]; then
        assert_success 0 "dead same-host lock is removed"
    else
        assert_success 1 "dead same-host lock is removed"
    fi
    if [ -d "$foreign_lock" ]; then
        assert_success 0 "foreign-host lock is preserved"
    else
        assert_success 1 "foreign-host lock is preserved"
    fi
    rm -rf "$foreign_lock"
}

# Test release_session_lock function
test_release_session_lock() {
    echo "Testing release_session_lock..."

    # Acquire a session lock
    try_lock "test_session"
    assert_success $? "Setup: acquire session lock"

    # Release using release_session_lock
    release_session_lock "test_session"
    assert_success $? "release_session_lock should succeed"

    # Verify lock is released
    if [ ! -d "$HYDRA_HOME/locks/test_session.lock" ]; then
        echo "[PASS] Session lock should be released"
        pass_count=$((pass_count + 1))
    else
        echo "[FAIL] Session lock should be released"
        fail_count=$((fail_count + 1))
    fi
    test_count=$((test_count + 1))
}

# Test lock behavior without HYDRA_HOME
test_lock_without_home() {
    echo "Testing lock behavior without HYDRA_HOME..."

    saved_home="$HYDRA_HOME"
    unset HYDRA_HOME

    # try_lock should succeed (no-op) when HYDRA_HOME not set
    try_lock "no_home_lock"
    assert_success $? "try_lock should succeed when HYDRA_HOME not set"

    # cleanup_stale_locks should succeed when HYDRA_HOME not set
    cleanup_stale_locks
    assert_success $? "cleanup_stale_locks should succeed when HYDRA_HOME not set"

    HYDRA_HOME="$saved_home"
    export HYDRA_HOME
}

# Test private directory creation with portable chmod/mkdir arguments
test_private_mkdir() {
    echo "Testing hydra_private_mkdir..."

    (umask 022; mkdir "$test_home/kept")
    hydra_private_mkdir "$test_home/kept/a/b" "$test_home/c"
    assert_success $? "hydra_private_mkdir creates nested directories"
    assert_equal "" "$(find "$test_home/kept/a" "$test_home/c" ! -perm 700 -print)" \
        "every directory it creates is mode 700"
    assert_equal "" "$(find "$test_home/kept" -prune ! -perm 755 -print)" "an existing parent keeps its mode"

    # A relative name that starts with "-" (macOS chmod has no "--").
    (cd "$test_home" && hydra_private_mkdir -dash/inner)
    assert_success $? "hydra_private_mkdir creates a relative name that starts with a dash"
    assert_equal "" "$(find "$test_home/-dash" ! -perm 700 -print)" "the dash-named directories are mode 700"

    : > "$test_home/plain-file"
    hydra_private_mkdir "$test_home/plain-file" 2>/dev/null
    assert_failure $? "hydra_private_mkdir refuses a path that is a file"
}

# A held lock is refused even where mkdir reports success for a directory
# that already exists, as the Rust coreutils on Ubuntu 26.04 do when another
# process creates it between their check and mkdir(2). The fake mkdir below
# always behaves that way; only the claim can tell the callers apart.
test_lock_with_lenient_mkdir() {
    echo "Testing locks where mkdir succeeds for an existing directory..."
    lenient_bin="$test_home/lenient-bin"
    real_mkdir="$(command -v mkdir)"
    hydra_private_mkdir "$lenient_bin"
    lenient_path="$lenient_bin:$PATH"
    cat > "$lenient_bin/mkdir" <<LENIENT
#!/bin/sh
"$real_mkdir" "\$@" 2>/dev/null && exit 0
for dir do last="\$dir"; done
[ -d "\$last" ]
LENIENT
    chmod +x "$lenient_bin/mkdir"
    (
        PATH="$lenient_path"
        mkdir "$test_home/lenient-probe" && mkdir "$test_home/lenient-probe"
    )
    assert_success $? "the fake mkdir reports success for an existing directory"
    (
        PATH="$lenient_path"
        try_lock lenient_lock || exit 1
        (try_lock lenient_lock) && exit 2
        [ "$(sed -n '1p' "$HYDRA_HOME/locks/lenient_lock.lock/pid")" = "$$" ] || exit 3
        release_lock lenient_lock
        [ ! -e "$HYDRA_HOME/locks/lenient_lock.lock" ] || exit 4
        hydra_exclusive_mkdir "$test_home/claimed" || exit 5
        hydra_exclusive_mkdir "$test_home/claimed" && exit 6
        exit 0
    )
    assert_success $? "a held lock and a claimed directory are refused, and release removes the claim"
}

# Run all tests
echo "Running locks.sh unit tests..."
echo "================================"

test_try_lock
test_release_lock
test_acquire_lock_fail_closed
test_acquire_lock_recovers_dead_owner
test_acquire_lock_keeps_owned_locks
test_acquire_lock_breaks_once_under_contention
test_cleanup_stale_locks
test_cleanup_dead_local_lock
test_release_session_lock
test_lock_without_home
test_private_mkdir
test_lock_with_lenient_mkdir

# Clean up
cleanup_test_locks

echo "================================"
echo "Test Results:"
echo "Total:  $test_count"
echo "Passed: $pass_count"
echo "Failed: $fail_count"

if [ "$fail_count" -eq 0 ]; then
    echo "All tests passed!"
    exit 0
else
    echo "Some tests failed!"
    exit 1
fi
