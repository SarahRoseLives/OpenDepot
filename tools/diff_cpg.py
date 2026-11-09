#!/usr/bin/env python3
"""Diff two decoded .cpg stream sets by record id and dump changed payloads."""
import sys
import os
import json


def be16(b, o=0):
    return (b[o] << 8) | b[o + 1]


def be32(b, o=0):
    return (b[o] << 24) | (b[o + 1] << 16) | (b[o + 2] << 8) | b[o + 3]


def load(d):
    with open(os.path.join(d, "manifest.json"), encoding="utf-8") as f:
        man = json.load(f)
    recs = {}
    for row in man["map"]:
        name = row["stream"]
        path = os.path.join(d, name + ".bin")
        with open(path, "rb") as f:
            data = f.read()
        recs[row["id"]] = {"stream": name, "data": data, "size": len(data),
                           "v1": row["v1"], "v2": row["v2"], "v3": row["v3"]}
    return man, recs


def hexdump(data, limit=128):
    out = []
    n = min(len(data), limit)
    for off in range(0, n, 16):
        chunk = data[off:off + 16]
        h = " ".join(f"{b:02X}" for b in chunk).ljust(47)
        t = "".join(chr(b) if 32 <= b < 127 else "." for b in chunk)
        out.append(f"{off:06X}  {h}  |{t}|")
    return "\n".join(out)


def main():
    a, b = sys.argv[1], sys.argv[2]
    ma, ra = load(a)
    mb, rb = load(b)
    common = sorted(set(ra) & set(rb), key=lambda x: int(x, 16))
    added = [x for x in rb if x not in ra]
    removed = [x for x in ra if x not in rb]
    changed = []
    for rid in common:
        if ra[rid]["data"] != rb[rid]["data"]:
            changed.append(rid)
    print(f"common={len(common)} added={len(added)} removed={len(removed)} changed={len(changed)}")
    print(f"\nADDED ({len(added)}):")
    for rid in sorted(added, key=lambda x: int(x, 16)):
        r = rb[rid]
        print(f"  id={rid} stream={r['stream']} size={r['size']} v1={r['v1']} v3={r['v3']}")
    print(f"\nREMOVED ({len(removed)}):")
    for rid in sorted(removed, key=lambda x: int(x, 16)):
        r = ra[rid]
        print(f"  id={rid} stream={r['stream']} size={r['size']} v1={r['v1']} v3={r['v3']}")
    print(f"\nCHANGED ({len(changed)}):")
    for rid in sorted(changed, key=lambda x: int(x, 16)):
        ra_ = ra[rid]["data"]
        rb_ = rb[rid]["data"]
        # first differing offset
        off = next((i for i in range(min(len(ra_), len(rb_))) if ra_[i] != rb_[i]), min(len(ra_), len(rb_)))
        print(f"\n--- id={rid} streamA={ra[rid]['stream']} streamB={rb[rid]['stream']} "
              f"sizeA={len(ra_)} sizeB={len(rb_)} firstdiff=0x{off:X} v1 {ra[rid]['v1']}->{rb[rid]['v1']}")
        print("  A:")
        print(hexdump(ra_, 160))
        print("  B:")
        print(hexdump(rb_, 160))


if __name__ == "__main__":
    main()
