#!/usr/bin/env python3
"""Find the exact RC4 key derivation used by CFhAdkSdkCrypto.

Validates candidates against invariants:
  - Strm_0 is a stream map: 10-byte entries (BE32 id, 3x BE16) + 2-byte checksum.
  - Every data stream begins with a 4-byte BE id present in the map, and its last
    two bytes (BE16) equal the map entry's first BE16 field.
"""
import sys
import hashlib
import olefile
from adkcrypt import decrypt as adk_decrypt


def rc4(key, data):
    S = list(range(256))
    j = 0
    for i in range(256):
        j = (j + S[i] + key[i % len(key)]) & 0xFF
        S[i], S[j] = S[j], S[i]
    out = bytearray()
    i = j = 0
    for b in data:
        i = (i + 1) & 0xFF
        j = (j + S[i]) & 0xFF
        S[i], S[j] = S[j], S[i]
        out.append(b ^ S[(S[i] + S[j]) & 0xFF])
    return bytes(out)


def be16(b, o=0):
    return (b[o] << 8) | b[o + 1]


def be32(b, o=0):
    return (b[o] << 24) | (b[o + 1] << 16) | (b[o + 2] << 8) | b[o + 3]


def score(key, map_ct, data_cts):
    m = rc4(key, map_ct)
    entries = {}
    n = len(m) - 2
    for o in range(0, n - 9, 10):
        entries[be32(m, o)] = be16(m, o + 4)
    good = 0
    for ct in data_cts:
        d = rc4(key, ct)
        if len(d) < 6:
            continue
        if entries.get(be32(d, 0)) == be16(d, len(d) - 2):
            good += 1
    return good, entries, m


def main():
    path = sys.argv[1]
    ole = olefile.OleFileIO(path)
    hdr = ole.openstream(["AdkFileHeader"]).read()
    ln = hdr[0x239]
    blob = hdr[0x23A:0x23A + ln]
    seed = adk_decrypt(blob)
    entries = [e for e in ole.listdir() if e[0] == "DataStg" and len(e) == 2]
    entries.sort(key=lambda e: int(e[1].split("_")[1]))
    data = {e[1]: ole.openstream(e).read() for e in entries}
    map_ct = data["Strm_0"]
    probe = [data[f"Strm_{i}"] for i in range(1, 41) if f"Strm_{i}" in data]
    print(f"seed={seed.hex()} ({len(seed)}B)  data streams={len(data)}")

    digest = hashlib.md5(seed[:9]).digest()
    candidates = {}
    for klen in (5, 7, 8, 10, 12, 16):
        candidates[f"md5[:{klen}]"] = digest[:klen]
        candidates[f"md5[-{klen}:]"] = digest[-klen:]
    candidates["seed9"] = seed[:9]
    candidates["md5full"] = digest
    candidates["sha1[:16]"] = hashlib.sha1(seed[:9]).digest()[:16]

    best = None
    for name, key in candidates.items():
        good, ent, m = score(key, map_ct, probe)
        print(f"{name:14} key={key.hex():32} matches={good}/{len(probe)}")
        if best is None or good > best[0]:
            best = (good, name, key, m)
    good, name, key, m = best
    print(f"\nBEST: {name} key={key.hex()} matches={good}")
    print("Strm_0 first 160 bytes:")
    for off in range(0, min(len(m), 160), 16):
        chunk = m[off:off + 16]
        hexs = " ".join(f"{b:02X}" for b in chunk).ljust(47)
        text = "".join(chr(b) if 32 <= b < 127 else "." for b in chunk)
        print(f"{off:08X}  {hexs}  |{text}|")
    print("\nmap entries 0..12 (id, v1, v2, v3):")
    for i in range(13):
        o = i * 10
        print(f"  {i:4}: id=0x{be32(m,o):08X} v1=0x{be16(m,o+4):04X} "
              f"v2=0x{be16(m,o+6):04X} v3=0x{be16(m,o+8):04X}")
    ole.close()


if __name__ == "__main__":
    main()
