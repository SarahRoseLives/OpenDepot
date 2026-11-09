#!/usr/bin/env python3
"""Parse RT_STRING resources to map command IDs to labels (Depot)."""
import json
import struct
import sys

EXE = r"C:\Program Files (x86)\Motorola\ASTRO 25 Products\ASTRO 25 Portable Depot\PatPortDepot.exe"
RESJSON = r"C:\Users\Rose\.local\share\opencode\tool-output\tool_1b5bb8288001g2S4WkwseynXGr"

sys.stdout.reconfigure(encoding="utf-8", errors="replace")


def main():
    data = open(EXE, "rb").read()
    d = json.load(open(RESJSON, encoding="utf-8"))
    res = d["normalized_result"]["resources"]
    terms = ("create", "codeplug", "read device", "write device", "package", "port", "feature", "option")
    for r in res:
        t = r["type"].get("id")
        if t != 6:
            continue
        block = r["name"].get("id")
        pl = r.get("payload") or {}
        loc = pl.get("location") or {}
        off = loc.get("offset")
        size = loc.get("bytes")
        if off is None or not size:
            continue
        b = data[off:off + size]
        o = 0
        for i in range(16):
            if o + 2 > len(b):
                break
            ln = b[o] | (b[o + 1] << 8)
            o += 2
            s = b[o:o + ln * 2].decode("utf-16le", errors="replace")
            o += ln * 2
            if not s:
                continue
            sid = (block - 1) * 16 + i + 1
            low = s.lower()
            if any(t in low for t in terms):
                print(f"id={sid:5d} block={block} idx={i:2d} {s!r}")


if __name__ == "__main__":
    main()
