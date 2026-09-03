#!/bin/sh
# POSIX-compliant installation script
# Installs to $PREFIX/bin/hydra and $PREFIX/lib/hydra (default PREFIX=/usr/local).
# No root required when PREFIX is writable. DESTDIR is optional staging.

set -e

usage() {
    echo "Usage: [PREFIX=/usr/local] [DESTDIR=] ./install.sh" >&2
    echo "  PREFIX    installation prefix (default: /usr/local)" >&2
    echo "  DESTDIR   optional staging directory prepended to PREFIX" >&2
    echo "  HYDRA_INSTALL_CORE  auto (default), never, or required" >&2
    echo "  HYDRA_BUILD_CORE=1  build the optional core from this checkout" >&2
    echo "  HYDRA_CORE_ARTIFACT offline hydra-core path with adjacent metadata" >&2
    echo "  HYDRA_INSTALL_TUI   auto-build when possible (default), never, or required" >&2
    echo "  HYDRA_BUILD_TUI=1   require building the native TUI from this checkout" >&2
    echo "  HYDRA_TUI_ARTIFACT  offline hydra-tui path with adjacent metadata" >&2
    echo "" >&2
    echo "Non-root example:" >&2
    echo "  PREFIX=\$HOME/.local ./install.sh" >&2
    echo "See README Quick Start." >&2
}

if [ "${1:-}" = "-h" ] || [ "${1:-}" = "--help" ]; then
    usage
    exit 0
fi

