#!/usr/bin/env python3
"""Validate the reconstructed ADK custom cipher by re-encrypting the 9-byte
seed and reproducing the stored 18-byte blob byte-for-byte.
"""
import sys
import olefile
from adkcrypt import decrypt as adk_decrypt


def nxt(s):
    return (s * 0x55 + 0x19) & 0xFF


def encrypt_with_seed(s0, plaintext, header_out):
    """Reproduce CFhAdkEncrypt::Encrypt given an initial PRNG value s0."""
    s = s0 & 0xFF
    out = bytearray(9)
    for i in range(9):
        s = nxt(s)
        out[i] = s
    s = nxt(s); out[5] = s; b1 = s
    s = nxt(s); out[6] = s; b2 = s
    s = nxt(s); out[8] = s
    seed = ((b2 + b1) & 0xFF) - out[8]
    s = seed & 0xFF
    s = nxt(s); ks = s
    header_out.extend(out)
    pos = 9
    for ch in plaintext:
        c = (ks ^ ch) & 0xFF
        if c in (0x1A, 0x1B):
            out.append(0x1B)
            if c == 0x1B:
                saved = s
                while True:
                    s = nxt(s)
                    if s not in (0xE5, 0x1B, 0x1A):
                        break
                c = s
                s = saved
            else:
                c = 0xE5
        out.append(c)
        s = nxt(s)
        ks = (ks + s + ch) & 0xFF
    return bytes(out)


def main():
    path = sys.argv[1]
    ole = olefile.OleFileIO(path)
    hdr = ole.openstream(["AdkFileHeader"]).read()
    ln = hdr[0x239]
    blob = hdr[0x23A:0x23A + ln]
    seed = adk_decrypt(blob)
    print(f"blob  = {blob.hex()}")
    print(f"seed  = {seed.hex()} ({len(seed)}B)")
    target_header = blob[:9]
    found = None
    for s0 in range(256):
        h = bytearray()
        enc = encrypt_with_seed(s0, seed, h)
        if bytes(h) == target_header:
            found = (s0, enc)
            break
    if found is None:
        print("No initial PRNG seed reproduced the header -> cipher model wrong")
        ole.close()
        return
    s0, enc = found
    print(f"initial PRNG seed s0={s0:02X}")
    print(f"reencrypted = {enc.hex()}")
    print(f"blob        = {blob.hex()}")
    print("EXACT MATCH" if enc == blob else "MISMATCH")
    ole.close()


if __name__ == "__main__":
    main()
