#!/usr/bin/env python3
"""Converts a flat firmware binary into a UF2 file for the RP2040 / RP2350
USB bootloader. Used instead of picotool so the build needs no host binaries.

usage: bin2uf2.py <in.bin> <out.uf2> --family rp2040|rp2350 [--base 0x10000000]
"""
import argparse
import struct

FAMILIES = {
    "rp2040": 0xE48BFF56,
    "rp2350": 0xE48BFF59,  # RP2350, Arm secure
}
MAGIC0, MAGIC1, MAGIC_END = 0x0A324655, 0x9E5D5157, 0x0AB16F30
FLAG_FAMILY_ID = 0x00002000
PAYLOAD = 256


def convert(data: bytes, base: int, family: int) -> bytes:
    blocks = [data[i:i + PAYLOAD] for i in range(0, len(data), PAYLOAD)]
    out = bytearray()
    for n, chunk in enumerate(blocks):
        chunk = chunk.ljust(PAYLOAD, b"\x00")
        header = struct.pack("<8I", MAGIC0, MAGIC1, FLAG_FAMILY_ID, base + n * PAYLOAD,
                             PAYLOAD, n, len(blocks), family)
        block = header + chunk.ljust(476, b"\x00") + struct.pack("<I", MAGIC_END)
        assert len(block) == 512
        out += block
    return bytes(out)


def main() -> None:
    ap = argparse.ArgumentParser()
    ap.add_argument("input")
    ap.add_argument("output")
    ap.add_argument("--family", choices=FAMILIES.keys(), required=True)
    ap.add_argument("--base", type=lambda s: int(s, 0), default=0x10000000)
    args = ap.parse_args()
    with open(args.input, "rb") as f:
        data = f.read()
    with open(args.output, "wb") as f:
        f.write(convert(data, args.base, FAMILIES[args.family]))
    print(f"{args.output}: {len(data)} bytes, {(len(data) + PAYLOAD - 1) // PAYLOAD} blocks")


if __name__ == "__main__":
    main()
