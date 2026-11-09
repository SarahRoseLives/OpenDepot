import serial, time, struct, sys, socket
import os, olefile, re
from adkcrypt import decrypt as adk_decrypt
from cpg_decrypt import Rc4, be32 as cbe32

AUTH_MASK = bytes.fromhex("88946a50e2ec6e06a0e290c6567c92c44c881cf4ead240766ca22a0c0c4284ca")

def auth_response(challenge):
    """RcmpProtocol auth response (NextGen10.dll FUN_1000fd89).
    32-bit LFSR seeded 0x51E88D21, 7 clocks/byte, feedback = challenge[31-k] bit ^ state bit9.
    Verified against three captured challenge/response pairs."""
    if len(challenge) < 32:
        return b""
    inp = bytes(challenge[:32])
    state = 0x51E88D21
    out = bytearray(32)
    for k in range(32):
        b = inp[k]
        rev = inp[31 - k]
        for bit in range(7, 0, -1):
            fb = ((rev >> bit) & 1) ^ ((state >> 9) & 1)
            state = (state * 2 + fb) & 0xFFFFFFFF
            b ^= (fb << bit) & 0xFF
        out[k] = b
    return bytes(out)

def load_records(path):
    ole = olefile.OleFileIO(path)
    hdr = ole.openstream(['AdkFileHeader']).read()
    etype = (hdr[8] & 0x18) >> 3
    rc4 = None
    if etype == 1:
        ln = hdr[0x239]
        seed = adk_decrypt(hdr[0x23A:0x23A + ln])
        rc4 = Rc4(seed[:9])
    recs = []
    for e in [x for x in ole.listdir() if x[0] == 'DataStg' and len(x) == 2]:
        if e[1] == 'Strm_0':
            continue
        raw = ole.openstream(e).read()
        pt = rc4.decrypt(raw) if etype == 1 else raw
        recs.append((cbe32(pt, 0), pt[6:]))
    ole.close()
    return recs

def parse_capture_bodies(path):
    """Extract ordered RCMP query/write bodies from a depot write capture,
    starting at the first 0x1005 query (after config) and stopping before 010601."""
    bodies = []
    txt = open(path, "r", encoding="utf-8", errors="replace").read()
    for ln in txt.splitlines():
        if "W COMP" not in ln:
            continue
        m = re.search(r'::\s*([0-9a-fA-F]+)', ln)
        if not m:
            continue
        h = m.group(1)
        if len(h) < 8:
            continue
        bodies.append(bytes.fromhex(h[4:]))
    start = None
    for i, b in enumerate(bodies):
        if len(b) >= 5 and b[0] == 1 and b[1] == 2 and b[3:5] == b"\x10\x05":
            start = i
            break
    if start is None:
        return []
    out = []
    for b in bodies[start:]:
        if b == b"\x01\x06\x01":
            break
        if len(b) >= 2 and b[0] == 1 and b[1] in (1, 2):
            out.append(b)
    return out

HOST_IP = "192.168.128.2"
RADIO_IP = "192.168.128.1"
RADIO_PORT = 8002
HOST_PORT = 8001

def fcs(data):
    f = 0xFFFF
    for b in data:
        f ^= b
        for _ in range(8):
            f = (f >> 1) ^ 0x8408 if f & 1 else f >> 1
    return f ^ 0xFFFF

def esc(data):
    out = bytearray()
    for b in data:
        if b in (0x7e, 0x7d) or b < 0x20:
            out += bytes([0x7d, b ^ 0x20])
        else:
            out.append(b)
    return bytes(out)

def frame(proto, data, pfc=True, addr=True):
    body = (b"\xff\x03" if addr else b"") + (bytes([proto]) if pfc else struct.pack(">H", proto)) + data
    f = fcs(body)
    return b"\x7e" + esc(body + struct.pack("<H", f)) + b"\x7e"

def unesc(data):
    out = bytearray(); i = 0
    while i < len(data):
        if data[i] == 0x7d and i + 1 < len(data):
            out.append(data[i + 1] ^ 0x20); i += 2
        else:
            out.append(data[i]); i += 1
    return bytes(out)

