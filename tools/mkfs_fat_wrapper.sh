#!/bin/sh
for tool in /usr/sbin/mkfs.fat /sbin/mkfs.fat /usr/bin/mkfs.fat /bin/mkfs.fat; do
    if [ -x "$tool" ] && [ "$tool" != "$0" ]; then
        exec "$tool" "$@"
    fi
done
printf '%s\n' 'mkfs.fat: host mkfs.fat not found' >&2
exit 127
