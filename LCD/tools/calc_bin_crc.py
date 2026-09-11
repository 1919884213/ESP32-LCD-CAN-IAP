#!/usr/bin/env python3
"""Calculate the CRC used by this project's CAN-IAP binary metadata."""

from __future__ import annotations

import argparse
import sys
import zlib
from pathlib import Path


def crc16_modbus(data: bytes) -> int:
    """CRC-16/Modbus: init=0xFFFF, poly=0xA001, refin/refout=true."""
    crc = 0xFFFF
    for byte in data:
        crc ^= byte
        for _ in range(8):
            crc = (crc >> 1) ^ 0xA001 if crc & 1 else crc >> 1
    return crc & 0xFFFF


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(
        description="Calculate CRC-16/Modbus for a firmware .bin file."
    )
    parser.add_argument("bin_file", type=Path, help="Path to the firmware .bin file")
    parser.add_argument(
        "--crc32",
        action="store_true",
        help="Also print the standard IEEE CRC-32 value.",
    )
    return parser.parse_args()


def main() -> int:
    args = parse_args()
    bin_file = args.bin_file.expanduser()

    if not bin_file.is_file():
        print(f"Error: file not found: {bin_file}", file=sys.stderr)
        return 1

    try:
        data = bin_file.read_bytes()
    except OSError as error:
        print(f"Error: cannot read {bin_file}: {error}", file=sys.stderr)
        return 1

    crc16 = crc16_modbus(data)
    print(f"File: {bin_file}")
    print(f"Size: {len(data)} bytes (0x{len(data):X})")
    print(f"CRC-16/Modbus: 0x{crc16:04X}")
    print(f"IAP metadata bytes (little-endian): {crc16 & 0xFF:02X} {crc16 >> 8:02X}")

    if args.crc32:
        print(f"CRC-32/IEEE: 0x{zlib.crc32(data) & 0xFFFFFFFF:08X}")

    return 0


if __name__ == "__main__":
    raise SystemExit(main())
