#!/usr/bin/env python3
"""Decode an ASTRO 25 .cpg codeplug into its plaintext DataStg records.

Reconstructs the ADK container:
  - AdkFileHeader (800 bytes): version/model/guid/serial, encryption type in byte 8
  - encryption types: 0 = custom ADK cipher, 1 = Windows CryptoAPI RC4, 2 = none
  - type 1: 18-byte key blob at header[0x23A] is custom-decrypted to a 9-byte seed;
    MD5(seed) -> CryptDeriveKey(CALG_RC4) -> each DataStg stream RC4-decrypted
  - Strm_0 is the stream map: 10-byte entries (BE32 id, 3 BE16) + 2-byte checksum
  - each data stream = BE32 id + payload + BE16 checksum (== map entry field 2)

Read-only. Writes decrypted streams and a manifest into an output directory.
"""
import sys
import os
import json
import hashlib
import ctypes
from ctypes import wintypes
import olefile
from adkcrypt import decrypt as adk_decrypt

advapi32 = ctypes.WinDLL("advapi32", use_last_error=True)
HP = ctypes.c_void_p
advapi32.CryptAcquireContextA.argtypes = [ctypes.POINTER(HP), ctypes.c_char_p, ctypes.c_char_p, wintypes.DWORD, wintypes.DWORD]
advapi32.CryptCreateHash.argtypes = [HP, wintypes.DWORD, ctypes.c_void_p, wintypes.DWORD, ctypes.POINTER(HP)]
advapi32.CryptHashData.argtypes = [HP, ctypes.c_char_p, wintypes.DWORD, wintypes.DWORD]
advapi32.CryptDeriveKey.argtypes = [HP, wintypes.DWORD, HP, wintypes.DWORD, ctypes.POINTER(HP)]
advapi32.CryptDecrypt.argtypes = [HP, ctypes.c_void_p, wintypes.BOOL, wintypes.DWORD, ctypes.c_char_p, ctypes.POINTER(wintypes.DWORD)]
advapi32.CryptEncrypt.argtypes = [HP, ctypes.c_void_p, wintypes.BOOL, wintypes.DWORD, ctypes.c_char_p, ctypes.POINTER(wintypes.DWORD), wintypes.DWORD]
advapi32.CryptDestroyKey.argtypes = [HP]

PROV_RSA_FULL = 1
CRYPT_VERIFYCONTEXT = 0xF0000000
CALG_MD5 = 0x8003
CALG_RC4 = 0x6801


class Rc4:
    def __init__(self, seed9):
        self.prov = HP()
        if not advapi32.CryptAcquireContextA(ctypes.byref(self.prov), None,
                                             b"Microsoft Base Cryptographic Provider v1.0",
                                             PROV_RSA_FULL, CRYPT_VERIFYCONTEXT):
            raise OSError(ctypes.get_last_error(), "CryptAcquireContext")
        self.h = HP()
        advapi32.CryptCreateHash(self.prov, CALG_MD5, None, 0, ctypes.byref(self.h))
        advapi32.CryptHashData(self.h, seed9, len(seed9), 0)

    def key(self):
        k = HP()
        if not advapi32.CryptDeriveKey(self.prov, CALG_RC4, self.h, 0, ctypes.byref(k)):
            raise OSError(ctypes.get_last_error(), "CryptDeriveKey")
        return k

    def decrypt(self, data):
        k = self.key()
        buf = ctypes.create_string_buffer(bytes(data), len(data))
        n = wintypes.DWORD(len(data))
        if not advapi32.CryptDecrypt(k, None, True, 0, buf, ctypes.byref(n)):
            raise OSError(ctypes.get_last_error(), "CryptDecrypt")
        advapi32.CryptDestroyKey(k)
        return buf.raw[:n.value]

    def encrypt(self, data):
        k = self.key()
        # get required size
        n = wintypes.DWORD(len(data))
        advapi32.CryptEncrypt(k, None, True, 0, None, ctypes.byref(n), 0)
        buf = ctypes.create_string_buffer(bytes(data), len(data))
        if not advapi32.CryptEncrypt(k, None, True, 0, buf, ctypes.byref(n), len(data)):
            raise OSError(ctypes.get_last_error(), "CryptEncrypt")
        advapi32.CryptDestroyKey(k)
        return buf.raw[:n.value]


