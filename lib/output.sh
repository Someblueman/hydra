#!/bin/sh
# Output formatting functions for Hydra
# POSIX-compliant shell script
#
# Provides ASCII-only output helpers to replace emoji characters
# and ensure consistent formatting across all output.

# Print success message with [OK] prefix
# Usage: print_success <message>
print_success() {
    echo "  [OK] $1"
}

# Print failure message with [FAIL] prefix
# Usage: print_failure <message>
print_failure() {
    echo "  [FAIL] $1"
}

# Print warning message with [WARN] prefix
# Usage: print_warning <message>
print_warning() {
    echo "  [WARN] $1"
}

# Print info message with [INFO] prefix
# Usage: print_info <message>
print_info() {
    echo "  [INFO] $1"
}

# Print a summary success line (no indent)
# Usage: print_summary_success <message>
print_summary_success() {
    echo "[OK] $1"
}

# Print a summary failure line (no indent)
# Usage: print_summary_failure <message>
print_summary_failure() {
    echo "[FAIL] $1"
}

# =============================================================================
# JSON Output Helpers
# =============================================================================
# POSIX-compliant JSON formatting without external dependencies

# Escape a string for safe use in JSON
# Usage: json_escape <string>
# Returns: Escaped string on stdout
json_escape() {
    # json_escape operates on POSIX C strings (NUL cannot appear in $1).
    # Named JSON escapes for ", \, and common C0; remaining C0 as \u00XX.
    # Bytes >= 32 (including UTF-8 payload bytes) are emitted unchanged; do
    # not use awk "%c" for that, because a UTF-8 locale recodes 0x80-0xFF.
    printf '%s' "$1" | LC_ALL=C od -An -v -tu1 | {
        while IFS= read -r _je_line || [ -n "$_je_line" ]; do
            # shellcheck disable=SC2086
            for _je_b in $_je_line; do
                case "$_je_b" in
                    34) printf '%s' '\"' ;;
                    92) printf '%s' "\\\\" ;;
                    8)  printf '%s' '\b' ;;
                    9)  printf '%s' '\t' ;;
                    10) printf '%s' '\n' ;;
                    12) printf '%s' '\f' ;;
                    13) printf '%s' '\r' ;;
                    *)
                        if [ "$_je_b" -lt 32 ]; then
                            printf '\\u00%02x' "$_je_b"
                        else
                            printf '%b' "$(printf '\\%03o' "$_je_b")"
                        fi
                        ;;
                esac
            done
        done
    }
}

# Output a JSON string key-value pair
# Usage: json_kv <key> <value>
# Returns: "key": "value" on stdout (no trailing comma)
json_kv() {
    printf '"%s": "%s"' "$1" "$(json_escape "$2")"
}

# Output a JSON numeric key-value pair
# Usage: json_kv_num <key> <value>
# Returns: "key": value on stdout (no trailing comma)
json_kv_num() {
    printf '"%s": %s' "$1" "$2"
}

# Output a JSON boolean key-value pair
# Usage: json_kv_bool <key> <true|false>
# Returns: "key": true/false on stdout (no trailing comma)
json_kv_bool() {
    printf '"%s": %s' "$1" "$2"
}

# Output a JSON null key-value pair
# Usage: json_kv_null <key>
# Returns: "key": null on stdout (no trailing comma)
json_kv_null() {
    printf '"%s": null' "$1"
}

# Stable automation envelope for successful commands.
# Usage: json_success <command> <data-json>
json_success() {
    _js_data="${2:-}"
    [ -n "$_js_data" ] || _js_data='{}'
    printf '{"schema_version":1,"ok":true,"command":"%s","data":%s}\n' \
        "$(json_escape "$1")" "$_js_data"
}

# Stable automation envelope for command failures.
# Usage: json_error <command> <code> <message> <recovery>
json_error() {
    printf '{"schema_version":1,"ok":false,"command":"%s","error":{"code":"%s","message":"%s","recovery":"%s"}}\n' \
        "$(json_escape "$1")" "$(json_escape "$2")" \
        "$(json_escape "$3")" "$(json_escape "${4:-}")"
}
