import re
import struct
import sys

SRC = r"COM20 HEX DUMP.txt"
OUT_RCMP = r"build\out\rcmp_streams.txt"


def parse_blocks(path):
    blocks = []
    cur = None
    buf = bytearray()
    for line in open(path, "r", encoding="latin1", errors="replace"):
        if "- Open port" in line:
            if cur and buf:
                blocks.append((cur, bytes(buf)))
            cur = None
            buf = bytearray()
        elif "Written data" in line:
            if cur and buf:
                blocks.append((cur, bytes(buf)))
            cur = "W"
            buf = bytearray()
        elif "Read data" in line:
            if cur and buf:
                blocks.append((cur, bytes(buf)))
            cur = "R"
            buf = bytearray()
        elif "- Close port" in line:
            if cur and buf:
                blocks.append((cur, bytes(buf)))
            cur = None
            buf = bytearray()
        else:
            m = re.match(r"^\s+((?:[0-9a-f]{2} )+)", line)
            if m and cur:
                buf += bytes.fromhex(m.group(1))
    if cur and buf:
        blocks.append((cur, bytes(buf)))
    return blocks


def ppp_deframe(data):
    frames = []
    parts = data.split(b"\x7e")
    for part in parts:
        if not part:
            continue
        out = bytearray()
        i = 0
        ok = True
        while i < len(part):
            b = part[i]
            if b == 0x7D:
                if i + 1 >= len(part):
                    ok = False
                    break
                out.append(part[i + 1] ^ 0x20)
                i += 2
            else:
                out.append(b)
                i += 1
        if not ok or len(out) < 3:
            continue
        # drop 2-byte FCS
        frames.append(bytes(out[:-2]))
    return frames


def parse_ip(pkt):
    if len(pkt) < 20 or (pkt[0] >> 4) != 4:
        return None
    ihl = (pkt[0] & 0xF) * 4
    if len(pkt) < ihl + 20 or pkt[9] != 6:
        return None
    src = ".".join(str(x) for x in pkt[12:16])
    dst = ".".join(str(x) for x in pkt[16:20])
    tcp = pkt[ihl:]
    sport, dport, seq, ack = struct.unpack(">HHII", tcp[0:12])
    off = (tcp[12] >> 4) * 4
    flags = tcp[13]
    payload = tcp[off:]
    return src, dst, sport, dport, seq, ack, flags, payload


def main():
    blocks = parse_blocks(SRC)
    print("blocks:", len(blocks))
    streams = {"W": bytearray(), "R": bytearray()}
    lcp = []
    ipcp = []
    tcp_events = []
    for direction, data in blocks:
        if data.startswith(b"CLIENT") or data.startswith(b"CLIENTSERVER"):
            print(direction, "handshake:", data[:32])
        for fr in ppp_deframe(data):
            if not fr:
                continue
            body = fr
            if len(body) >= 2 and body[0] == 0xFF and body[1] == 0x03:
                body = body[2:]
            if not body:
                continue
            if body[0] & 1:
                proto = body[0]
                body = body[1:]
            elif len(body) >= 2:
                proto = struct.unpack(">H", body[0:2])[0]
                body = body[2:]
            else:
                continue
            if proto == 0xC021:
                lcp.append((direction, body))
            elif proto == 0x8021:
                ipcp.append((direction, body))
            elif proto == 0x0021:
                r = parse_ip(body)
                if r:
                    src, dst, sp, dp, seq, ack, flags, payload = r
                    tcp_events.append((direction, src, dst, sp, dp, seq, ack, flags, payload))
                    if payload:
                        streams[direction] += payload
    print("LCP frames:", len(lcp), "IPCP:", len(ipcp), "TCP events:", len(tcp_events))
    for d, b in lcp[:6]:
        print("LCP", d, b.hex())
    for d, b in ipcp[:8]:
        print("IPCP", d, b.hex())
    for ev in tcp_events[:12]:
        d, src, dst, sp, dp, seq, ack, flags, payload = ev
        print("TCP", d, src, sp, "->", dst, dp, "seq", seq, "ack", ack, "flags", hex(flags), "len", len(payload))

    with open(OUT_RCMP, "w", encoding="utf-8") as f:
        for d in ("W", "R"):
            data = bytes(streams[d])
            f.write("=== %s %d bytes ===\n" % (d, len(data)))
            o = 0
            n = 0
            while o + 2 <= len(data):
                ln = struct.unpack(">H", data[o:o + 2])[0]
                if ln < 2 or o + 2 + ln > len(data):
                    f.write("  bad length %d at %d\n" % (ln, o))
                    break
                msg = data[o + 2:o + 2 + ln]
                f.write("%4d %s len=%d %s\n" % (n, d, len(msg), msg[:48].hex()))
                o += 2 + ln
                n += 1
            f.write("\n")
    print("wrote", OUT_RCMP)


if __name__ == "__main__":
    main()
