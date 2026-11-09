#!/usr/bin/env python3
"""Use the real Windows CryptoAPI (same provider as CFhAdkSdkCrypto) to test
whether .cpg DataStg streams are CALG_RC4-encrypted with a CALG_MD5-derived key.
"""
import sys
import ctypes
from ctypes import wintypes
import olefile
from adkcrypt import decrypt as adk_decrypt

advapi32 = ctypes.WinDLL("advapi32", use_last_error=True)
HCRYPTPROV = ctypes.c_void_p
HCRYPTKEY = ctypes.c_void_p
HCRYPTHASH = ctypes.c_void_p

advapi32.CryptAcquireContextA.argtypes = [ctypes.POINTER(HCRYPTPROV), ctypes.c_char_p,
                                          ctypes.c_char_p, wintypes.DWORD, wintypes.DWORD]
advapi32.CryptCreateHash.argtypes = [HCRYPTPROV, wintypes.DWORD, ctypes.c_void_p,
                                     wintypes.DWORD, ctypes.POINTER(HCRYPTHASH)]
advapi32.CryptHashData.argtypes = [HCRYPTHASH, ctypes.c_char_p, wintypes.DWORD, wintypes.DWORD]
advapi32.CryptDeriveKey.argtypes = [HCRYPTPROV, wintypes.DWORD, HCRYPTHASH,
                                    wintypes.DWORD, ctypes.POINTER(HCRYPTKEY)]
advapi32.CryptDecrypt.argtypes = [HCRYPTKEY, ctypes.c_void_p, wintypes.BOOL, wintypes.DWORD,
                                  ctypes.c_char_p, ctypes.POINTER(wintypes.DWORD)]
advapi32.CryptDestroyKey.argtypes = [HCRYPTKEY]
advapi32.CryptDestroyHash.argtypes = [HCRYPTHASH]
advapi32.CryptReleaseContext.argtypes = [HCRYPTPROV, wintypes.DWORD]

PROV_RSA_FULL = 1
CRYPT_VERIFYCONTEXT = 0xF0000000
CALG_MD5 = 0x8003
CALG_RC4 = 0x6801


class SdkCrypto:
    def __init__(self, seed9):
        self.prov = HCRYPTPROV()
        if not advapi32.CryptAcquireContextA(ctypes.byref(self.prov), None,
                                             b"Microsoft Base Cryptographic Provider v1.0",
                                             PROV_RSA_FULL, CRYPT_VERIFYCONTEXT):
            raise OSError(ctypes.get_last_error(), "CryptAcquireContext")
        h = HCRYPTHASH()
        if not advapi32.CryptCreateHash(self.prov, CALG_MD5, None, 0, ctypes.byref(h)):
            raise OSError(ctypes.get_last_error(), "CryptCreateHash")
        if not advapi32.CryptHashData(h, seed9, len(seed9), 0):
            raise OSError(ctypes.get_last_error(), "CryptHashData")
        self.hash = h
        self.keys = []

    def new_key(self):
        k = HCRYPTKEY()
        if not advapi32.CryptDeriveKey(self.prov, CALG_RC4, self.hash, 0, ctypes.byref(k)):
            raise OSError(ctypes.get_last_error(), "CryptDeriveKey")
        self.keys.append(k)
        return k

    def decrypt(self, key, data):
        buf = ctypes.create_string_buffer(bytes(data), len(data))
        n = wintypes.DWORD(len(data))
        if not advapi32.CryptDecrypt(key, None, True, 0, buf, ctypes.byref(n)):
            raise OSError(ctypes.get_last_error(), "CryptDecrypt")
        return buf.raw[:n.value]


def be16(b, o=0):
    return (b[o] << 8) | b[o + 1]


def be32(b, o=0):
    return (b[o] << 24) | (b[o + 1] << 16) | (b[o + 2] << 8) | b[o + 3]


def main():
    path = sys.argv[1]
    ole = olefile.OleFileIO(path)
    hdr = ole.openstream(["AdkFileHeader"]).read()
    ln = hdr[0x239]
    blob = hdr[0x23A:0x23A + ln]
    seed = adk_decrypt(blob)
    print(f"seed = {seed.hex()} ({len(seed)}B)")
    c = SdkCrypto(seed[:9])
    entries = [e for e in ole.listdir() if e[0] == "DataStg" and len(e) == 2]
    entries.sort(key=lambda e: int(e[1].split("_")[1]))
    data = {e[1]: ole.openstream(e).read() for e in entries}

    # Recreate key per stream (per-stream deterministic)
    m = c.decrypt(c.new_key(), data["Strm_0"])
    print(f"Strm_0 plain {len(m)} bytes: {m[:32].hex()}")
    mp = {}
    for o in range(0, len(m) - 2 - 9, 10):
        mp[be32(m, o)] = be16(m, o + 4)
    good = 0
    total = 0
    for name, ct in data.items():
        if name == "Strm_0":
            continue
        total += 1
        d = c.decrypt(c.new_key(), ct)
        if len(d) >= 6 and mp.get(be32(d, 0)) == be16(d, len(d) - 2):
            good += 1
    print(f"per-stream key: matches {good}/{total}")

    # Reuse one key across streams (continuous keystream)
    k = c.new_key()
    mp2 = {}
    m2 = c.decrypt(k, data["Strm_0"])
    for o in range(0, len(m2) - 2 - 9, 10):
        mp2[be32(m2, o)] = be16(m2, o + 4)
    good2 = 0
    order = sorted(data, key=lambda n: int(n.split('_')[1]))
    for name in order:
        if name == "Strm_0":
            continue
        d = c.decrypt(k, data[name])
        if len(d) >= 6 and mp2.get(be32(d, 0)) == be16(d, len(d) - 2):
            good2 += 1
    print(f"reused key: matches {good2}/{total}")

    print("\nStrm_0 map sample:")
    for i in range(8):
        o = i * 10
        print(f"  id=0x{be32(m,o):08X} v1=0x{be16(m,o+4):04X} v2=0x{be16(m,o+6):04X} v3=0x{be16(m,o+8):04X}")
    ole.close()


if __name__ == "__main__":
    main()
