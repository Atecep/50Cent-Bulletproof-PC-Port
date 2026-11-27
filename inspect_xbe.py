#!/usr/bin/env python3
"""Inspect an original Xbox XBE using Python's standard library.

Usage: python3 inspect_xbe.py /path/to/default.xbe [path/to/XboxXBERecomp/sdk]
Does not modify the original file or extract game content.
"""
from __future__ import annotations

import hashlib
import struct
import sys
from collections import Counter
from pathlib import Path


def u32(blob: bytes, offset: int) -> int:
    if offset < 0 or offset + 4 > len(blob):
        raise ValueError(f"Offset 0x{offset:X} outside file")
    return struct.unpack_from('<I', blob, offset)[0]


def inspect(path: Path, sdk: Path | None = None) -> None:
    blob = path.read_bytes()
    if blob[:4] != b'XBEH':
        raise ValueError('Invalid XBE header (expected XBEH). Check ISO extraction.')

    base = u32(blob, 0x104)
    headers_size = u32(blob, 0x108)
    section_count = u32(blob, 0x11C)
    section_table_va = u32(blob, 0x120)
    if section_count > 1024:
        raise ValueError(f'Implausible section count: {section_count}')

    def va_to_off(va: int, size: int = 1) -> int:
        header_offset = va - base
        if va >= base and 0 <= header_offset and header_offset + size <= min(headers_size, len(blob)):
            return header_offset
        table_offset = section_table_va - base
        if not (0 <= table_offset and table_offset + section_count * 56 <= len(blob)):
            raise ValueError('Section headers are invalid or outside the XBE header')
        for i in range(section_count):
            section_offset = table_offset + 56 * i
            virtual_addr = u32(blob, section_offset + 4)
            raw_addr = u32(blob, section_offset + 12)
            raw_size = u32(blob, section_offset + 16)
            displacement = va - virtual_addr
            if 0 <= displacement and displacement + size <= raw_size and raw_addr + displacement + size <= len(blob):
                return raw_addr + displacement
        raise ValueError(f'Cannot locate VA 0x{va:08X} in XBE file')

    print(f'File: {path}')
    print(f'Size: {len(blob):,} bytes')
    print(f'SHA-256: {hashlib.sha256(blob).hexdigest()}')
    print(f'Image base: 0x{base:08X} | Sections: {section_count}')

    cert_va = u32(blob, 0x118)
    if cert_va:
        try:
            cert_offset = va_to_off(cert_va, 0x5C)
            title_id = u32(blob, cert_offset + 8)
            title = blob[cert_offset + 12: cert_offset + 92].decode('utf-16-le', errors='replace').split('\x00', 1)[0]
            print(f'Title ID: {title_id:08X}')
            print(f'Title: {title or "(no title in certificate)"}')
        except (ValueError, struct.error) as exc:
            print(f'Certificate: could not decode ({exc})')

    count = u32(blob, 0x160)
    versions_va = u32(blob, 0x164)
    if count > 128:
        raise ValueError(f'Implausible library version count: {count}')
    if not count or not versions_va:
        print('No library version table available; XDK build could not be inferred.')
        return

    start = va_to_off(versions_va, count * 16)
    print('\nLinked libraries (XDK build numbers):')
    builds: list[int] = []
    preferred_build: int | None = None
    for i in range(count):
        name_bytes, major, minor, build, flags = struct.unpack_from('<8sHHHH', blob, start + 16 * i)
        name = name_bytes.rstrip(b'\x00 ').decode('ascii', errors='replace')
        print(f'  {name or "(unnamed)":9s} {major}.{minor}.{build} (flags 0x{flags:04X})')
        if build:
            builds.append(build)
        if name.upper() == 'XAPILIB' and build:
            preferred_build = build

    if builds:
        most_common = Counter(builds).most_common(1)[0][0]
        candidate = preferred_build or most_common
        print(f'\nLikely XDK build: {candidate} (verify with XboxXBERecomp xbe_symbols)')
        if sdk is not None:
            xdk_dir = sdk / 'xdk' if (sdk / 'xdk').is_dir() else sdk
            print(f'Signature directory: {xdk_dir}')
            for build in sorted(set(builds)):
                file = xdk_dir / str(build) / 'signatures.toml'
                print(f'  {build}: {"present" if file.is_file() else "not present"} ({file})')
            print('Presence of signatures does not guarantee that all symbols will match.')


def main() -> int:
    if not 2 <= len(sys.argv) <= 3:
        print(__doc__.strip(), file=sys.stderr)
        return 2
    path = Path(sys.argv[1]).expanduser().resolve()
    sdk = Path(sys.argv[2]).expanduser().resolve() if len(sys.argv) == 3 else None
    try:
        inspect(path, sdk)
        return 0
    except (OSError, ValueError, struct.error) as exc:
        print(f'Error: {exc}', file=sys.stderr)
        return 1


if __name__ == '__main__':
    raise SystemExit(main())
