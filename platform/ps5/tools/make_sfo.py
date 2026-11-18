#!/usr/bin/env python3
"""Write a real PS5/PS4 param.sfo binary.

The console reads a little-endian container, not JSON: a 20-byte header, one
16-byte entry per key, then a NUL-separated key table and a 4-byte-aligned data
table. This script builds that container from explicit values so the package
does not depend on an SDK tool that does not exist (the payload SDK's
install_app sample stages param.json and lets the system build the SFO).

Layout, all fields little-endian:

    header  "\0PSF" (0x46535000), version 0x00000101, key table offset,
            data table offset, entry count
    entry   key offset (u16), format (u16), length (u32), max length (u32),
            data offset (u32)
    keys    NUL-terminated ASCII, offsets relative to the key table
    data    values, offsets relative to the data table, 4-byte aligned

Strings are UTF-8 including a trailing NUL in both lengths; integers are one
little-endian u32.
"""

import argparse
import re
import struct
import sys

MAGIC = 0x46535000
VERSION = 0x00000101
FMT_UTF8 = 0x0204
FMT_INT32 = 0x0404

TITLE_ID_RE = re.compile(r"^[A-Z]{4}[0-9]{5}$")


def align4(value):
    return (value + 3) & ~3


def build_sfo(params):
    keys = sorted(params)
    key_table = bytearray()
    key_offsets = {}
    for key in keys:
        key_offsets[key] = len(key_table)
        key_table += key.encode("ascii") + b"\0"

    data_table = bytearray()
    entries = []
    for key in keys:
        fmt, value = params[key]
        if fmt == FMT_UTF8:
            payload = value.encode("utf-8") + b"\0"
        elif fmt == FMT_INT32:
            payload = struct.pack("<I", value & 0xFFFFFFFF)
        else:
            raise ValueError("unsupported format 0x%04x for %s" % (fmt, key))
        offset = len(data_table)
        if offset % 4 != 0:
            raise AssertionError("data offset for %s is not aligned" % key)
        padded = align4(len(payload))
        entries.append((key_offsets[key], fmt, len(payload), padded, offset))
        data_table += payload + b"\0" * (padded - len(payload))

    header_size = 20 + 16 * len(keys)
    key_start = header_size
    data_start = align4(key_start + len(key_table))

    out = bytearray()
    out += struct.pack("<IIIII", MAGIC, VERSION, key_start, data_start, len(keys))
    for entry in entries:
        out += struct.pack("<HHIII", *entry)
    if len(out) != key_start:
        raise AssertionError("entry table size mismatch")
    out += key_table
    out += b"\0" * (data_start - len(out))
    out += data_table
    return bytes(out)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--title-id", required=True)
    parser.add_argument("--title", required=True)
    parser.add_argument("--content-id", required=True)
    parser.add_argument("--app-ver", required=True)
    parser.add_argument("--output", required=True)
    args = parser.parse_args()

    if not TITLE_ID_RE.match(args.title_id):
        sys.exit("title id %r is not four uppercase letters and five digits" % args.title_id)
    content_re = re.compile(r"^[A-Z]{2}[0-9]{4}-%s_00-[A-Z0-9]{16}$" % args.title_id)
    if not content_re.match(args.content_id):
        sys.exit("content id %r does not contain %s in Sony's 36-character form"
                 % (args.content_id, args.title_id))
    if not re.match(r"^[0-9]{2}\.[0-9]{2}$", args.app_ver):
        sys.exit("app version %r is not NN.NN" % args.app_ver)
    if not args.title.strip():
        sys.exit("title is empty")

    params = {
        "APP_TYPE": (FMT_INT32, 1),
        "APP_VER": (FMT_UTF8, args.app_ver),
        "ATTRIBUTE": (FMT_INT32, 0),
        "CATEGORY": (FMT_UTF8, "gd"),
        "CONTENT_ID": (FMT_UTF8, args.content_id),
        "PARENTAL_LEVEL": (FMT_INT32, 1),
        "SYSTEM_VER": (FMT_INT32, 0),
        "TITLE": (FMT_UTF8, args.title),
        "TITLE_ID": (FMT_UTF8, args.title_id),
        "VERSION": (FMT_UTF8, args.app_ver),
    }

    blob = build_sfo(params)
    with open(args.output, "wb") as handle:
        handle.write(blob)
    print("param.sfo: %s keys=%d bytes=%d" % (args.output, len(params), len(blob)))


if __name__ == "__main__":
    main()
