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
    # Escape backslashes, double quotes, and tabs
    # Note: newlines in branch/session names are not expected, but we handle tabs
    printf '%s' "$1" | sed -e 's/\\/\\\\/g' -e 's/"/\\"/g' -e 's/	/\\t/g'
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
