#!/usr/bin/env python3
"""Parse RT_MENU resources from PatPortDepot.exe to recover command IDs/labels."""
import struct
import sys

EXE = r"C:\Program Files (x86)\Motorola\ASTRO 25 Products\ASTRO 25 Portable Depot\PatPortDepot.exe"

# (type, name, file offset, size) from REA resource inspection
MENUS = [
    (129, 24413280, 1200), (156, 24414480, 84), (164, 24414564, 344),
    (165, 24414908, 1566), (171, 24416476, 48), (181, 24416524, 228),
    (182, 24416752, 86), (191, 24416840, 294), (192, 24417136, 930),
    (194, 24418068, 106), (195, 24418176, 90), (196, 24418268, 114),
    (197, 24418384, 86), (520, 24418472, 2228), (521, 24420700, 16),
    (522, 24420716, 122), (524, 24420840, 1286), (1030, 24422128, 4198),
    (1217, 24426328, 104), (2166, 24426432, 60),
]

MF_POPUP = 0x10
MF_SEPARATOR = 0x800


def rd16(b, o):
    return b[o] | (b[o + 1] << 8)


def rd32(b, o):
    return b[o] | (b[o + 1] << 8) | (b[o + 2] << 16) | (b[o + 3] << 24)


def utf16z(b, o):
    end = o
    while end + 1 < len(b) and not (b[end] == 0 and b[end + 1] == 0):
        end += 2
    try:
        s = b[o:end].decode("utf-16le", errors="replace")
    except Exception:
        s = ""
    return s, end + 2


def parse_standard(b, start, out, depth=0):
    o = start
    while o + 4 <= len(b):
        flags = rd16(b, o)
        if flags == 0 and b[o + 2] == 0 and b[o + 3] == 0 and o + 4 >= len(b):
            break
        popup = bool(flags & MF_POPUP)
        o += 2
        mid = None
        if not popup:
            if o + 2 > len(b):
                break
            mid = rd16(b, o)
            o += 2
        s, o = utf16z(b, o)
        if flags & MF_SEPARATOR and not s:
            continue
        if s:
            out.append((depth, mid, flags, s))
        if popup:
            # submenu follows immediately (standard templates embed recursively)
            parse_standard(b, o, out, depth + 1)
        if flags & 0x80 and not s and not popup:
            break
        if o >= len(b):
            break


def main():
    data = open(EXE, "rb").read()
    needle = ("read", "write", "radio", "codeplug", "device", "upload", "download", "clone", "program")
    for name, off, size in MENUS:
        b = data[off:off + size]
        if len(b) < 4:
            continue
        ver = rd16(b, 0)
        hdr = rd16(b, 2)
        out = []
        try:
            if ver == 0:
                parse_standard(b, 4, out)
            elif ver == 1:
                # MENUEX
                o = 4 + hdr
                while o + 4 <= len(b):
                    mtype = rd32(b, o); o += 4
                    state = rd32(b, o); o += 4
                    mid = rd32(b, o); o += 4
                    flags = rd16(b, o); o += 2
                    s, o = utf16z(b, o)
                    if s:
                        out.append((0, mid, flags, s))
                    o = (o + 3) & ~3
                    if mtype == 0 and not s and o >= len(b):
                        break
            else:
                continue
        except Exception as e:
            print(f"menu {name}: parse error {e}")
            continue
        interesting = [x for x in out if any(k in x[3].lower() for k in needle)]
        if interesting:
            print(f"===== menu {name} (ver={ver} hdr={hdr} size={size}) =====")
            for depth, mid, flags, s in out:
                mark = "  <--" if any(k in s.lower() for k in needle) else ""
                print(f"  {'  '*depth}[id={mid}] flags=0x{flags:04X} '{s}'{mark}")


if __name__ == "__main__":
    main()
