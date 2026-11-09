import sys, os
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import olefile
from radio_tcp import load_records

def carr(f, name, b):
    f.write("static const uint8_t %s[] = {\n" % name)
    for k in range(0, len(b), 16):
        f.write("    " + ",".join("0x%02x" % x for x in b[k:k+16]) + ",\n")
    f.write("};\n\n")

def load(cpg, model):
    ole = olefile.OleFileIO(cpg)
    hdr = ole.openstream(['AdkFileHeader']).read()[:800]
    ole.close()
    if len(hdr) < 800:
        raise SystemExit("short AdkFileHeader in " + cpg)
    blob = bytearray()
    for i, p in sorted(load_records(cpg)):
        part = 0x0081 if (i >> 16) in (0x1004, 0x1005, 0x1006) else 0x0080
        blob += i.to_bytes(4, 'big') + part.to_bytes(2, 'big') + len(p).to_bytes(2, 'big') + p
    return model, hdr, blob

def emit(pairs, out):
    # pairs: [(cpg, model), ...]
    data = [load(cpg, model) for cpg, model in pairs]
    with open(out, "w") as f:
        f.write('#include "model_db.h"\n\n')
        tags = []
        for model, hdr, blob in data:
            tag = "".join(c if c.isalnum() else "_" for c in model)
            tags.append(tag)
            carr(f, "H_" + tag, hdr)
            carr(f, "M_" + tag, blob)
        f.write("const ModelBlob kModels[] = {\n")
        for (model, hdr, blob), tag in zip(data, tags):
            f.write('    {"%s", H_%s, M_%s, sizeof(M_%s)},\n' % (model, tag, tag, tag))
        f.write("};\nconst int kModelCount = %d;\n" % len(data))
    for (model, hdr, blob) in data:
        print("  %s: %d bytes of records" % (model, len(blob)))
    print("wrote", out, "models:", len(data))

if __name__ == "__main__":
    out = sys.argv[1]
    rest = sys.argv[2:]
    if len(rest) < 2 or len(rest) % 2:
        raise SystemExit("usage: gen_model_db.py <out.cpp> <cpg> <model> [<cpg> <model> ...]")
    pairs = [(rest[i], rest[i+1]) for i in range(0, len(rest), 2)]
    emit(pairs, out)
