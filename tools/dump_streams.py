#!/usr/bin/env python3
"""Hexdump selected streams of an ASTRO 25 codeplug OLE compound document."""
import sys
import olefile


def hexdump(data, base=0, maxbytes=None):
    n = len(data) if maxbytes is None else min(len(data), maxbytes)
    for off in range(0, n, 16):
        chunk = data[off:off + 16]
        hexs = " ".join(f"{b:02X}" for b in chunk)
        hexs = hexs.ljust(16 * 3 - 1)
        text = "".join(chr(b) if 32 <= b < 127 else "." for b in chunk)
        print(f"{base+off:08X}  {hexs}  |{text}|")
    if maxbytes is not None and len(data) > maxbytes:
        print(f"... ({len(data) - maxbytes} more bytes)")


def main():
    path = sys.argv[1]
    streams = sys.argv[2:]
    ole = olefile.OleFileIO(path)
    try:
        if not streams:
            streams = ["AdkFileHeader"]
        for name in streams:
            entry = name.split("/")
            if not ole.exists(entry):
                print(f"=== {name} : MISSING ===")
                continue
            data = ole.openstream(entry).read()
            print(f"=== {name}  ({len(data)} bytes) ===")
            hexdump(data)
            print()
    finally:
        ole.close()


if __name__ == "__main__":
    main()
