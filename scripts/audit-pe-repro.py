#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
"""Fail-closed audit for the PE IMAGE_DEBUG_TYPE_REPRO marker."""
import argparse
import json
from pathlib import Path
import struct
import sys

IMAGE_DEBUG_TYPE_REPRO = 16
IMAGE_FILE_MACHINE_AMD64 = 0x8664
IMAGE_NT_OPTIONAL_HDR64_MAGIC = 0x20B
IMAGE_DIRECTORY_ENTRY_DEBUG = 6


def u16(b, o):
    return struct.unpack_from("<H", b, o)[0]


def u32(b, o):
    return struct.unpack_from("<I", b, o)[0]


def fail(message):
    raise ValueError(message)


def rva_to_offset(data, sections, rva, size):
    for section in sections:
        va = section["virtual_address"]
        span = max(section["virtual_size"], section["raw_size"])
        if va <= rva and rva + size <= va + span:
            delta = rva - va
            if delta + size > section["raw_size"]:
                fail("debug directory extends beyond section raw data")
            return section["raw_ptr"] + delta
    fail(f"RVA 0x{rva:x} is not backed by section raw data")


def audit(path):
    data = path.read_bytes()
    if len(data) < 0x100 or data[:2] != b"MZ":
        fail("not an MZ/PE image")
    pe = u32(data, 0x3C)
    if pe + 24 > len(data) or data[pe:pe+4] != b"PE\0\0":
        fail("PE signature missing")

    coff = pe + 4
    machine = u16(data, coff)
    sections_count = u16(data, coff + 2)
    timestamp = u32(data, coff + 4)
    optional_size = u16(data, coff + 16)
    optional = coff + 20
    if machine != IMAGE_FILE_MACHINE_AMD64:
        fail(f"machine 0x{machine:04x} is not AMD64")
    if optional + optional_size > len(data):
        fail("optional header truncated")
    if u16(data, optional) != IMAGE_NT_OPTIONAL_HDR64_MAGIC:
        fail("optional header is not PE32+")

    data_directory = optional + 112
    if data_directory + (IMAGE_DIRECTORY_ENTRY_DEBUG + 1) * 8 > optional + optional_size:
        fail("debug data directory missing")
    debug_rva = u32(data, data_directory + IMAGE_DIRECTORY_ENTRY_DEBUG * 8)
    debug_size = u32(data, data_directory + IMAGE_DIRECTORY_ENTRY_DEBUG * 8 + 4)
    if not debug_rva or debug_size < 28 or debug_size % 28:
        fail("debug directory absent or malformed")

    section_table = optional + optional_size
    sections = []
    for index in range(sections_count):
        off = section_table + index * 40
        if off + 40 > len(data):
            fail("section table truncated")
        sections.append({
            "virtual_size": u32(data, off + 8),
            "virtual_address": u32(data, off + 12),
            "raw_size": u32(data, off + 16),
            "raw_ptr": u32(data, off + 20),
        })

    debug_off = rva_to_offset(data, sections, debug_rva, debug_size)
    if debug_off + debug_size > len(data):
        fail("debug directory file range truncated")

    entries = []
    repro = False
    for index in range(debug_size // 28):
        off = debug_off + index * 28
        debug_type = u32(data, off + 12)
        size_of_data = u32(data, off + 16)
        entries.append({"index": index, "type": debug_type, "size_of_data": size_of_data})
        if debug_type == IMAGE_DEBUG_TYPE_REPRO:
            repro = True
    if not repro:
        fail("IMAGE_DEBUG_TYPE_REPRO marker missing")

    return {
        "file": path.name,
        "bytes": len(data),
        "machine": "AMD64",
        "coff_timestamp_hex": f"0x{timestamp:08x}",
        "debug_entries": entries,
        "image_debug_type_repro": True,
    }


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--input", required=True, type=Path)
    ap.add_argument("--report", type=Path)
    args = ap.parse_args()
    result = audit(args.input)
    text = json.dumps(result, indent=2, sort_keys=True) + "\n"
    if args.report:
        args.report.parent.mkdir(parents=True, exist_ok=True)
        args.report.write_text(text, encoding="utf-8")
    print("H10_PE_REPRO_AUDIT=PASS; machine=AMD64; image_debug_type_repro=YES")
    print(text, end="")
    return 0


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except (OSError, ValueError, struct.error) as exc:
        print(f"H10_PE_REPRO_AUDIT=FAIL: {exc}", file=sys.stderr)
        raise SystemExit(1)
