#!/usr/bin/env python3
"""Export the CALG_RC4 key that Microsoft CryptDeriveKey produces for the ADK
seed, then compare it with the raw MD5 digest to determine the derivation."""
import sys
import ctypes
from ctypes import wintypes
import hashlib
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
advapi32.CryptExportKey.argtypes = [HCRYPTKEY, HCRYPTKEY, wintypes.DWORD, wintypes.DWORD,
                                    ctypes.c_char_p, ctypes.POINTER(wintypes.DWORD)]
advapi32.CryptDestroyKey.argtypes = [HCRYPTKEY]
advapi32.CryptDestroyHash.argtypes = [HCRYPTHASH]
advapi32.CryptReleaseContext.argtypes = [HCRYPTPROV, wintypes.DWORD]

PROV_RSA_FULL = 1
CRYPT_VERIFYCONTEXT = 0xF0000000
CRYPT_EXPORTABLE = 0x00000001
CALG_MD5 = 0x8003
CALG_RC4 = 0x6801
PLAINTEXTKEYBLOB = 8


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
    ln = hdr[0x239]
    seed = adk_decrypt(hdr[0x23A:0x23A + ln])
    print(f"seed = {seed.hex()} = {seed!r}")
    md5 = hashlib.md5(seed[:9]).digest()
    print(f"MD5(seed) = {md5.hex()}")

    prov = HCRYPTPROV()
    advapi32.CryptAcquireContextA(ctypes.byref(prov), None,
                                  b"Microsoft Base Cryptographic Provider v1.0",
                                  PROV_RSA_FULL, CRYPT_VERIFYCONTEXT)
    h = HCRYPTHASH()
    advapi32.CryptCreateHash(prov, CALG_MD5, None, 0, ctypes.byref(h))
    advapi32.CryptHashData(h, seed, 9, 0)
    k = HCRYPTKEY()
    if not advapi32.CryptDeriveKey(prov, CALG_RC4, h, CRYPT_EXPORTABLE, ctypes.byref(k)):
        raise OSError(ctypes.get_last_error(), "CryptDeriveKey")
    n = wintypes.DWORD(0)
    if not advapi32.CryptExportKey(k, None, PLAINTEXTKEYBLOB, 0, None, ctypes.byref(n)):
        raise OSError(ctypes.get_last_error(), "CryptExportKey size")
    buf = ctypes.create_string_buffer(n.value)
    if not advapi32.CryptExportKey(k, None, PLAINTEXTKEYBLOB, 0, buf, ctypes.byref(n)):
        raise OSError(ctypes.get_last_error(), "CryptExportKey")
    blob = buf.raw[:n.value]
    # BLOBHEADER(8 bytes: bType, bVersion, reserved(2), aiKeyAlg(4)) + DWORD cbKeySize
    alg = int.from_bytes(blob[4:8], "little")
    cbsize = int.from_bytes(blob[8:12], "little")
    key = blob[12:12 + cbsize]
    print(f"exported alg=0x{alg:08X} keylen={cbsize} key={key.hex()}")

    ct = ole.openstream(["DataStg", "Strm_0"]).read()
    pt_csp = None
    # use CSP decrypt to confirm, then pure rc4
    advapi32.CryptDecrypt.argtypes = [HCRYPTKEY, ctypes.c_void_p, wintypes.BOOL, wintypes.DWORD,
                                      ctypes.c_char_p, ctypes.POINTER(wintypes.DWORD)]
    # need a fresh key for decrypt because export key is separate
    k2 = HCRYPTKEY()
    advapi32.CryptDeriveKey(prov, CALG_RC4, h, 0, ctypes.byref(k2))
    b2 = ctypes.create_string_buffer(ct, len(ct))
    n2 = wintypes.DWORD(len(ct))
    advapi32.CryptDecrypt(k2, None, True, 0, b2, ctypes.byref(n2))
    pt_csp = b2.raw[:n2.value]
    print(f"CSP Strm_0[0:16] = {pt_csp[:16].hex()}")
    print(f"pure RC4(key)[0:16] = {rc4(key, ct)[:16].hex()}")
    print("pure==CSP:", rc4(key, ct) == pt_csp)
    ole.close()


if __name__ == "__main__":
    main()
