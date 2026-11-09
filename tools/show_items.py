#!/usr/bin/env python3
"""Show decoded ISH items by tag from a cpg_decrypt output directory."""
import sys
import os
import json


def be16(b, o=0):
    return (b[o] << 8) | b[o + 1]


def be32(b, o=0):
    return (b[o] << 24) | (b[o + 1] << 16) | (b[o + 2] << 8) | b[o + 3]


def main():
    d = sys.argv[1]
    wanted = set(int(x, 16) for x in sys.argv[2:]) if len(sys.argv) > 2 else None
    with open(os.path.join(d, "manifest.json"), encoding="utf-8") as f:
        man = json.load(f)
    for row in man["map"]:
        tag = int(row["id"], 16) >> 16
        if wanted and tag not in wanted:
            continue
        with open(os.path.join(d, row["stream"] + ".bin"), "rb") as f:
            data = f.read()
        inst = be16(data, 2)
        ln = be16(data, 4)
        payload = data[6:-2]
        print(f"=== tag=0x{tag:04X} inst={inst} stream={row['stream']} part=0x{row['v1']:04X} "
              f"size={len(data)} len={ln} crc=0x{row['v2']:04X} ===")
        for off in range(0, len(payload), 16):
            chunk = payload[off:off + 16]
            h = " ".join(f"{x:02X}" for x in chunk).ljust(47)
            t = "".join(chr(x) if 32 <= x < 127 else "." for x in chunk)
            print(f"  {off:04X}  {h}  |{t}|")
        print()


if __name__ == "__main__":
    main()