def csum(data):
    if len(data) % 2:
        data += b"\0"
    s = sum(struct.unpack(">%dH" % (len(data) // 2), data))
    while s >> 16:
        s = (s & 0xFFFF) + (s >> 16)
    return (~s) & 0xFFFF

def ip_packet(proto, payload, ident):
    total = 20 + len(payload)
    h = struct.pack(">BBHHHBBH4s4s", 0x45, 0, total, ident, 0x4000, 64, proto, 0,
                    socket.inet_aton(HOST_IP), socket.inet_aton(RADIO_IP))
    chk = csum(h)
    h = struct.pack(">BBHHHBBH4s4s", 0x45, 0, total, ident, 0x4000, 64, proto, chk,
                    socket.inet_aton(HOST_IP), socket.inet_aton(RADIO_IP))
    return h + payload

def tcp_segment(sport, dport, seq, ack, flags, payload, window=0x16D0):
    hdr = struct.pack(">HHIIHHHH", sport, dport, seq, ack, (5 << 12) | flags, window, 0, 0)
    pseudo = socket.inet_aton(HOST_IP) + socket.inet_aton(RADIO_IP) + struct.pack(">BBH", 0, 6, len(hdr) + len(payload))
    chk = csum(pseudo + hdr + payload)
    hdr = struct.pack(">HHIIHHHH", sport, dport, seq, ack, (5 << 12) | flags, window, chk, 0)
    return hdr + payload

TCP_FIN, TCP_SYN, TCP_RST, TCP_PSH, TCP_ACK = 1, 2, 4, 8, 0x10

def bringup(port):
    for baud in (9600, 115200):
        try:
            s = serial.Serial(port, baud, rtscts=True, timeout=0.5)
        except Exception:
            continue
        s.write(b"AT&V\r"); time.sleep(1.5)
        r = s.read(s.in_waiting)
        print("AT&V@%d ->" % baud, repr(r))
        if b"CARRIER" in r or b"RADIO" in r or b"OK" in r or b"ERROR" in r:
            s.write(b"AT#DATLCL=1\r"); time.sleep(0.5); print("DATLCL ->", repr(s.read(s.in_waiting)))
            if baud == 9600:
                s.write(b"AT+IPR=115200\r"); time.sleep(0.5); print("IPR ->", repr(s.read(s.in_waiting)))
            s.close(); time.sleep(0.4)
            return 115200
        s.close()
    return None

class Link:
    def __init__(self, port, baud=115200):
        self.s = serial.Serial(port, baud, rtscts=True, timeout=0.2)
        self.ip_id = 1

    def send_ctrl(self, code, ident, options=b""):
        self.s.write(frame(0x8021 if code in (3, 4) else 0x8021, bytes([code, ident]) + options, pfc=False))

    def send_ip(self, payload, proto=6):
        pkt = ip_packet(proto, payload, self.ip_id); self.ip_id = (self.ip_id + 1) & 0xFFFF
        self.s.write(frame(0x21, pkt, pfc=True, addr=False))

    def read_frames(self, t):
        end = time.time() + t; buf = bytearray(); out = []
        while time.time() < end:
            n = self.s.in_waiting
            if n:
                buf += self.s.read(n)
                while True:
                    i = buf.find(b"\x7e")
                    if i < 0:
                        break
                    j = buf.find(b"\x7e", i + 1)
                    if j < 0:
                        break
                    raw = bytes(buf[i + 1:j]); del buf[:j + 1]
                    out.append(unesc(raw))
            else:
                time.sleep(0.01)
        return out

def parse_opts(b):
    i = 0; out = []
    while i + 2 <= len(b):
        t = b[i]; l = b[i + 1]
        if l < 2 or i + l > len(b):
            break
        out.append(b[i:i + l]); i += l
    return out

def handle_link(f, link, lcp_id, seen):
    if len(f) >= 6 and f[0] == 0xFF and f[1] == 0x03 and f[2:4] == b"\xc0\x21":
        code = f[4]
        if code in (1, 9):
            if code == 1:
                # echo radio LCP options back as Ack, our magic in place of theirs is fine to echo
                ack = frame(0xC021, bytes([2, f[5]]) + f[6:-2], pfc=False)
                link.s.write(ack)
                seen["lcp_radio_cr"] = True
                print("LCP <- ConfigReq; ACK sent")
            else:
                reply = frame(0xC021, bytes([0x0A, f[5]]) + f[6:-2], pfc=False)
                link.s.write(reply)
                print("LCP <- EchoReq; reply sent")
    elif len(f) >= 6 and f[0] == 0xFF and f[1] == 0x03 and f[2:4] == b"\x80\x21":
        code = f[4]
        if code == 1:
            opts = f[6:-2]
            vj = [o for o in parse_opts(opts) if o[0] == 2]
            if vj:
                rej = bytes([4, f[5]]) + b"".join(vj)
                link.s.write(frame(0x8021, rej, pfc=False))
                print("IPCP <- ConfigReq with VJ; REJECT VJ (force uncompressed)")
            else:
                ack = bytes([2, f[5]]) + opts
                link.s.write(frame(0x8021, ack, pfc=False))
                seen["ipcp_radio_up"] = True
                print("IPCP <- ConfigReq (no VJ); ACK sent")
        elif code == 2:
            seen["ipcp_host_up"] = True
            print("IPCP <- ConfigAck (our request accepted)")
        elif code == 3:
            print("IPCP <- ConfigNak", f.hex())

def parse_ipv4(f):
    # f begins with 0x21 (PFC) optionally
    if f and f[0] == 0x21:
        ip = f[1:]
    elif f and f[0] == 0x00:
        ip = f
    else:
        return None
    if len(ip) < 20 or (ip[0] >> 4) != 4:
        return None
    ihl = (ip[0] & 0xF) * 4
    total = struct.unpack(">H", ip[2:4])[0]
    proto = ip[9]
    src = ".".join(str(b) for b in ip[12:16])
    dst = ".".join(str(b) for b in ip[16:20])
    payload = ip[ihl:total]
    return dict(proto=proto, src=src, dst=dst, payload=payload)

def main():
    port = "COM20"
    write_cpg = None
    replay_capture = None
    auth_test = False
    write_ids = [0x10040000, 0x10050000, 0x10060000]
    args = sys.argv[1:]
    i = 0
    while i < len(args):
        a = args[i]
        if a == "--write-cpg" and i + 1 < len(args):
            write_cpg = args[i + 1]
            i += 2
            continue
        if a == "--auth-test":
            auth_test = True
            i += 1
            continue
        if a == "--replay-capture" and i + 1 < len(args):
            replay_capture = args[i + 1]
            i += 2
            continue
        if a == "--ids" and i + 1 < len(args):
            write_ids = [int(x, 16) for x in args[i + 1].split(',')]
            i += 2
            continue
        if not a.startswith("--"):
            port = a
        i += 1
    if replay_capture and "--dry-run" in args:
        bodies = parse_capture_bodies(replay_capture)
        print("capture:", replay_capture)
        print("replay messages:", len(bodies))
        for j, b in enumerate(bodies):
            if j < 8 or j >= len(bodies) - 4:
                print(f"{j:03d} {b.hex()[:96]}")
        return

    if bringup(port) is None:
        print("WARN: no AT response; assuming radio is already in PPP/data mode, trying LCP anyway")

    link = Link(port)
    link.s.write(b"CLIENT"); time.sleep(2.0); print("CLIENT ->", repr(link.s.read(link.s.in_waiting)))

    # LCP
    link.s.write(frame(0xC021, bytes.fromhex("010100140206000000000506000148b807020802"), pfc=False))
    print("TX LCP CR")
    seen = {}
    for f in link.read_frames(2.0):
        handle_link(f, link, 1, seen)

    # IPCP request (address only, no VJ)
    link.s.write(frame(0x8021, bytes.fromhex("0101000a0306c0a88002"), pfc=False))
    print("TX IPCP CR")
    t0 = time.time()
    while time.time() - t0 < 6:
        for f in link.read_frames(0.5):
            handle_link(f, link, 1, seen)
        if seen.get("ipcp_host_up") and seen.get("ipcp_radio_up"):
            break
    print("state:", seen)

    # TCP handshake
    seq = 0x12345678; ack = 0
    syn = tcp_segment(HOST_PORT, RADIO_PORT, seq, 0, TCP_SYN, b"")
    link.send_ip(syn)
    print("TX SYN")
    radio_seq = None
    t0 = time.time()
    while time.time() - t0 < 4 and radio_seq is None:
        for f in link.read_frames(0.5):
            if len(f) >= 2 and f[0] == 0xFF:
                handle_link(f, link, 1, seen); continue
            ip = parse_ipv4(f)
            if not ip:
                print("RX non-IP len=%d %s" % (len(f), f.hex())); continue
            if ip["proto"] != 6:
                print("RX IP proto", ip["proto"], ip["payload"].hex()); continue
            th = ip["payload"]
            sport, dport, tseq, tack, off, win, c, urg = struct.unpack(">HHIIHHHH", th[:20])
            flags = off & 0x1FF
            print("RX TCP %d->%d seq=%08x ack=%08x flags=%03x" % (sport, dport, tseq, tack, flags))
            if flags & TCP_SYN and flags & TCP_ACK:
                radio_seq = tseq
                print("SYN-ACK! radio ISN =", hex(tseq))
    if radio_seq is None:
        print("no SYN-ACK"); link.s.close(); sys.exit(3)

    my_seq = (seq + 1) & 0xFFFFFFFF
    their_seq = (radio_seq + 1) & 0xFFFFFFFF
    link.send_ip(tcp_segment(HOST_PORT, RADIO_PORT, my_seq, their_seq, TCP_ACK, b""))
    print("TX ACK; TCP established")

    rxbuf = bytearray()

    def pump_tcp(dur):
        nonlocal their_seq
        end = time.time() + dur
        while time.time() < end:
            for f in link.read_frames(0.3):
                if len(f) >= 2 and f[0] == 0xFF:
                    handle_link(f, link, 1, seen); continue
                ip = parse_ipv4(f)
                if not ip:
                    if f: print("RX nonIP len=%d %s" % (len(f), f.hex()))
                    continue
                if ip["proto"] != 6:
                    continue
                th = ip["payload"]
                if len(th) < 20:
                    continue
                sport, dport, tseq, tack, off, win, c, urg = struct.unpack(">HHIIHHHH", th[:20])
                hlen = (off >> 12) * 4
                flags = off & 0x1FF
                data = th[hlen:]
                if len(data):
                    # Sequence-aware receive: the radio may retransmit or
                    # overlap; only append bytes we have not already consumed,
                    # otherwise the length-prefixed stream desyncs.
                    diff = (tseq - their_seq) & 0xFFFFFFFF
                    added = 0
                    if diff == 0:
                        rxbuf.extend(data); their_seq = (their_seq + len(data)) & 0xFFFFFFFF
                        added = len(data)
                    elif diff & 0x80000000:
                        # tseq behind their_seq: duplicate/overlap -> keep only the tail
                        skipped = (their_seq - tseq) & 0xFFFFFFFF
                        if skipped < len(data):
                            tail = data[skipped:]
                            rxbuf.extend(tail); their_seq = (their_seq + len(tail)) & 0xFFFFFFFF
                            added = len(tail)
                        else:
                            added = 0  # fully duplicate
                    else:
                        # gap / out of order: do not append; ACK what we expect
                        print("RX OOO seq=%08x want=%08x drop" % (tseq, their_seq))
                    # Always ACK so the radio stops retransmitting
                    link.send_ip(tcp_segment(HOST_PORT, RADIO_PORT, my_seq, their_seq, TCP_ACK, b""))
                    if added:
                        print("RX +%d (seq=%08x now=%08x) buf=%d" % (added, tseq, their_seq, len(rxbuf)))

    def send_msg(payload):
        nonlocal my_seq
        wire = struct.pack(">H", len(payload)) + payload
        link.send_ip(tcp_segment(HOST_PORT, RADIO_PORT, my_seq, their_seq, TCP_ACK | TCP_PSH, wire))
        my_seq = (my_seq + len(wire)) & 0xFFFFFFFF
        print("TX %d: %s" % (len(payload), payload.hex()))

    def recv_msg(dur=2.0):
        end = time.time() + dur
        while True:
            if len(rxbuf) >= 2:
                L = struct.unpack(">H", rxbuf[:2])[0]
                if L <= 4096 and len(rxbuf) >= 2 + L:
                    m = bytes(rxbuf[2:2 + L]); del rxbuf[:2 + L]; return m
            if time.time() > end:
                return None
            pump_tcp(0.3)

    def send_raw(hx):
        nonlocal my_seq
        b = bytes.fromhex(hx)
        link.send_ip(tcp_segment(HOST_PORT, RADIO_PORT, my_seq, their_seq, TCP_ACK | TCP_PSH, b))
        my_seq = (my_seq + len(b)) & 0xFFFFFFFF

    def req(payload, dur=2.0):
        send_msg(payload)
        m = recv_msg(dur)
        print("   -> %s" % (m.hex() if m else "NONE"))
        return m

    if "--replay" in sys.argv:
        import re as _re
        msgs = []
        src = r"C:\Users\Rose\Projects\OpenDepot\build\out\vj_write.txt"
        for ln in open(src, encoding="latin-1"):
            m = _re.match(r"\s*\d+ W .*::\s*([0-9a-f]+)\s*$", ln)
            if not m:
                continue
            h = m.group(1)
            if len(h) < 8:
                continue
            L = int(h[:4], 16)
            if 2 + L > len(h) // 2:
                continue
            wire = h[:(2 + L) * 2]
            if wire[4:6] == "03":
                continue
            msgs.append(wire)
        print("REPLAY messages:", len(msgs))
        pump_tcp(0.5)
        log = open(r"C:\Users\Rose\Projects\OpenDepot\build\out\replay.log", "w", encoding="ascii")
        for i, hx in enumerate(msgs):
            send_raw(hx)
            r = recv_msg(1.5)
            st = ""
            if r is not None and len(r) >= 3 and r[0] == 0x81 and r[2] != 0:
                st = " STATUS=%02x" % r[2]
            line = "%03d/%03d %s%s" % (i + 1, len(msgs), hx[:28], st)
            print(line); log.write(line + "\n"); log.flush()
        log.close()
        print("REPLAY done")
        link.s.close(); return

    pump_tcp(1.0)

    if auth_test:
        req(bytes.fromhex("010600"))
        req(bytes.fromhex("01048512bf00000000"))
        req(bytes.fromhex("000f00"))
        r = req(bytes.fromhex("0300"), 3.0)
        print("challenge:", r.hex() if r else None, flush=True)
        if r and len(r) >= 35 and r[0] == 0x83 and r[1] == 0x00:
            key = r[3:35]
            a2 = auth_response(key)
            rr = req(bytes([0x03, 0x01]) + a2, 3.0)
            print("auth reply:", rr.hex() if rr else None, flush=True)
        rr = req(bytes.fromhex("010881"), 3.0)
        print("program-mode reply:", rr.hex() if rr else None, flush=True)
        req(bytes.fromhex("010601"), 3.0)
        link.s.close()
        return

    def radio_write(cpg_path, ids):
        recs = dict(load_records(cpg_path))
        if '--all' in sys.argv:
            ids = list(recs.keys())
        first = [0x10050000, 0x10040000, 0x10060000]
        order = [i for i in first if i in recs]
        order += [i for i in ids if i not in first and i != 0x00470000]
        if 0x00470000 in recs:
            order.append(0x00470000)
        print("BEGIN records=%d" % len(order), flush=True)
        req(bytes.fromhex("010600"))
        req(bytes.fromhex("01048512bf00000000"))
        req(bytes.fromhex("000f00"))
        r = req(bytes.fromhex("0300"), 3.0)
        if not (r and len(r) >= 35 and r[0] == 0x83 and r[1] == 0x00):
            print("AUTH CHALLENGE FAILED", r.hex() if r else None, flush=True)
            link.s.close(); sys.exit(3)
        a2 = auth_response(r[3:35])
        rr = req(bytes([0x03, 0x01]) + a2, 3.0)
        if not (rr and len(rr) >= 2 and rr[0] == 0x83 and rr[1] == 0x01):
            print("AUTH REJECTED", rr.hex() if rr else None, flush=True)
            link.s.close(); sys.exit(3)
        print("auth ok", flush=True)
        req(bytes.fromhex("010881"))
        req(bytes.fromhex("010280004700000001"))
        req(bytes.fromhex("010380000201f4"))
        req(bytes.fromhex("0103800e0b0fff"))
        req(bytes.fromhex("01078000"))
        fail = 0
        for sid in order:
            data = recs.get(sid)
            if data is None:
                print("missing record", hex(sid), flush=True)
                fail += 1
                continue
            part = 0x0081 if (sid >> 16) in (0x1004, 0x1005, 0x1006) else 0x0080
            tag = sid >> 16
            inst = sid & 0xFFFF
            q = bytes([0x01, 0x02, part]) + tag.to_bytes(2, 'big') + bytes([0x00, 0x00, 0x00, 0x01])
            req(q, 3.0)
            ln = len(data)
            body = (bytearray([0x01, 0x01, part]) + tag.to_bytes(2, 'big') +
                    inst.to_bytes(2, 'big') + ln.to_bytes(2, 'big') + ln.to_bytes(4, 'big') + data)
            print("write", hex(sid), "len", ln, flush=True)
            wr = req(bytes(body), 5.0)
            print("  reply", wr.hex() if wr else None, flush=True)
            if not (wr and len(wr) >= 3 and wr[0] == 0x81 and wr[1] == 0x01 and wr[2] == 0x00):
                fail += 1
        req(bytes.fromhex("01078000"))  # Send ReOrg (0x01078000) finalizes the ISH directory
        req(bytes.fromhex("010601"))
        if fail:
            print("WRITE FAILED: %d record(s) not accepted" % fail, flush=True)
            link.s.close(); sys.exit(3)
        print("END", flush=True)

    if write_cpg:
        radio_write(write_cpg, write_ids)
        link.s.close()
        return

    if replay_capture:
        bodies = parse_capture_bodies(replay_capture)
        if "--reads-only" in sys.argv:
            bodies = [b for b in bodies if len(b) >= 2 and b[0] == 1 and b[1] == 2]
        print("replay messages:", len(bodies), flush=True)
        if not bodies:
            print("no messages parsed", flush=True)
            link.s.close(); return
        req(bytes.fromhex("010600"))
        req(bytes.fromhex("01048512bf00000000"))
        req(bytes.fromhex("000f00"))
        r = req(bytes.fromhex("0300"), 3.0)
        if not (r and len(r) >= 35 and r[0] == 0x83 and r[1] == 0x00):
            print("AUTH CHALLENGE FAILED", r.hex() if r else None, flush=True)
            link.s.close(); return
        a2 = auth_response(r[3:35])
        rr = req(bytes.fromhex("0301") + a2, 3.0)
        if not (rr and rr[0] == 0x83 and rr[1] == 0x01):
            print("AUTH REJECTED", rr.hex() if rr else None, flush=True)
            link.s.close(); return
        print("auth ok", flush=True)
        req(bytes.fromhex("010881"))
        req(bytes.fromhex("010280004700000001"))
        req(bytes.fromhex("010380000201f4"))
        req(bytes.fromhex("0103800e0b0fff"))
        req(bytes.fromhex("01078000"))
        for j, b in enumerate(bodies):
            q = req(b, 5.0)
            if j % 50 == 0 or j == len(bodies) - 1:
                print("progress", j + 1, "/", len(bodies), (q.hex()[:48] if q else None), flush=True)
        req(bytes.fromhex("01078000"), 5.0)  # Send ReOrg (commit/finalize ISH directory)
        req(bytes.fromhex("010601"), 5.0)
        print("REPLAY END", flush=True)
        link.s.close(); return
    req(bytes.fromhex("010600"))
    req(bytes.fromhex("000f00"))   # version
    req(bytes.fromhex("001100"))   # serial
    req(bytes.fromhex("0012"))     # uuid
    req(bytes.fromhex("0300"))     # secret key (read-only)
    if "--probe" in sys.argv:
        req(bytes.fromhex("0100800034000000120000"))
        req(bytes.fromhex("0100800ff0000000260000"))
    if "--wtest" in sys.argv:
        wp = bytes.fromhex("01018000340000001200000012400314060000070c230b0a0602570003c73e")
        req(wp)
    if "--rwtest" in sys.argv:
        rd = req(bytes.fromhex("0100800047000000100000"))
        if rd and rd[0] == 0x81 and len(rd) >= 27:
            data = rd[11:]
            print("0x0047 current:", data.hex())
            wr = bytes.fromhex("01018000470000001000000010") + data
            req(wr)
            rd2 = req(bytes.fromhex("0100800047000000100000"))
            if rd2 and rd2[0] == 0x81 and len(rd2) >= 27:
                print("0x0047 after:", rd2[11:].hex(), "MATCH" if rd2[11:] == data else "DIFF")
    if "--authtest" in sys.argv:
        km = req(bytes.fromhex("0300"))
        if km and km[0] == 0x83 and len(km) >= 35:
            K2 = km[3:35]
            A2 = auth_response(K2)
            print("computed auth:", A2.hex())
            req(bytes.fromhex("0301") + A2)
    link.s.close()

if __name__ == "__main__":
    main()
