#!/usr/bin/env python3
"""Identify the Microsoft CALG_RC4 variant by comparing keystreams."""
import sys
import ctypes
from ctypes import wintypes
import hashlib
import olefile
from adkcrypt import decrypt as adk_decrypt

advapi32 = ctypes.WinDLL("advapi32", use_last_error=True)
HP = ctypes.c_void_p
advapi32.CryptAcquireContextA.argtypes = [ctypes.POINTER(HP), ctypes.c_char_p, ctypes.c_char_p, wintypes.DWORD, wintypes.DWORD]
advapi32.CryptCreateHash.argtypes = [HP, wintypes.DWORD, ctypes.c_void_p, wintypes.DWORD, ctypes.POINTER(HP)]
advapi32.CryptHashData.argtypes = [HP, ctypes.c_char_p, wintypes.DWORD, wintypes.DWORD]
advapi32.CryptGetHashParam.argtypes = [HP, wintypes.DWORD, ctypes.c_char_p, ctypes.POINTER(wintypes.DWORD), wintypes.DWORD]
advapi32.CryptDeriveKey.argtypes = [HP, wintypes.DWORD, HP, wintypes.DWORD, ctypes.POINTER(HP)]
advapi32.CryptDecrypt.argtypes = [HP, ctypes.c_void_p, wintypes.BOOL, wintypes.DWORD, ctypes.c_char_p, ctypes.POINTER(wintypes.DWORD)]
advapi32.CryptImportKey.argtypes = [HP, ctypes.c_char_p, wintypes.DWORD, HP, wintypes.DWORD, ctypes.POINTER(HP)]


def ksa_variant(key, mode):
    S = list(range(256)); j = 0; kl = len(key)
    if mode == "standard":
        for i in range(256):
            j = (j + S[i] + key[i % kl]) & 0xFF; S[i], S[j] = S[j], S[i]
    elif mode == "pluslen":
        for i in range(256):
            j = (j + S[i] + key[i % kl] + kl) & 0xFF; S[i], S[j] = S[j], S[i]
    elif mode == "onebased":
        for i in range(1, 257):
            j = (j + S[i & 0xFF] + key[(i - 1) % kl]) & 0xFF; S[i & 0xFF], S[j] = S[j], S[i & 0xFF]
    out = bytearray(); i = j = 0
    for _ in range(32):
        i = (i + 1) & 0xFF; j = (j + S[i]) & 0xFF; S[i], S[j] = S[j], S[i]
        out.append(S[(S[i] + S[j]) & 0xFF])
    return bytes(out)


def main():
    path = sys.argv[1]
    ole = olefile.OleFileIO(path)
    hdr = ole.openstream(["AdkFileHeader"]).read()
    seed = adk_decrypt(hdr[0x23A:0x23A + hdr[0x239]])
    ct = ole.openstream(["DataStg", "Strm_0"]).read()

    prov = HP()
    advapi32.CryptAcquireContextA(ctypes.byref(prov), None, b"Microsoft Base Cryptographic Provider v1.0", 1, 0xF0000000)
    h = HP(); advapi32.CryptCreateHash(prov, 0x8003, None, 0, ctypes.byref(h))
    advapi32.CryptHashData(h, seed, 9, 0)
    n = wintypes.DWORD(20)
    buf = ctypes.create_string_buffer(20)
    advapi32.CryptGetHashParam(h, 2, buf, ctypes.byref(n), 0)  # HP_HASHVAL=2
    print("CSP MD5 =", buf.raw[:n.value].hex())
    print("hasher  =", hashlib.md5(seed[:9]).hexdigest())

    k = HP(); advapi32.CryptDeriveKey(prov, 0x6801, h, 0, ctypes.byref(k))
    b = ctypes.create_string_buffer(ct, len(ct)); nn = wintypes.DWORD(len(ct))
    advapi32.CryptDecrypt(k, None, True, 0, b, ctypes.byref(nn))
    ks = bytes(a ^ c for a, c in zip(ct, b.raw[:nn.value]))
    print("CSP keystream[0:32] =", ks[:32].hex())

    md5 = hashlib.md5(seed[:9]).digest()
    key5 = md5[:5]
    variants = {
        "std(md5[:5])": (key5, "standard"),
        "rev(md5[:5])": (key5[::-1], "standard"),
        "std(md5[:5]+0)": (key5 + b"\x00", "standard"),
        "std(0+md5[:5])": (b"\x00" + key5, "standard"),
        "pluslen(md5[:5])": (key5, "pluslen"),
        "onebased(md5[:5])": (key5, "onebased"),
        "std(md5)": (md5, "standard"),
    }
    for name, (kk, mode) in variants.items():
        got = ksa_variant(kk, mode)
        print(f"{name:20} {got.hex()} {'<== MATCH' if got == ks[:32] else ''}")
    ole.close()


if __name__ == "__main__":
    main()
