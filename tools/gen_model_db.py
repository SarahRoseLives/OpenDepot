import sys, os
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import olefile
from radio_tcp import load_records

def carr(f, name, b):
    f.write("static const uint8_t %s[] = {\n" % name)
    for k in range(0, len(b), 16):
        f.write("    " + ",".join("0x%02x" % x for x in b[k:k+16]) + ",\n")
    f.write("};\n\n")

def emit(cpg, model, out):
    ole = olefile.OleFileIO(cpg)
    hdr = ole.openstream(['AdkFileHeader']).read()[:800]
    ole.close()
    if len(hdr) < 800:
        raise SystemExit("short AdkFileHeader")
    recs = sorted(load_records(cpg))
    blob = bytearray()
    for i, p in recs:
        part = 0x0081 if (i >> 16) in (0x1004, 0x1005, 0x1006) else 0x0080
        blob += i.to_bytes(4, 'big') + part.to_bytes(2, 'big') + len(p).to_bytes(2, 'big') + p
    tag = "".join(c if c.isalnum() else "_" for c in model)
    with open(out, "w") as f:
        f.write('#include "model_db.h"\n\n')
        carr(f, "H_" + tag, hdr)
        carr(f, "M_" + tag, blob)
        f.write("const ModelBlob kModels[] = {\n")
        f.write('    {"%s", H_%s, M_%s, sizeof(M_%s)},\n' % (model, tag, tag, tag))
        f.write("};\nconst int kModelCount = 1;\n")
    print("wrote", out, len(recs), "records", len(blob), "bytes, header 800")

if __name__ == "__main__":
    emit(sys.argv[1], sys.argv[2], sys.argv[3])
