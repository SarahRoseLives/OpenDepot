#!/usr/bin/env python3
"""Correctly parse standard RT_MENU templates from PatPortDepot.exe."""
import sys

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
sys.stdout.reconfigure(encoding="utf-8", errors="replace")


def u16(b, o):
    return b[o] | (b[o + 1] << 8)


def u16z(b, o):
    e = o
    while e + 1 < len(b) and not (b[e] == 0 and b[e + 1] == 0):
        e += 2
    return b[o:e].decode("utf-16le", errors="replace"), e + 2


def parse(b, o, depth, out):
    guard = 0
    while o + 2 <= len(b):
        guard += 1
        if guard > 5000:
            return o
        flags = u16(b, o)
        o += 2
        if flags == 0:
            return o
        popup = bool(flags & 0x10)
        mid = None
        if not popup:
            if o + 2 > len(b):
                return o
            mid = u16(b, o)
            o += 2
        s, o = u16z(b, o)
        out.append((depth, mid, flags, s))
        if popup:
            o = parse(b, o, depth + 1, out)
    return o


def main():
    data = open(EXE, "rb").read()
    for name, off, size in MENUS:
        b = data[off:off + size]
        if len(b) < 4 or u16(b, 0) != 0:
            continue
        out = []
        parse(b, 4, 0, out)
        interesting = [x for x in out if any(k in x[3].lower() for k in
                       ("create", "codeplug", "new", "package", "feature", "device", "read", "write"))]
        if interesting:
            print(f"===== menu {name} =====")
            for depth, mid, flags, s in out:
                mark = "  <--" if any(k in s.lower() for k in ("create", "codeplug", "package")) else ""
                print(f"  {'  '*depth}[id={mid}] '{s}'{mark}")


if __name__ == "__main__":
    main()
