import re, sys, struct

NEW_C = 0x40; NEW_I = 0x20; PUSH = 0x10
NEW_S = 0x08; NEW_A = 0x04; NEW_W = 0x02; NEW_U = 0x01
SPECIAL_I = NEW_S | NEW_W | NEW_U
SPECIAL_D = NEW_S | NEW_A | NEW_W | NEW_U
SPECIALS_MASK = SPECIAL_D

def dec_num(b, i):
    if b[i] != 0:
        return b[i], i + 1
    return (b[i + 1] << 8) | b[i + 2], i + 3

class State:
    def __init__(self):
        self.ip_id = 0; self.total = 0
        self.src = b""; self.dst = b""; self.ihl = 20
        self.sport = 0; self.dport = 0
        self.seq = 0; self.ack = 0; self.win = 0; self.urg = 0
        self.off = 20; self.flags = 0

def decode_uncompressed(ip):
    ihl = (ip[0] & 0xF) * 4
    conn = ip[9]
    st = State()
    st.ihl = ihl
    st.total = struct.unpack(">H", ip[2:4])[0]
    st.ip_id = struct.unpack(">H", ip[4:6])[0]
    st.src = bytes(ip[12:16]); st.dst = bytes(ip[16:20])
    tcp = ip[ihl:]
    st.sport, st.dport, st.seq, st.ack, offf, st.win, ck, st.urg = struct.unpack(">HHIIHHHH", tcp[:20])
    st.off = (offf >> 12) * 4
    st.flags = offf & 0x1FF
    data = tcp[st.off:]
    return conn, st, st.total, data, ck

def decode_compressed(buf, st):
    mask = buf[0]; i = 1
    if mask & NEW_C:
        conn = buf[i]; i += 1
    ck = struct.unpack(">H", buf[i:i+2])[0]; i += 2
    # order U, W, A, S, I
    if mask & NEW_U:
        d, i = dec_num(buf, i); st.urg = (st.urg + d) & 0xFFFF
        st.flags |= 0x20
    else:
        st.flags &= ~0x20
    if mask & NEW_W:
        d, i = dec_num(buf, i); st.win = (st.win + d) & 0xFFFF
    if special_type(mask):
        pass
    else:
        if mask & NEW_A:
            d, i = dec_num(buf, i); st.ack = (st.ack + d) & 0xFFFFFFFF
        if mask & NEW_S:
            d, i = dec_num(buf, i); st.seq = (st.seq + d) & 0xFFFFFFFF
    if mask & NEW_I:
        d, i = dec_num(buf, i); st.ip_id = (st.ip_id + d) & 0xFFFF
    else:
        st.ip_id = (st.ip_id + 1) & 0xFFFF
    if mask & PUSH: st.flags |= 0x08
    else: st.flags &= ~0x08
    data = buf[i:]
    return ck, data

def special_type(mask):
    m = mask & SPECIALS_MASK
    return m in (SPECIAL_I, SPECIAL_D)

def apply_special(mask, st, last_data_len):
    m = mask & SPECIALS_MASK
    if m == SPECIAL_D:
        st.seq = (st.seq + last_data_len) & 0xFFFFFFFF
    elif m == SPECIAL_I:
        st.seq = (st.seq + last_data_len) & 0xFFFFFFFF
        st.ack = (st.ack + last_data_len) & 0xFFFFFFFF

def load_capture(fn):
    lines = open(fn, encoding="latin-1").read().splitlines()
    def hexbytes(s):
        t = []
        for p in s.strip().split():
            if re.fullmatch(r"[0-9A-Fa-f]{2}", p): t.append(int(p, 16))
            else: break
        return t
    stream = []; cur = None
    for ln in lines:
        if "Written data" in ln: cur = "W"; continue
        if "Read data" in ln: cur = "R"; continue
        if ln.strip().startswith("["): continue
        if cur and re.match(r"\s*[0-9A-Fa-f]{2}", ln):
            for b in hexbytes(ln): stream.append((cur, b))
    frames = []; cf = []
    for d, b in stream:
        if b == 0x7E:
            if cf: frames.append(cf); cf = []
        else: cf.append((d, b))
    out = []
    for fr in frames:
        raw = bytes(b for _, b in fr); d = fr[0][0]
        u = bytearray(); i = 0
        while i < len(raw):
            if raw[i] == 0x7D and i + 1 < len(raw): u.append(raw[i+1] ^ 0x20); i += 2
            else: u.append(raw[i]); i += 1
        out.append((d, bytes(u[:-2]) if len(u) >= 2 else bytes(u)))
    return out

