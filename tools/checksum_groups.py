#!/usr/bin/env python3
"""Determine per-record checksum formula by grouping records."""
import sys
import os
import json
from collections import defaultdict


def be16(b, o=0):
    return (b[o] << 8) | b[o + 1]


def load(d):
    with open(os.path.join(d, "manifest.json"), encoding="utf-8") as f:
        man = json.load(f)
    recs = []
    for row in man["map"]:
        with open(os.path.join(d, row["stream"] + ".bin"), "rb") as f:
            data = f.read()
        recs.append({"id": int(row["id"], 16), "v1": row["v1"], "data": data})
    return man, recs


def candidates(d):
    body = d[:-2]
    return {
        "sum(payload)": sum(d[6:-2]),
        "sum(payload)+AAAB": sum(d[6:-2]) + 0xAAAB,
        "sum(payload)+AAAC": sum(d[6:-2]) + 0xAAAC,
        "sum(d[:-2])": sum(body),
        "sum(d[:-2])+AAAB": sum(body) + 0xAAAB,
        "-sum(payload)": -sum(d[6:-2]),
        "AAAB-sum(payload)": 0xAAAB - sum(d[6:-2]),
        "sum(payload+2)": sum(d[6:-2]) + 2,
        "sum(payload)+len": sum(d[6:-2]) + len(d),
        "xor": sum(d[6:-2]) ^ 0xFFFF,
    }


def main():
    _, recs = load(sys.argv[1])
    groups = defaultdict(list)
    for r in recs[1:]:
        d = r["data"]
        groups[(r["id"] >> 16, len(d), r["v1"])].append(r)
    print(f"groups={len(groups)}")
    match_counts = defaultdict(int)
    for key, rows in sorted(groups.items(), key=lambda kv: -len(kv[1])):
        if len(rows) < 2:
            continue
        best = defaultdict(int)
        for r in rows:
            d = r["data"]
            csum = be16(d, len(d) - 2)
            for name, val in candidates(d).items():
                if (val & 0xFFFF) == csum:
                    best[name] += 1
        top = sorted(best.items(), key=lambda kv: -kv[1])[:3]
        print(f"  hi=0x{key[0]:04X} size={key[1]:4} v1={key[2]} n={len(rows):3} -> {top}")
    # map checksum
    # (recompute separately in caller context not available here)


if __name__ == "__main__":
    main()
