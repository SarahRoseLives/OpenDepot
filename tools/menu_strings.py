#!/usr/bin/env python3
"""Dump UTF-16 strings from PatPortDepot RT_MENU resources with nearby IDs."""
import struct

EXE = r"C:\Program Files (x86)\Motorola\ASTRO 25 Products\ASTRO 25 Portable Depot\PatPortDepot.exe"
MENUS = [
    (129, 24413280, 1200), (156, 24414480, 84), (164, 24414564, 344),
    (165, 24414908, 1566), (171, 24416476, 48), (181, 24416524, 228),
    (182, 24416752, 86), (191, 24416840, 294), (192, 24417136, 930),
    (194, 24418068, 106), (195, 24418176, 90), (196, 24418268, 114),
    (197, 24418384, 86), (520, 24418472, 2228), (521, 24420700, 16),
    (522, 24420716, 122), (524, 24420840, 1286), (1030, 24422128, 4198),
    (1217, 24426328, 104), (2166, 24426432, 60),
]


def rd16(b, o):
    return b[o] | (b[o + 1] << 8)


def rd32(b, o):
    return b[o] | (b[o + 1] << 8) | (b[o + 2] << 16) | (b[o + 3] << 24)


def u16z(b, o):
    e = o
    while e + 1 < len(b) and not (b[e] == 0 and b[e + 1] == 0):
        e += 2
    try:
        s = b[o:e].decode("utf-16le")
    except Exception:
        s = ""
    return s, e + 2


def main():
    data = open(EXE, "rb").read()
    for name, off, size in MENUS:
        b = data[off:off + size]
        if len(b) < 4:
            continue
        ver = rd16(b, 0)
        hdr = rd16(b, 2)
        print(f"===== menu {name} ver={ver} hdr={hdr} =====")
        if ver == 1:
            o = hdr
            while o + 14 <= len(b):
                t = rd32(b, o); o += 4
                st = rd32(b, o); o += 4
                mid = rd32(b, o); o += 4
                fl = rd16(b, o); o += 2
                s, o = u16z(b, o)
                o = (o + 3) & ~3
                if s:
                    print(f"  [id={mid} type=0x{t:X} flags=0x{fl:X}] {s!r}")
                if o >= len(b):
                    break
        else:
            o = 4
            while o + 2 <= len(b):
                s, e = u16z(b, o)
                if s and all(ch.isprintable() for ch in s):
                    id2 = rd16(b, o - 2) if o >= 2 else -1
                    id4 = rd16(b, o - 4) if o >= 4 else -1
                    print(f"  [prev2={id2} prev4={id4}] {s!r}")
                    o = e
                else:
                    o += 2


if __name__ == "__main__":
    main()