def be16(b, o=0):
    return (b[o] << 8) | b[o + 1]


def be32(b, o=0):
    return (b[o] << 24) | (b[o + 1] << 16) | (b[o + 2] << 8) | b[o + 3]


def decode(path, outdir=None):
    ole = olefile.OleFileIO(path)
    hdr = ole.openstream(["AdkFileHeader"]).read()
    version = hdr[0:6].decode("latin1")
    flag = hdr[8]
    etype = (flag & 0x18) >> 3
    adktype = flag & 7
    def cstr(off, maxlen):
        end = hdr.find(b"\0", off, off + maxlen)
        if end < 0:
            end = off + maxlen
        return hdr[off:end].decode("latin1")
    model = cstr(0x1C, 20)
    serial = cstr(0x30, 20)
    guid = hdr[0x0C:0x1C].hex()
    print(f"file       : {os.path.basename(path)}")
    print(f"version    : {version}  adktype={adktype}  encType={etype}  flag=0x{flag:02X}")
    print(f"model      : {model}")
    print(f"serial     : {serial}")
    print(f"guid       : {guid}")

    rc4 = None
    if etype == 1:
        ln = hdr[0x239]
        seed = adk_decrypt(hdr[0x23A:0x23A + ln])
        print(f"crypto seed: {seed!r}")
        rc4 = Rc4(seed[:9])

    def read(fullpath):
        raw = ole.openstream(fullpath.split("/")).read()
        if etype == 1:
            return rc4.decrypt(raw)
        if etype == 0:
            return adk_decrypt(raw)
        return raw

    entries = [e for e in ole.listdir() if e[0] == "DataStg" and len(e) == 2]
    entries.sort(key=lambda e: int(e[1].split("_")[1]))
    streams = {}
    for e in entries:
        streams[e[1]] = read("/".join(e))

    m = streams["Strm_0"]
    mp = {}
    order = []
    for o in range(0, len(m) - 2 - 9, 10):
        sid = be32(m, o)
        v1, v2, v3 = be16(m, o + 4), be16(m, o + 6), be16(m, o + 8)
        mp[sid] = (v1, v2, v3)
        order.append((sid, v1, v2, v3))
    tail = bytes(m[len(m) - 2:])
    print(f"map entries: {len(order)}  trailing checksum: {tail.hex()}")

    good = bad = 0
    manifest = []
    for name, data in streams.items():
        if name == "Strm_0":
            continue
        sid = be32(data, 0)
        csum = be16(data, len(data) - 2)
        info = mp.get(sid)
        ok = info is not None and info[1] == csum
        if ok:
            good += 1
        else:
            bad += 1
            if bad <= 8:
                print(f"  BAD {name}: id=0x{sid:08X} csum=0x{csum:04X} map={info}")
        manifest.append({"stream": name, "id": f"0x{sid:08X}", "size": len(data),
                         "v1": info[0] if info else None, "v2": info[1] if info else None,
                         "v3": info[2] if info else None, "verified": ok})
    print(f"stream verification: {good} ok, {bad} bad")

    if outdir:
        os.makedirs(outdir, exist_ok=True)
        for name, data in streams.items():
            with open(os.path.join(outdir, name + ".bin"), "wb") as f:
                f.write(data)
        with open(os.path.join(outdir, "manifest.json"), "w", encoding="utf-8") as f:
            json.dump({"model": model, "serial": serial, "guid": guid,
                       "adktype": adktype, "encType": etype, "map": manifest}, f, indent=1)
        print(f"wrote {len(streams)} streams to {outdir}")
    ole.close()
    return streams


if __name__ == "__main__":
    if len(sys.argv) < 2:
        print("usage: cpg_decrypt.py <file.cpg> [outdir]")
        sys.exit(2)
    decode(sys.argv[1], sys.argv[2] if len(sys.argv) > 2 else None)
