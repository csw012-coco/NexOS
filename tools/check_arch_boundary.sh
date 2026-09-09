#!/usr/bin/env sh
set -eu

ROOT_DIR=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)

COMMON_PATHS="
block
drivers
fs
kernel/core
kernel/driver
kernel/internal/driver
kernel/proc
kernel/sched
kernel/public/proc
kernel/public/mem
kernel/mem
"

ALLOW_PATH_RE='^(drivers/i386/|drivers/dummy/i386_|hal/i386/|kernel/sys/syscall_i386_)'
PATTERN='#include "arch/x86/i386/|\bi386_(pmm|paging|keyboard|scheduler|process32)|\bdriver_elf(32|64)_'

status=0

for path in $COMMON_PATHS; do
    if [ ! -e "$ROOT_DIR/$path" ]; then
        continue
    fi
    while IFS= read -r line; do
        rel=${line%%:*}
        case "$rel" in
            "" ) continue ;;
        esac
        if printf '%s\n' "$rel" | grep -Eq "$ALLOW_PATH_RE"; then
            continue
        fi
        printf '%s\n' "$line"
        status=1
    done <<EOF
$(cd "$ROOT_DIR" && rg -n "$PATTERN" "$path" || true)
EOF
done

if [ "$status" -ne 0 ]; then
    printf 'arch boundary check failed: common code uses i386-only interfaces\n' >&2
fi

exit "$status"
