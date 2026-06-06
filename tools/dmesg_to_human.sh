#!/bin/sh
# Convert dmesg relative timestamps [secs] to human HH:MM:SS.mmm relative to boot.
# Usage: ./dmesg_to_human.sh [dmesg_args]

# If dmesg is available, pass args through. If not, read from stdin.
if command -v dmesg >/dev/null 2>&1; then
  DMESG_CMD="dmesg $*"
else
  DMESG_CMD="cat -"
fi

# Run the command and convert leading [secs] to human relative HH:MM:SS.mmm
# This avoids reliance on 'dmesg -T'.
sh -c "$DMESG_CMD" 2>/dev/null | awk '
function sec2hms(s,   hh, mm, ss, ms, intsec) {
    intsec = int(s);
    ms = int((s - intsec) * 1000 + 0.5);
    hh = int(intsec/3600);
    mm = int((intsec%3600)/60);
    ss = int(intsec%60);
    return sprintf("%02d:%02d:%02d.%03d", hh, mm, ss, ms);
}
{
    if (match($0, /^\[ *([0-9]+(\.[0-9]+)?)\]/, m)) {
        t = m[1];
        sub(/^\[[^]]*\] */, "");
        print "[" sec2hms(t) "] " $0;
    } else {
        print $0;
    }
}
'
