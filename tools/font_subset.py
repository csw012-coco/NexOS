#!/usr/bin/env python3
import sys


RANGES = (
    (0x0000, 0x00FF),  # ASCII and Latin-1
    (0x1100, 0x11FF),  # Hangul Jamo
    (0x2000, 0x206F),  # General punctuation
    (0x2190, 0x21FF),  # Arrows
    (0x2500, 0x259F),  # Box drawing and block elements
    (0x3000, 0x303F),  # CJK punctuation
    (0x3130, 0x318F),  # Hangul compatibility Jamo
    (0xAC00, 0xD7A3),  # Hangul syllables
    (0xFF00, 0xFFEF),  # Halfwidth and fullwidth forms
)


def wanted(codepoint):
    return codepoint == 0xFFFD or any(start <= codepoint <= end for start, end in RANGES)


def main():
    if len(sys.argv) != 3:
        print(f"usage: {sys.argv[0]} INPUT.HEX OUTPUT.HEX", file=sys.stderr)
        return 2

    kept = 0
    with open(sys.argv[1], "r", encoding="ascii") as source, \
            open(sys.argv[2], "w", encoding="ascii", newline="\n") as output:
        for line in source:
            separator = line.find(":")
            if separator <= 0:
                continue
            try:
                codepoint = int(line[:separator], 16)
            except ValueError:
                continue
            if wanted(codepoint):
                output.write(line.rstrip("\r\n") + "\n")
                kept += 1

    if kept == 0:
        print("font subset is empty", file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
