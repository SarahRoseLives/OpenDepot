#!/usr/bin/env python3
"""Analyze decoded codeplug records: checksum rule, v1 flag, record ids."""
import sys
import os
import json
from collections import Counter


def be16(b, o=0):
    return (b[o] << 8) | b[o + 1]


def be32(b, o=0):
    return (b[o] << 24) | (b[o + 1] << 16) | (b[o + 2] << 8) | b[o + 3]


def load(d):
    with open(os.path.join(d, "manifest.json"), encoding="utf-8") as f:
        man = json.load(f)
    recs = []
    for row in man["map"]:
        with open(os.path.join(d, row["stream"] + ".bin"), "rb") as f:
            data = f.read()
        recs.append({"id": int(row["id"], 16), "v1": row["v1"], "v2": row["v2"],
                     "v3": row["v3"], "data": data, "stream": row["stream"]})
    return man, recs


def main():
    _, recs = load(sys.argv[1])
    print(f"records={len(recs)}")
    print("v1 distribution:", Counter(r["v1"] for r in recs))

    # checksum hypotheses
    names = ["sum(all-2)", "sum(payload)", "sum(id+len+payload)", "sum(id2+len2+payload)"]
    scores = {n: 0 for n in names}
    total = 0
    samples = []
    for r in recs[1:]:
        d = r["data"]
        if len(d) < 8:
            continue
        total += 1
        csum = be16(d, len(d) - 2)
        cand = {
            "sum(all-2)": sum(d[:-2]) & 0xFFFF,
            "sum(payload)": sum(d[6:-2]) & 0xFFFF,
            "sum(id+len+payload)": sum(d[4:-2]) & 0xFFFF,
            "sum(id2+len2+payload)": sum(d[2:-2]) & 0xFFFF,
        }
        for n in names:
            if cand[n] == csum:
                scores[n] += 1
        if len(samples) < 5:
            samples.append((r["id"], len(d), csum, cand))
    print(f"exact-checksum matches over {total}: {scores}")
    for sid, ln, csum, cand in samples:
        print(f"  id=0x{sid:08X} len={ln} csum=0x{csum:04X} {cand}")

    # try affine relation csum = (sum + K) mod 65536 over all records
    for field in ["sum(all-2)", "sum(payload)"]:
        Ks = Counter()
        for r in recs[1:]:
            d = r["data"]
            if len(d) < 8:
                continue
            csum = be16(d, len(d) - 2)
            s = sum(d[:-2]) & 0xFFFF if field == "sum(all-2)" else sum(d[6:-2]) & 0xFFFF
            Ks[(csum - s) & 0xFFFF] += 1
        print(f"\n{field}: top (csum - sum) constants:")
        for k, c in Ks.most_common(5):
            print(f"  0x{k:04X} x{c}")

    # id high-word distribution
    hi = Counter((r["id"] >> 16) for r in recs)
    print("\ntop id high-words:", [(f"0x{k:04X}", v) for k, v in hi.most_common(20)])
    # v1 for each id high word
    byhi = {}
    for r in recs:
        byhi.setdefault(r["id"] >> 16, Counter())[r["v1"]] += 1
    for k in sorted(byhi)[:20]:
        print(f"  hi=0x{k:04X} v1={dict(byhi[k])}")


if __name__ == "__main__":
    main()
