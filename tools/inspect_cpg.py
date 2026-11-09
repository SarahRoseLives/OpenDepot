#!/usr/bin/env python3
"""Inspect an ASTRO 25 codeplug (.cpg) OLE compound document.

Read-only. Lists streams, sizes, and hashes; can dump a stream to stdout.
"""
import sys
import hashlib
import olefile


def human(n):
    for unit in ("B", "KB", "MB", "GB"):
        if n < 1024:
            return f"{n:.0f}{unit}" if unit == "B" else f"{n:.1f}{unit}"
        n /= 1024
    return f"{n:.1f}TB"


def list_streams(path):
    ole = olefile.OleFileIO(path)
    try:
        print(f"file: {path}")
        print(f"streams: {len(ole.listdir(streams=True, storages=True))}")
        entries = ole.listdir(streams=True, storages=True)
        rows = []
        for entry in entries:
            dotted = "/".join(entry)
            is_stream = ole.exists(entry) and ole.get_type(entry) == olefile.STGTY_STREAM
            if is_stream:
                size = ole.get_size(entry)
                data = ole.openstream(entry).read()
                digest = hashlib.sha256(data).hexdigest()
                rows.append((dotted, size, digest))
            else:
                rows.append((dotted + "/", -1, "(storage)"))
        rows.sort(key=lambda r: (r[1] < 0, r[0].lower()))
        for dotted, size, digest in rows:
            if size < 0:
                print(f"  [dir] {dotted}")
            else:
                print(f"  {size:>9} {human(size):>9}  {digest[:16]}  {dotted}")
    finally:
        ole.close()


def dump_stream(path, stream):
    ole = olefile.OleFileIO(path)
    try:
        entry = stream.split("/")
        data = ole.openstream(entry).read()
        sys.stdout.buffer.write(data)
    finally:
        ole.close()


def main():
    if len(sys.argv) < 3:
        print("usage: inspect_cpg.py <list|dump> <path> [stream]")
        sys.exit(2)
    cmd, path = sys.argv[1], sys.argv[2]
    if cmd == "list":
        list_streams(path)
    elif cmd == "dump":
        dump_stream(path, sys.argv[3])
    else:
        print(f"unknown command: {cmd}")
        sys.exit(2)


if __name__ == "__main__":
    main()
