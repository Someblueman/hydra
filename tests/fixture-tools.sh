#!/bin/sh
# Test-only native helpers; no helper is installed with the shell CLI.
fixture_json() {
    _fj_root=$(CDPATH='' cd -- "${HYDRA_TEST_ROOT:-${root:?}}" && pwd)
    _fj_build=${BUILD_DIR:-$_fj_root/build}
    case $_fj_build in /*) ;; *) _fj_build="$_fj_root/$_fj_build" ;; esac
    _fj_binary="$_fj_build/native-tests/fixture-json"
    # A test invocation resolves this once; Make tracks every source dependency.
    if [ "${_fj_ready:-}" != "$_fj_binary" ]; then
        make -s -C "$_fj_root" BUILD_DIR="$_fj_build" build-test-fixture >&2 || return 1
        _fj_ready=$_fj_binary
    fi
    "$_fj_binary" "$@"
}

statistics_evidence() {
    _se_root=$(CDPATH='' cd -- "$(dirname "$1")/.." && pwd)
    _se_build=${BUILD_DIR:-$_se_root/build}
    case $_se_build in /*) ;; *) _se_build="$_se_root/$_se_build" ;; esac
    _se_binary="$_se_build/native-tests/statistics-evidence"
    if [ "${_se_ready:-}" != "$_se_binary" ]; then
        make -s -C "$_se_root" BUILD_DIR="$_se_build" "$_se_binary" >&2 || return 1
        _se_ready=$_se_binary
    fi
    "$_se_binary" "$@"
}

fixture_lock() {
    _fl_root=$(CDPATH='' cd -- "${HYDRA_TEST_ROOT:-${root:?}}" && pwd)
    _fl_build=${BUILD_DIR:-$_fl_root/build}
    case $_fl_build in /*) ;; *) _fl_build="$_fl_root/$_fl_build" ;; esac
    _fl_binary="$_fl_build/fixture-lock"
    if [ ! -x "$_fl_binary" ] || [ -n "$(find "$_fl_root/tests/fixture/lock.c" -newer "$_fl_binary" -print)" ]; then
        mkdir -p "$_fl_build"
        _fl_temp=$(mktemp "$_fl_build/fixture-lock.XXXXXX") || return 1
        if "${CC:-cc}" -std=c99 -Wall -Wextra -Werror -pedantic "$_fl_root/tests/fixture/lock.c" -o "$_fl_temp"; then
            mv "$_fl_temp" "$_fl_binary"
        else
            rm -f "$_fl_temp"
            return 1
        fi
    fi
    "$_fl_binary" "$@"
}
