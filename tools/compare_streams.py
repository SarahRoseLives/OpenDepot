#!/usr/bin/env python3
"""Compare raw DataStg streams between two .cpg files by Strm_<n> name."""
import sys
import hashlib
import olefile


def streams(path):
    ole = olefile.OleFileIO(path)
    d = {}
    for e in ole.listdir():
        if e[0] == "DataStg" and len(e) == 2:
            d[e[1]] = ole.openstream(e).read()
    hdr = ole.openstream(["AdkFileHeader"]).read()
    ole.close()
    return hdr, d


def main():
    a, b = sys.argv[1], sys.argv[2]
    ha, sa = streams(a)
    hb, sb = streams(b)
    print(f"header identical: {ha == hb}")
    print(f"streams A={len(sa)} B={len(sb)}")
    keys = sorted(set(sa) | set(sb), key=lambda k: int(k.split('_')[1]))
    same = diff = 0
    for k in keys:
        x = sa.get(k)
        y = sb.get(k)
        if x == y:
            same += 1
        else:
            diff += 1
            if diff <= 10:
                print(f"  DIFF {k}: A={None if x is None else len(x)} B={None if y is None else len(y)}")
    print(f"identical streams: {same}, differing: {diff}")


if __name__ == "__main__":
    main()
