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
    # Named JSON escapes for ", \, and common C0; remaining C0 as \\u00XX.
    printf '%s' "$1" | od -An -v -tx1 | awk '
    BEGIN {
        for (i = 0; i < 256; i++) hex[sprintf("%02x", i)] = i
    }
    {
        for (i = 1; i <= NF; i++) {
            b = hex[tolower($i)]
            if (b == 34) { printf "\\\""; continue }
            if (b == 92) { printf "\\\\"; continue }
            if (b == 8) { printf "\\b"; continue }
            if (b == 9) { printf "\\t"; continue }
            if (b == 10) { printf "\\n"; continue }
            if (b == 12) { printf "\\f"; continue }
            if (b == 13) { printf "\\r"; continue }
            if (b < 32) { printf "\\u00%02x", b; continue }
            printf "%c", b
        }
    }
    '
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
