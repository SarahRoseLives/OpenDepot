#!/usr/bin/env python3
"""Compare Microsoft CALG_RC4 keystream with textbook RC4 to identify the variant."""
import sys
import ctypes
from ctypes import wintypes
import hashlib
import olefile
from adkcrypt import decrypt as adk_decrypt


def rc4_ks(key, n):
    S = list(range(256))
    j = 0
    for i in range(256):
        j = (j + S[i] + key[i % len(key)]) & 0xFF
        S[i], S[j] = S[j], S[i]
    out = bytearray()
    i = j = 0
    for _ in range(n):
        i = (i + 1) & 0xFF
        j = (j + S[i]) & 0xFF
        S[i], S[j] = S[j], S[i]
        out.append(S[(S[i] + S[j]) & 0xFF])
    return bytes(out)


def main():
    # sanity: standard RC4 test vector
    key = b"Key"
    S = list(range(256)); j = 0
    for i in range(256):
        j = (j + S[i] + key[i % len(key)]) & 0xFF; S[i], S[j] = S[j], S[i]
    out = bytearray(); i = j = 0
    for b in b"Plaintext":
        i = (i + 1) & 0xFF; j = (j + S[i]) & 0xFF; S[i], S[j] = S[j], S[i]
        out.append(b ^ S[(S[i] + S[j]) & 0xFF])
    print("RC4('Key','Plaintext') =", out.hex(), "(expect bbf316e8d940af0ad3)")

    path = sys.argv[1]
    ole = olefile.OleFileIO(path)
    hdr = ole.openstream(["AdkFileHeader"]).read()
    seed = adk_decrypt(hdr[0x23A:0x23A + hdr[0x239]])
    md5 = hashlib.md5(seed[:9]).digest()
    ct = ole.openstream(["DataStg", "Strm_0"]).read()

    advapi32 = ctypes.WinDLL("advapi32", use_last_error=True)
    HCRYPTPROV = ctypes.c_void_p; HCRYPTKEY = ctypes.c_void_p; HCRYPTHASH = ctypes.c_void_p
    advapi32.CryptAcquireContextA.argtypes = [ctypes.POINTER(HCRYPTPROV), ctypes.c_char_p, ctypes.c_char_p, wintypes.DWORD, wintypes.DWORD]
    advapi32.CryptCreateHash.argtypes = [HCRYPTPROV, wintypes.DWORD, ctypes.c_void_p, wintypes.DWORD, ctypes.POINTER(HCRYPTHASH)]
    advapi32.CryptHashData.argtypes = [HCRYPTHASH, ctypes.c_char_p, wintypes.DWORD, wintypes.DWORD]
    advapi32.CryptDeriveKey.argtypes = [HCRYPTPROV, wintypes.DWORD, HCRYPTHASH, wintypes.DWORD, ctypes.POINTER(HCRYPTKEY)]
    advapi32.CryptDecrypt.argtypes = [HCRYPTKEY, ctypes.c_void_p, wintypes.BOOL, wintypes.DWORD, ctypes.c_char_p, ctypes.POINTER(wintypes.DWORD)]
    prov = HCRYPTPROV()
    advapi32.CryptAcquireContextA(ctypes.byref(prov), None, b"Microsoft Base Cryptographic Provider v1.0", 1, 0xF0000000)
    h = HCRYPTHASH(); advapi32.CryptCreateHash(prov, 0x8003, None, 0, ctypes.byref(h))
    advapi32.CryptHashData(h, seed, 9, 0)
    k = HCRYPTKEY(); advapi32.CryptDeriveKey(prov, 0x6801, h, 0, ctypes.byref(k))
    buf = ctypes.create_string_buffer(ct, len(ct)); n = wintypes.DWORD(len(ct))
    advapi32.CryptDecrypt(k, None, True, 0, buf, ctypes.byref(n))
    pt = buf.raw[:n.value]
    ks_csp = bytes(a ^ b for a, b in zip(ct, pt))
    for name, kk in [("md5[:5]", md5[:5]), ("md5[16]", md5), ("md5[:8]", md5[:8]),
                     ("seed", seed[:9]), ("md5[-5:]", md5[-5:])]:
        ks = rc4_ks(kk, 64)
        print(f"std RC4 {name:10} ks={ks[:24].hex()}")
    print(f"CSP              ks={ks_csp[:24].hex()}")
    ole.close()


if __name__ == "__main__":
    main()
