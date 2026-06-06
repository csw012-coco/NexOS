#!/bin/sh
# Portable wrapper for extended-regex grep functionality.
# Usage: ./grepE_compat.sh "PATTERN" [FILE ...]

# If grep supports -E, use it. Else try egrep. Else fallback to awk.
if grep -E "" </dev/null >/dev/null 2>&1; then
    grep -E "$@"
    exit $?
fi
if command -v egrep >/dev/null 2>&1; then
    egrep "$@"
    exit $?
fi
# Fallback: first arg is pattern
pattern="$1"
shift
awk -v pat="$pattern" 'BEGIN{IGNORECASE=0} $0 ~ pat' "$@"
