#!/usr/bin/env python3
"""Find additive checksum formula: csum == (sum(d[a:len-b]) + K) mod 65536."""
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


def main():
    _, recs = load(sys.argv[1])
    groups = defaultdict(list)
    for r in recs[1:]:
        groups[(r["id"] >> 16, len(r["data"]))].append(r)

    for key, rows in sorted(groups.items(), key=lambda kv: -len(kv[1])):
        if len(rows) < 2:
            continue
        found = []
        for a in range(0, 12):
            for b in range(0, 6):
                ks = set()
                for r in rows:
                    d = r["data"]
                    end = len(d) - b
                    if a >= end:
                        ks = None
                        break
                    csum = be16(d, len(d) - 2)
                    k = (csum - sum(d[a:end])) & 0xFFFF
                    ks.add(k)
                if ks and len(ks) <= 2:
                    found.append((a, b, sorted(ks)))
        print(f"hi=0x{key[0]:04X} size={key[1]:4} n={len(rows):3} v1={rows[0]['v1']} formulas={found}")


if __name__ == "__main__":
    main()
