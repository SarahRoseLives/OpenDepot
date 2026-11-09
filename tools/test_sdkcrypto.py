#!/usr/bin/env python3
"""Test the ADK SDK-crypto (RC4) path used by the .cpg data streams."""
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


def main():
    path = sys.argv[1]
    ole = olefile.OleFileIO(path)
    hdr = ole.openstream(["AdkFileHeader"]).read()
    print(f"header stream: {len(hdr)} bytes")
    # object = file + 4  => length byte at object 0x23D = file 0x239
    ln = hdr[0x239]
    blob = hdr[0x23A:0x23A + ln]
    print(f"encryption flag file[8]=0x{hdr[8]:02X} -> type=0x{(hdr[8] & 0x18) >> 3}")
    print(f"sdk blob len byte hdr[0x239]={ln}, blob={blob.hex()}")
    seed = adk_decrypt(blob)
    print(f"custom-decrypt(blob) -> {len(seed)} bytes: {seed.hex()}")
    if len(seed) >= 9:
        key = hashlib.md5(seed[:9]).digest()
        print(f"MD5(seed[0:9]) = {key.hex()}")
        for name in ["DataStg/Strm_0", "DataStg/Strm_1", "DataStg/Strm_2"]:
            ct = ole.openstream(name.split("/")).read()
            pt = rc4(key, ct)
            print(f"\n=== {name}: ct={len(ct)} pt={len(pt)} ===")
            n = min(len(pt), 96)
            for off in range(0, n, 16):
                chunk = pt[off:off + 16]
                hexs = " ".join(f"{b:02X}" for b in chunk).ljust(47)
                text = "".join(chr(b) if 32 <= b < 127 else "." for b in chunk)
                print(f"{off:08X}  {hexs}  |{text}|")
    ole.close()


if __name__ == "__main__":
    main()
