#!/usr/bin/env python3
"""Map Depot toolbar command IDs to labels via RT_STRING, and find Create Codeplug."""
import json
import sys

EXE = r"C:\Program Files (x86)\Motorola\ASTRO 25 Products\ASTRO 25 Portable Depot\PatPortDepot.exe"
RESJSON = r"C:\Users\Rose\.local\share\opencode\tool-output\tool_1b5bb8288001g2S4WkwseynXGr"
sys.stdout.reconfigure(encoding="utf-8", errors="replace")


def main():
    data = open(EXE, "rb").read()
    d = json.load(open(RESJSON, encoding="utf-8"))
    res = d["normalized_result"]["resources"]

    strings = {}
    for r in res:
        if r["type"].get("id") != 6:
            continue
        block = r["name"].get("id")
        loc = (r.get("payload") or {}).get("location") or {}
        off, size = loc.get("offset"), loc.get("bytes")
        if off is None or not size:
            continue
        b = data[off:off + size]
        o = 0
        for i in range(16):
            if o + 2 > len(b):
                break
            ln = b[o] | (b[o + 1] << 8); o += 2
            s = b[o:o + ln * 2].decode("utf-16le", errors="replace"); o += ln * 2
            if s:
                strings[(block - 1) * 16 + i + 1] = s

    print("=== strings matching Create/New/Codeplug ===")
    for sid, s in sorted(strings.items()):
        low = s.lower()
        if "create" in low or "new codeplug" in low or "codeplug" in low and ("new" in low or "create" in low):
            print(f"  id={sid} {s!r}")

    print("\n=== toolbars ===")
    for r in res:
        if r["type"].get("id") != 241:
            continue
        name = r["name"].get("id")
        loc = (r.get("payload") or {}).get("location") or {}
        off, size = loc.get("offset"), loc.get("bytes")
        if off is None or not size:
            continue
        b = data[off:off + size]
        if len(b) < 8:
            continue
        ver = b[0] | (b[1] << 8)
        n = b[6] | (b[7] << 8)
        ids = []
        o = 8
        for _ in range(n):
            if o + 4 > len(b):
                break
            cmd = b[o + 2] | (b[o + 3] << 8)
            ids.append(cmd)
            o += 4
        print(f"--- toolbar {name} ver={ver} buttons={n} ---")
        for cid in ids:
            print(f"  cmd={cid:5d} {strings.get(cid, '')!r}")


if __name__ == "__main__":
    main()
