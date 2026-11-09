#!/usr/bin/env python3
"""Search for the per-record (block) checksum algorithm using known-good records."""
import sys
import os
import json


def be16(b, o=0):
    return (b[o] << 8) | b[o + 1]


def load(d):
    with open(os.path.join(d, "manifest.json"), encoding="utf-8") as f:
        man = json.load(f)
    recs = []
    for row in man["map"]:
        with open(os.path.join(d, row["stream"] + ".bin"), "rb") as f:
            data = f.read()
        recs.append({"id": int(row["id"], 16), "data": data})
    return recs


def crc16_ccitt(data, init=0xFFFF, poly=0x1021):
    crc = init
    for b in data:
        crc ^= b << 8
        for _ in range(8):
            crc = ((crc << 1) ^ poly) & 0xFFFF if crc & 0x8000 else (crc << 1) & 0xFFFF
    return crc


def crc16_ibm(data, init=0x0000, poly=0xA001):
    crc = init
    for b in data:
        crc ^= b
        for _ in range(8):
            crc = (crc >> 1) ^ poly if crc & 1 else crc >> 1
    return crc & 0xFFFF


def fletcher16(data):
    a = b = 0
    for x in data:
        a = (a + x) % 255
        b = (b + a) % 255
    return (b << 8) | a


def sum16be(data):
    s = 0
    for i in range(0, len(data) - 1, 2):
        s += (data[i] << 8) | data[i + 1]
    return s & 0xFFFF


def sum16le(data):
    s = 0
    for i in range(0, len(data) - 1, 2):
        s += data[i] | (data[i + 1] << 8)
    return s & 0xFFFF


def xor16be(data):
    s = 0
    for i in range(0, len(data) - 1, 2):
        s ^= (data[i] << 8) | data[i + 1]
    return s


ALGOS = {
    "sum8[6:-2]": lambda d: sum(d[6:-2]) & 0xFFFF,
    "sum8[0:-2]": lambda d: sum(d[:-2]) & 0xFFFF,
    "sum8[4:-2]": lambda d: sum(d[4:-2]) & 0xFFFF,
    "sum16be[6:-2]": lambda d: sum16be(d[6:-2]),
    "sum16le[6:-2]": lambda d: sum16le(d[6:-2]),
    "sum16be[0:-2]": lambda d: sum16be(d[:-2]),
    "sum16le[0:-2]": lambda d: sum16le(d[:-2]),
    "xor16be[6:-2]": lambda d: xor16be(d[6:-2]),
    "crc_ccitt[6:-2]": lambda d: crc16_ccitt(d[6:-2]),
    "crc_ccitt[0:-2]": lambda d: crc16_ccitt(d[:-2]),
    "crc_ccitt0[6:-2]": lambda d: crc16_ccitt(d[6:-2], 0),
    "crc_ibm[6:-2]": lambda d: crc16_ibm(d[6:-2]),
    "crc_ibm[0:-2]": lambda d: crc16_ibm(d[:-2]),
    "fletcher[6:-2]": lambda d: fletcher16(d[6:-2]),
    "fletcher[0:-2]": lambda d: fletcher16(d[:-2]),
}


def main():
    recs = load(sys.argv[1])
    counts = {k: 0 for k in ALGOS}
    total = 0
    per_type = {}
    for r in recs[1:]:
        d = r["data"]
        if len(d) < 8:
            continue
        total += 1
        csum = be16(d, len(d) - 2)
        matched_any = False
        for name, fn in ALGOS.items():
            if fn(d) == csum:
                counts[name] += 1
                matched_any = True
        key = (r["id"] >> 16, len(d))
        per_type.setdefault(key, [0, 0])
        per_type[key][1] += 1
        if matched_any:
            per_type[key][0] += 1
    print(f"total records={total}")
    for k, v in sorted(counts.items(), key=lambda kv: -kv[1]):
        print(f"  {k:20} {v}/{total}")
    print("\ntop groups (matched/total):")
    for key, (m, t) in sorted(per_type.items(), key=lambda kv: -kv[1][1])[:15]:
        print(f"  hi=0x{key[0]:04X} size={key[1]:4} {m}/{t}")


if __name__ == "__main__":
    main()
