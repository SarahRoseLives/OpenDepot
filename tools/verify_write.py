#!/usr/bin/env python3
"""Verify write-path assumptions:
  - map trailing checksum == sum(map[:-2]) + 0xAAAB
  - each record length field == len-6
  - RC4 encrypt(decrypt(stream)) reproduces the original ciphertext exactly
"""
import sys
import os
import json
import ctypes
from ctypes import wintypes
import olefile
from adkcrypt import decrypt as adk_decrypt
from cpg_decrypt import Rc4, be16, be32


def main():
    path = sys.argv[1]
    ole = olefile.OleFileIO(path)
    hdr = ole.openstream(["AdkFileHeader"]).read()
    seed = adk_decrypt(hdr[0x23A:0x23A + hdr[0x239]])
    rc4 = Rc4(seed[:9])

    entries = [e for e in ole.listdir() if e[0] == "DataStg" and len(e) == 2]
    entries.sort(key=lambda e: int(e[1].split("_")[1]))

    ok_rt = 0
    bad_rt = 0
    for e in entries:
        raw = ole.openstream(e).read()
        pt = rc4.decrypt(raw)
        ct = rc4.encrypt(pt)
        if ct == raw:
            ok_rt += 1
        else:
            bad_rt += 1
            if bad_rt <= 3:
                print(f"  RT mismatch {e[1]}: {len(raw)} vs {len(ct)} firstdiff="
                      f"{next((i for i in range(min(len(raw), len(ct))) if raw[i] != ct[i]), -1)}")
    print(f"RC4 round-trip: {ok_rt} ok, {bad_rt} mismatch")

    m = rc4.decrypt(ole.openstream(["DataStg", "Strm_0"]).read())
    calc = (sum(m[:-2]) + 0xAAAB) & 0xFFFF
    stored = be16(m, len(m) - 2)
    print(f"map checksum: stored=0x{stored:04X} calc=0x{calc:04X} {'OK' if calc == stored else 'FAIL'}")

    bad_len = 0
    bad_csum = 0
    n = 0
    mp = {}
    for o in range(0, len(m) - 2 - 9, 10):
        mp[be32(m, o)] = (be16(m, o + 4), be16(m, o + 6), be16(m, o + 8))
    for e in entries:
        if e[1] == "Strm_0":
            continue
        d = rc4.decrypt(ole.openstream(e).read())
        n += 1
        length = be16(d, 4)
        if length != len(d) - 6:
            bad_len += 1
        sid = be32(d, 0)
        if mp.get(sid, (None, None, None))[1] != be16(d, len(d) - 2):
            bad_csum += 1
    print(f"records={n} bad_len={bad_len} bad_csum={bad_csum}")
    print(f"header[0x44] checksum byte = 0x{hdr[0x44]:02X}")

    # header checksum using file bytes only (vftable contributes 4 unknown bytes)
    a = sum(hdr[0:0x40]) & 0xFF
    print(f"sum(file[0:0x40])=0x{a:02X}; need (0x5A - x) = 0x{hdr[0x44]:02X} -> x=0x{(0x5A - hdr[0x44]) & 0xFF:02X}")
    ole.close()


if __name__ == "__main__":
    main()
