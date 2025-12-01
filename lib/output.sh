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
