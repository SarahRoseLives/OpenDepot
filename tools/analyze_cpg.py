#!/usr/bin/env python3
"""Probe the .cpg data store for an integrity hash and stream relationships."""
import sys
import hashlib
import olefile


def main():
    path = sys.argv[1] if len(sys.argv) > 1 else r"C:\Users\Rose\Documents\Codeplugs\320CFP2121-backup.cpg"
    ole = olefile.OleFileIO(path)
    hdr = ole.openstream(["AdkFileHeader"]).read()

    # Candidate hash region in header
    targets = {
        "20B@0x236": hdr[0x236:0x236 + 20],
        "20B@0x246": hdr[0x246:0x246 + 20],
        "16B@0x238": hdr[0x238:0x238 + 16],
    }
    for name, t in targets.items():
        print(f"candidate {name} = {t.hex()}")

    entries = [e for e in ole.listdir() if e[0] == "DataStg" and len(e) == 2]
    by_index = sorted(entries, key=lambda e: int(e[1].split("_")[1]))
    by_name = sorted(entries)
    data = {e[1]: ole.openstream(e).read() for e in by_index}

    # Try many concatenation orders / subsets
    orders = {
        "index": [data[e[1]] for e in by_index],
        "name": [data[e[1]] for e in by_name],
        "index_minus0": [data[e[1]] for e in by_index if e[1] != "Strm_0"],
        "index_minus0_minus1": [data[e[1]] for e in by_index if e[1] not in ("Strm_0", "Strm_1")],
    }
    for oname, parts in orders.items():
        blob = b"".join(parts)
        for algo in ("sha1", "sha256", "md5"):
            d = hashlib.new(algo, blob).digest()
            for tname, t in targets.items():
                if d[:len(t)] == t:
                    print(f"MATCH {algo} order={oname} target={tname}")
        print(f"{oname}: n={len(parts)} bytes={len(blob)} sha1={hashlib.sha1(blob).hexdigest()}")

    # Is the header hash a hash of the OLE file minus header stream?
    raw = open(path, "rb").read()
    print("target 0x236 in raw file at:", raw.find(targets["20B@0x236"]))
    print("sha1(raw)=", hashlib.sha1(raw).hexdigest())

    # Structural: first 8 bytes of every stream
    print("\nfirst 8 bytes per stream (index order):")
    for e in by_index:
        d = data[e[1]]
        print(f"  {e[1]:>12} len={len(d):>5} {d[:8].hex()}")

    ole.close()


if __name__ == "__main__":
    main()