if [ $# -gt 0 ]; then
    echo "Error: Unexpected argument '$1'" >&2
    echo "Next: pass PREFIX and DESTDIR as environment variables, not flags" >&2
    usage
    exit 1
fi

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
SRC_BIN="$SCRIPT_DIR/bin/hydra"
SRC_LIB="$SCRIPT_DIR/lib"
HYDRA_VERSION="$(sed -n 's/^HYDRA_VERSION="\([^"]*\)"$/\1/p' "$SRC_BIN" | sed -n '1p')"

PREFIX="${PREFIX:-/usr/local}"
DESTDIR="${DESTDIR:-}"

case "$PREFIX" in
    /*) ;;
    *)
        PREFIX="$(pwd)/$PREFIX"
        ;;
esac

BIN_DIR="${DESTDIR}${PREFIX}/bin"
LIB_DIR="${DESTDIR}${PREFIX}/lib/hydra"
CORE_DIR="${DESTDIR}${PREFIX}/libexec/hydra"
CORE_MODE="${HYDRA_INSTALL_CORE:-auto}"
TUI_MODE="${HYDRA_INSTALL_TUI:-auto}"

case "$CORE_MODE" in
    auto|never|required) ;;
    *)
        echo "Error: HYDRA_INSTALL_CORE must be auto, never, or required" >&2
        exit 1
        ;;
esac
case "$TUI_MODE" in
    auto|never|required) ;;
    *)
        echo "Error: HYDRA_INSTALL_TUI must be auto, never, or required" >&2
        exit 1
        ;;
esac

if [ ! -f "$SRC_BIN" ] || [ ! -d "$SRC_LIB" ]; then
    echo "Error: This script must be run from a hydra source checkout" >&2
    echo "Precondition: bin/hydra and lib/ must exist next to install.sh" >&2
    echo "Next: cd to the hydra directory and run: PREFIX=\$HOME/.local ./install.sh" >&2
    exit 1
fi

if [ -z "$HYDRA_VERSION" ]; then
    echo "Error: cannot determine Hydra version from bin/hydra" >&2
    exit 1
fi

if ! grep -q "Hydra - POSIX-compliant CLI" "$SRC_BIN" 2>/dev/null; then
    echo "Error: bin/hydra does not appear to be the hydra binary" >&2
    echo "Next: cd to the hydra source checkout and retry ./install.sh" >&2
    exit 1
fi

ensure_writable() {
    target="$1"
    if [ -d "$target" ]; then
        if [ -w "$target" ]; then
            return 0
        fi
    elif mkdir -p "$target" 2>/dev/null; then
        return 0
    fi
    echo "Error: PREFIX is not writable: $target" >&2
    echo "Next: PREFIX=\$HOME/.local $0   or   sudo env PREFIX=$PREFIX $0" >&2
    echo "See README Quick Start." >&2
    return 1
}

echo "Installing hydra to $PREFIX..."

ensure_writable "$BIN_DIR"
ensure_writable "$LIB_DIR"

echo "Installing hydra binary..."
cp "$SRC_BIN" "$BIN_DIR/hydra"
chmod +x "$BIN_DIR/hydra"

echo "Installing library files..."
for lib_file in "$SRC_LIB"/*.sh; do
    if [ -f "$lib_file" ]; then
        filename="$(basename "$lib_file")"
        echo "  Installing $filename..."
        cp "$lib_file" "$LIB_DIR/$filename"
    fi
done

core_hash() {
    if command -v sha256sum >/dev/null 2>&1; then
        sha256sum "$1" | awk '{print $1}'
    elif command -v shasum >/dev/null 2>&1; then
        shasum -a 256 "$1" | awk '{print $1}'
    else
        echo "Error: native installation requires sha256sum or shasum" >&2
        return 1
    fi
}

CORE_SOURCE=""
CORE_CHECKSUM=""
CORE_PLATFORM=""
CORE_DEPENDENCIES=""
CORE_SOURCE_REF=""
if [ "$CORE_MODE" != never ]; then
    if [ "${HYDRA_BUILD_CORE:-0}" = 1 ]; then
        echo "Building optional read-only native helper..."
        make -C "$SCRIPT_DIR" build-core
    fi
    if [ -n "${HYDRA_CORE_ARTIFACT:-}" ]; then
        CORE_SOURCE="$HYDRA_CORE_ARTIFACT"
        CORE_CHECKSUM="$CORE_SOURCE.sha256"
        CORE_PLATFORM="$CORE_SOURCE.platform"
        CORE_DEPENDENCIES="$CORE_SOURCE.dependencies"
        CORE_SOURCE_REF="$CORE_SOURCE.source"
        for metadata in "$CORE_CHECKSUM" "$CORE_PLATFORM" "$CORE_DEPENDENCIES" "$CORE_SOURCE_REF"; do
            if [ ! -f "$metadata" ]; then
                echo "Error: offline native artifact metadata is missing: $metadata" >&2
                exit 1
            fi
        done
    elif [ -x "$SCRIPT_DIR/build/hydra-core" ]; then
        CORE_SOURCE="$SCRIPT_DIR/build/hydra-core"
    fi
fi

if [ "$CORE_MODE" = required ] && [ -z "$CORE_SOURCE" ]; then
    echo "Error: native helper is required but no artifact is available" >&2
    echo "Next: set HYDRA_BUILD_CORE=1 or HYDRA_CORE_ARTIFACT=/path/to/hydra-core" >&2
    exit 1
fi

if [ -n "$CORE_SOURCE" ]; then
    [ -x "$CORE_SOURCE" ] || {
        echo "Error: native artifact is not executable: $CORE_SOURCE" >&2
        exit 1
    }
    ensure_writable "$CORE_DIR"
    expected_platform="$(uname -s) $(uname -m)"
    if [ -n "$CORE_PLATFORM" ] && [ "$(sed -n '1p' "$CORE_PLATFORM")" != "$expected_platform" ]; then
        echo "Error: native artifact platform does not match this host" >&2
        echo "Expected: $expected_platform" >&2
        echo "Artifact: $(sed -n '1p' "$CORE_PLATFORM")" >&2
        exit 1
    fi
    if [ -n "$CORE_CHECKSUM" ]; then
        expected_hash="$(awk 'NR == 1 {print $1}' "$CORE_CHECKSUM")"
        actual_hash="$(core_hash "$CORE_SOURCE")"
        if [ -z "$expected_hash" ] || [ "$actual_hash" != "$expected_hash" ]; then
            echo "Error: native artifact checksum verification failed" >&2
            exit 1
        fi
    else
        actual_hash="$(core_hash "$CORE_SOURCE")"
    fi
    if [ "$("$CORE_SOURCE" --protocol-version 2>/dev/null || true)" != 1 ] || \
       [ "$("$CORE_SOURCE" --version 2>/dev/null || true)" != "Hydra core $HYDRA_VERSION protocol 1" ]; then
        echo "Error: native artifact version handshake failed" >&2
        exit 1
    fi

    core_stage="$(mktemp "$CORE_DIR/.hydra-core.XXXXXX")"
    checksum_stage="$core_stage.sha256"
    platform_stage="$core_stage.platform"
    dependencies_stage="$core_stage.dependencies"
    source_stage="$core_stage.source"
    cp "$CORE_SOURCE" "$core_stage"
    chmod +x "$core_stage"
    printf '%s  hydra-core\n' "$actual_hash" > "$checksum_stage"
    printf '%s\n' "$expected_platform" > "$platform_stage"
    if [ -n "$CORE_SOURCE_REF" ]; then
        cp "$CORE_SOURCE_REF" "$source_stage"
    elif source_commit="$(git -C "$SCRIPT_DIR" rev-parse HEAD 2>/dev/null)"; then
        printf '%s\n' "$source_commit" > "$source_stage"
    else
        printf 'hydra-%s-source-tree\n' "$HYDRA_VERSION" > "$source_stage"
    fi
    if [ -n "$CORE_DEPENDENCIES" ]; then
        cp "$CORE_DEPENDENCIES" "$dependencies_stage"
    elif command -v otool >/dev/null 2>&1; then
        otool -L "$core_stage" > "$dependencies_stage"
    elif command -v ldd >/dev/null 2>&1; then
        ldd "$core_stage" > "$dependencies_stage"
    else
        printf 'dependency inspection unavailable on %s\n' "$expected_platform" > "$dependencies_stage"
    fi

    for installed in hydra-core hydra-core.sha256 hydra-core.platform hydra-core.dependencies hydra-core.source; do
        if [ -e "$CORE_DIR/$installed" ]; then
            mv -f "$CORE_DIR/$installed" "$CORE_DIR/$installed.rollback"
        fi
    done
    if mv "$core_stage" "$CORE_DIR/hydra-core" && \
       mv "$checksum_stage" "$CORE_DIR/hydra-core.sha256" && \
       mv "$platform_stage" "$CORE_DIR/hydra-core.platform" && \
       mv "$source_stage" "$CORE_DIR/hydra-core.source" && \
       mv "$dependencies_stage" "$CORE_DIR/hydra-core.dependencies"; then
        echo "Installed optional native helper for $expected_platform"
    else
        echo "Error: native artifact replacement failed; restoring prior helper" >&2
        for installed in hydra-core hydra-core.sha256 hydra-core.platform hydra-core.dependencies hydra-core.source; do
            rm -f "$CORE_DIR/$installed"
            [ ! -e "$CORE_DIR/$installed.rollback" ] || mv -f "$CORE_DIR/$installed.rollback" "$CORE_DIR/$installed"
        done
        exit 1
    fi
fi

TUI_SOURCE=""
TUI_CHECKSUM=""
TUI_PLATFORM=""
TUI_DEPENDENCIES=""
TUI_SOURCE_REF=""
if [ "$TUI_MODE" != never ]; then
    if [ "${HYDRA_BUILD_TUI:-0}" = 1 ]; then
        echo "Building native mission-control TUI..."
        make -C "$SCRIPT_DIR" build-tui
    elif [ -z "${HYDRA_TUI_ARTIFACT:-}" ] && command -v make >/dev/null 2>&1; then
        tui_compiler="${CC:-cc}"
        tui_compiler="${tui_compiler%% *}"
        tui_needs_build=0
        if [ ! -x "$SCRIPT_DIR/build/hydra-tui" ] || \
           [ "$("$SCRIPT_DIR/build/hydra-tui" --version 2>/dev/null || true)" != "Hydra TUI $HYDRA_VERSION protocol 2" ]; then
            tui_needs_build=1
        fi
        if [ "$tui_needs_build" -eq 1 ] && command -v "$tui_compiler" >/dev/null 2>&1; then
            echo "Building native mission-control TUI by default..."
            if ! make -C "$SCRIPT_DIR" build-tui; then
                echo "Warning: native TUI build failed; installing the basic shell fallback only" >&2
            fi
        fi
    fi
    if [ -n "${HYDRA_TUI_ARTIFACT:-}" ]; then
        TUI_SOURCE="$HYDRA_TUI_ARTIFACT"
        TUI_CHECKSUM="$TUI_SOURCE.sha256"
        TUI_PLATFORM="$TUI_SOURCE.platform"
        TUI_DEPENDENCIES="$TUI_SOURCE.dependencies"
        TUI_SOURCE_REF="$TUI_SOURCE.source"
        for metadata in "$TUI_CHECKSUM" "$TUI_PLATFORM" "$TUI_DEPENDENCIES" "$TUI_SOURCE_REF"; do
            if [ ! -f "$metadata" ]; then
                echo "Error: offline native TUI artifact metadata is missing: $metadata" >&2
                exit 1
            fi
        done
    elif [ -x "$SCRIPT_DIR/build/hydra-tui" ] && \
         [ "$("$SCRIPT_DIR/build/hydra-tui" --version 2>/dev/null || true)" = "Hydra TUI $HYDRA_VERSION protocol 2" ]; then
        TUI_SOURCE="$SCRIPT_DIR/build/hydra-tui"
    elif [ -x "$SCRIPT_DIR/build/hydra-tui" ]; then
        echo "Warning: ignoring stale local native TUI; installing the basic shell fallback only" >&2
    fi
fi

if [ "$TUI_MODE" = required ] && [ -z "$TUI_SOURCE" ]; then
    echo "Error: native TUI is required but no artifact is available" >&2
    echo "Next: set HYDRA_BUILD_TUI=1 or HYDRA_TUI_ARTIFACT=/path/to/hydra-tui" >&2
    exit 1
fi

if [ -n "$TUI_SOURCE" ]; then
    [ -x "$TUI_SOURCE" ] || {
        echo "Error: native TUI artifact is not executable: $TUI_SOURCE" >&2
        exit 1
    }
    ensure_writable "$CORE_DIR"
    expected_platform="$(uname -s) $(uname -m)"
    if [ -n "$TUI_PLATFORM" ] && [ "$(sed -n '1p' "$TUI_PLATFORM")" != "$expected_platform" ]; then
        echo "Error: native TUI artifact platform does not match this host" >&2
        exit 1
    fi
    if [ -n "$TUI_CHECKSUM" ]; then
        expected_hash="$(awk 'NR == 1 {print $1}' "$TUI_CHECKSUM")"
        actual_hash="$(core_hash "$TUI_SOURCE")"
        if [ -z "$expected_hash" ] || [ "$actual_hash" != "$expected_hash" ]; then
            echo "Error: native TUI artifact checksum verification failed" >&2
            exit 1
        fi
    else
        actual_hash="$(core_hash "$TUI_SOURCE")"
    fi
    if [ "$("$TUI_SOURCE" --protocol-version 2>/dev/null || true)" != 2 ] || \
       [ "$("$TUI_SOURCE" --version 2>/dev/null || true)" != "Hydra TUI $HYDRA_VERSION protocol 2" ]; then
        echo "Error: native TUI artifact version handshake failed" >&2
        exit 1
    fi

    tui_stage="$(mktemp "$CORE_DIR/.hydra-tui.XXXXXX")"
    tui_checksum_stage="$tui_stage.sha256"
    tui_platform_stage="$tui_stage.platform"
    tui_dependencies_stage="$tui_stage.dependencies"
    tui_source_stage="$tui_stage.source"
    cp "$TUI_SOURCE" "$tui_stage"
    chmod +x "$tui_stage"
    printf '%s  hydra-tui\n' "$actual_hash" > "$tui_checksum_stage"
    printf '%s\n' "$expected_platform" > "$tui_platform_stage"
    if [ -n "$TUI_SOURCE_REF" ]; then
        cp "$TUI_SOURCE_REF" "$tui_source_stage"
    elif source_commit="$(git -C "$SCRIPT_DIR" rev-parse HEAD 2>/dev/null)"; then
        printf '%s\n' "$source_commit" > "$tui_source_stage"
    else
        printf 'hydra-%s-source-tree\n' "$HYDRA_VERSION" > "$tui_source_stage"
    fi
    if [ -n "$TUI_DEPENDENCIES" ]; then
        cp "$TUI_DEPENDENCIES" "$tui_dependencies_stage"
    elif command -v otool >/dev/null 2>&1; then
        otool -L "$tui_stage" > "$tui_dependencies_stage"
    elif command -v ldd >/dev/null 2>&1; then
        ldd "$tui_stage" > "$tui_dependencies_stage"
    else
        printf 'dependency inspection unavailable on %s\n' "$expected_platform" > "$tui_dependencies_stage"
    fi
    for installed in hydra-tui hydra-tui.sha256 hydra-tui.platform hydra-tui.dependencies hydra-tui.source; do
        if [ -e "$CORE_DIR/$installed" ]; then mv -f "$CORE_DIR/$installed" "$CORE_DIR/$installed.rollback"; fi
    done
    if mv "$tui_stage" "$CORE_DIR/hydra-tui" && \
       mv "$tui_checksum_stage" "$CORE_DIR/hydra-tui.sha256" && \
       mv "$tui_platform_stage" "$CORE_DIR/hydra-tui.platform" && \
       mv "$tui_source_stage" "$CORE_DIR/hydra-tui.source" && \
       mv "$tui_dependencies_stage" "$CORE_DIR/hydra-tui.dependencies"; then
        echo "Installed native TUI for $expected_platform"
    else
        echo "Error: native TUI artifact replacement failed; restoring prior TUI" >&2
        for installed in hydra-tui hydra-tui.sha256 hydra-tui.platform hydra-tui.dependencies hydra-tui.source; do
            rm -f "$CORE_DIR/$installed"
            [ ! -e "$CORE_DIR/$installed.rollback" ] || mv -f "$CORE_DIR/$installed.rollback" "$CORE_DIR/$installed"
        done
        exit 1
    fi
fi

if [ ! -x "$BIN_DIR/hydra" ]; then
    echo "Error: Installation failed: $BIN_DIR/hydra is not executable" >&2
    echo "Next: PREFIX=\$HOME/.local $0 or see README Quick Start" >&2
    exit 1
fi

if [ ! -f "$LIB_DIR/git.sh" ]; then
    echo "Error: Installation failed: libraries missing at $LIB_DIR" >&2
    echo "Next: re-run from a complete source checkout (lib/*.sh must exist)" >&2
    exit 1
fi

# Confirm library discovery without a HYDRA_ROOT override
ver_out="$(
    unset HYDRA_ROOT
    "$BIN_DIR/hydra" version
)" || {
    echo "Error: Installed hydra could not run (library discovery failed)" >&2
    echo "Next: confirm $LIB_DIR/git.sh exists, or set HYDRA_ROOT and see README Quick Start" >&2
    exit 1
}

echo ""
echo "Installation complete!"
echo ""
echo "$ver_out"
echo "Binary: $BIN_DIR/hydra"
echo "Libraries: $LIB_DIR"
[ -z "$CORE_SOURCE" ] || echo "Native core: $CORE_DIR/hydra-core"
[ -z "$TUI_SOURCE" ] || echo "Native TUI: $CORE_DIR/hydra-tui"
echo ""
echo "Run 'hydra doctor' to verify readiness, or see README Quick Start."
echo ""

case ":$PATH:" in
    *":$PREFIX/bin:"*)
        ;;
    *)
        echo "WARNING: $PREFIX/bin is not in your PATH"
        echo "Next: add it to your shell configuration:"
        echo "  export PATH=\"$PREFIX/bin:\$PATH\""
        ;;
esac
