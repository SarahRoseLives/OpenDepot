import serial, time, struct, sys

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

def frame(proto, data, acfc=True, pfc=False):
    body = (b"\xff\x03" if acfc else b"") + (bytes([proto]) if pfc and proto < 0x100 else struct.pack(">H", proto)) + data
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

def read_frames(s, t=2.0):
    end = time.time() + t; buf = bytearray()
    while time.time() < end:
        n = s.in_waiting
        if n: buf += s.read(n)
        else: time.sleep(0.05)
    frames = []; cur = None
    for b in buf:
        if b == 0x7e:
            if cur: frames.append(unesc(cur))
            cur = bytearray()
        elif cur is not None:
            cur.append(b)
    return frames

def handle(s, f):
    if len(f) < 6: return
    if f[0] == 0xff and f[1] == 0x03 and f[2] == 0xc0 and f[3] == 0x21:
        code = f[4]
        if code in (1, 9):
            ack = frame(0xc021, bytes([0x02 if code == 1 else 0x0a, f[5]]) + f[6:-2], acfc=True, pfc=False)
            s.write(ack); print("TX LCP " + ("ACK " if code == 1 else "ECHO-REPLY ") + ack.hex())
    elif f[0] == 0xff and f[1] == 0x03 and f[2] == 0x80 and f[3] == 0x21 and f[4] == 1:
        ack = frame(0x8021, bytes([0x02, f[5]]) + f[6:-2], acfc=True, pfc=False)
        s.write(ack); print("TX IPCP ACK " + ack.hex())


def pump(s, t):
    end = time.time() + t; buf = bytearray(); got = []
    while time.time() < end:
        n = s.in_waiting
        if n:
            buf += s.read(n)
            while True:
                i = buf.find(b"\x7e")
                if i < 0: break
                j = buf.find(b"\x7e", i + 1)
                if j < 0: break
                raw = bytes(buf[i + 1:j]); del buf[:j + 1]
                f = unesc(raw)
                if f and f[0] == 0x2d:
                    print("0x2d RX len=%d %s" % (len(f), f.hex())); got.append(f)
                elif f and f[0] == 0x0c and len(f) >= 3:
                    print("0x2d RX len=%d %s" % (len(f), f.hex())); got.append(f)
                else:
                    print("RX " + f.hex()); handle(s, f)
        else:
            time.sleep(0.02)
    return got

def rpc(s, payload, wait=0.6, log=True):
    fr = frame(0x2d, payload, acfc=False, pfc=True)
    s.write(fr)
    if log: print("TX 0x2d " + payload.hex())
    return pump(s, wait)

def handshake(s, which="write"):
    if which == "write":
        pre = ["00366b0003010600", "0bf243000901048512bf00000000"]
    else:
        pre = ["00f5b70003010600", "0bf6a300020012"]
    for p in pre:
        rpc(s, bytes.fromhex(p), 0.8)

def at(s, cmd, w=1.0):
    s.reset_input_buffer(); s.write(cmd); time.sleep(w)
    d = s.read(s.in_waiting); print(repr(cmd) + " -> " + repr(d)); return d

def main():
    port = "COM20"
    ppponly = "--ppponly" in sys.argv
    clientonly = "--clientonly" in sys.argv
    for a in sys.argv[1:]:
        if not a.startswith("--"): port = a
    if not ppponly and not clientonly:
        s = serial.Serial(port, 9600, rtscts=True, timeout=0.5)
        at(s, b"AT&V\r", 2.0)
        at(s, b"AT#DATLCL=1\r", 0.6)
        at(s, b"AT+IPR=115200\r", 0.6)
        s.close(); time.sleep(0.5)
    s = serial.Serial(port, 115200, rtscts=True, timeout=0.5)
    if not ppponly:
        if clientonly:
            at(s, b"AT#DATLCL=1\r", 0.6)
        s.reset_input_buffer(); s.write(b"CLIENT"); time.sleep(2.0)
        d = s.read(s.in_waiting); print("CLIENT -> " + repr(d))

    s.write(frame(0xc021, bytes.fromhex("010100140206000000000506000148b807020802"), acfc=True, pfc=False))
    print("TX LCP CR")
    for f in read_frames(s, 2.0):
        print("RX " + f.hex()); handle(s, f)

    s.write(frame(0x8021, bytes.fromhex("0101000a0306c0a88002"), acfc=True, pfc=False))
    print("TX IPCP CR")
    for f in read_frames(s, 2.0):
        print("RX " + f.hex()); handle(s, f)

    mode = "write"
    for a in sys.argv[1:]:
        if a.startswith("--hs="):
            mode = a.split("=", 1)[1]
    handshake(s, mode)
    pump(s, 3.0)
    s.close()

if __name__ == "__main__":
    main()