def main():
    fn = sys.argv[1]
    frames = load_capture(fn)
    states = {"W": {}, "R": {}}; last = {"W": None, "R": None}; lastlen = {"W": 0, "R": 0}
    msgs = []
    for d, u in frames:
        if not u: continue
        # strip control/ip proto with PFC consideration
        if u[0] == 0xFF and u[1:3] == b"\x03\xc0" and u[3:5] == b"\x21\x02":
            continue
        if u[0] == 0xFF and u[1:3] == b"\x03\x80" and u[3:5] == b"\x21\x02":
            continue
        proto = None; body = None
        if u[0] == 0x2F:
            proto = 0x2F; body = u[1:]
        elif u[0] == 0x2D:
            proto = 0x2D; body = u[1:]
        elif u[0] == 0x21:
            proto = 0x21; body = u[1:]
        if proto == 0x21:
            # plain IPv4
            ihl = (body[0] & 0xF) * 4
            if len(body) < 20 or (body[0] >> 4) != 4: continue
            lst = last[d]
            if lst is None:
                lst = State(); states[d][255] = lst; last[d] = 255
            st = states[d][last[d]]
            st.total = struct.unpack(">H", body[2:4])[0]
            st.ip_id = struct.unpack(">H", body[4:6])[0]
            st.src = bytes(body[12:16]); st.dst = bytes(body[16:20])
            tcp = body[ihl:]
            if len(tcp) < 20: continue
            st.sport, st.dport, st.seq, st.ack, offf, st.win, ck, st.urg = struct.unpack(">HHIIHHHH", tcp[:20])
            st.off = (offf >> 12) * 4; st.flags = offf & 0x1FF
            data = tcp[st.off:]
            lastlen[d] = len(data)
            msgs.append((d, "IP", st, data)); continue
        if proto == 0x2F:
            conn, st, total, data, ck = decode_uncompressed(body)
            st.ihl = (body[0] & 0xF) * 4
            states[d][conn] = st; last[d] = conn
            lastlen[d] = len(data)
            # data length: total includes headers
            hdr = st.ihl + st.off
            lastlen[d] = max(0, total - hdr)
            msgs.append((d, "UNCOMP", st, data))
            continue
        if proto == 0x2D:
            if last[d] is None:
                msgs.append((d, "COMP-NOSTATE", None, body)); continue
            st = states[d][last[d]]
            mask = body[0]
            if special_type(mask):
                apply_special(mask, st, lastlen[d])
                # special case: no A/S deltas; still checksum follows, then I
                ck, data = decode_compressed_special(body, st)
            else:
                ck, data = decode_compressed(body, st)
            lastlen[d] = len(data)
            msgs.append((d, "COMP", st, data))
            continue
    print("file:", fn, "frames:", len(msgs))
    for idx, (d, kind, st, data) in enumerate(msgs):
        if st is not None:
            print("%04d %s %-5s %d->%d flags=%02x seq=%08x ack=%08x len=%d :: %s" % (
                idx, d, kind, st.sport, st.dport, st.flags, st.seq, st.ack, len(data), data.hex()))
        else:
            print("%04d %s %-12s :: %s" % (idx, d, kind, data.hex()))

def decode_compressed_special(buf, st):
    mask = buf[0]; i = 1
    if mask & NEW_C: i += 1
    ck = struct.unpack(">H", buf[i:i+2])[0]; i += 2
    if mask & NEW_I:
        d, i = dec_num(buf, i); st.ip_id = (st.ip_id + d) & 0xFFFF
    else:
        st.ip_id = (st.ip_id + 1) & 0xFFFF
    if mask & PUSH: st.flags |= 0x08
    else: st.flags &= ~0x08
    return ck, buf[i:]

if __name__ == "__main__":
    main()
