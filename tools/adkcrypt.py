#!/usr/bin/env python3
"""Clean-room implementation of the ADK CFhAdkEncrypt stream cipher.

Derived from CFhAdkEncrypt::Encrypt/Decrypt/encryptChar/decryptChar and
randomGenerator:
    randomGenerator: state = (state * 0x55 + 0x19) & 0xFF

Ciphertext layout: 9-byte header + escaped XOR stream.
Header bytes 5,6,8 seed the PRNG; header byte 3 (K.byte2) participates.
"""
import sys
import olefile


def _next(s):
    return (s * 0x55 + 0x19) & 0xFF


def decrypt(data: bytes) -> bytes:
    K = 0            # 4-byte keystream register (DAT_100127f4)
    state = 0        # DAT_100127f8
    esc = 0          # DAT_100127fc
    f3 = 0           # DAT_100127f3
    prng = 0
    out = bytearray()
    first = True
    for ch in data:
        if first:
            state = 0
            esc = 0
            f3 = 0
            K = 0
            first = False
        if state > 8:
            if esc:
                esc = 0
                plain = 0x1A if ch == 0xE5 else 0x1B
            elif ch == 0x1B:
                esc = 1
                continue
            else:
                plain = (ch ^ ((K >> 24) & 0xFF)) & 0xFF
            r = _next(prng)
            prng = r
            K = (K & 0xFFFF00FF) | (r << 8)
            K = (K & 0x00FFFFFF) | (((plain + r + ((K >> 24) & 0xFF)) & 0xFF) << 24)
            out.append(plain)
            continue
        if state == 5:
            K = (K & 0xFF00FFFF) | (ch << 16)
            state += 1
        elif state == 6:
            state = 7
            f3 = ch
        elif state == 8:
            u = (((f3 + ((K >> 16) & 0xFF)) & 0xFF) - ch) & 0xFF
            K = (K & 0xFFFFFF00) | u
            prng = _next(u)
            K = (K & 0x00FFFFFF) | (prng << 24)
            state += 1
        else:
            state += 1
    return bytes(out)


def main():
    path = sys.argv[1]
    names = sys.argv[2:] or ["DataStg/Strm_0"]
    ole = olefile.OleFileIO(path)
    try:
        for name in names:
            data = ole.openstream(name.split("/")).read()
            pt = decrypt(data)
            print(f"=== {name} cipher={len(data)} plain={len(pt)} ===")
            n = min(len(pt), 256)
            for off in range(0, n, 16):
                chunk = pt[off:off + 16]
                hexs = " ".join(f"{b:02X}" for b in chunk).ljust(47)
                text = "".join(chr(b) if 32 <= b < 127 else "." for b in chunk)
                print(f"{off:08X}  {hexs}  |{text}|")
            print()
    finally:
        ole.close()


if __name__ == "__main__":
    main()
