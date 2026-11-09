import argparse
import json
import os
import threading
import time

import serial

LOG = os.path.join("build", "out", "com-proxy.jsonl")
stop = False
lock = threading.Lock()


def log_rec(direction, data):
    rec = {"t": time.time(), "dir": direction, "n": len(data), "b": data.hex()}
    with lock:
        with open(LOG, "a", encoding="utf-8") as f:
            f.write(json.dumps(rec) + "\n")
    print("%s %d %s" % (direction, len(data), data.hex()[:200]), flush=True)


def open_port(name, baud, rtscts):
    return serial.Serial(name, baudrate=baud, bytesize=8, parity="N", stopbits=1,
                         timeout=0.02, write_timeout=2, rtscts=rtscts, dsrdtr=False)


def pump(src, dst, direction):
    while not stop:
        try:
            n = src.in_waiting
            data = src.read(n if n else 1)
        except Exception as e:
            print("read error %s: %s" % (direction, e), flush=True)
            time.sleep(0.05)
            continue
        if data:
            log_rec(direction, data)
            try:
                dst.write(data)
                dst.flush()
            except Exception as e:
                print("write error %s: %s" % (direction, e), flush=True)
                time.sleep(0.05)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--virt", default="COM22")
    ap.add_argument("--radio", default="COM20")
    ap.add_argument("--baud", type=int, default=115200)
    ap.add_argument("--rtscts", action="store_true")
    args = ap.parse_args()
    os.makedirs(os.path.dirname(LOG), exist_ok=True)
    open(LOG, "w").close()
    v = open_port(args.virt, args.baud, args.rtscts)
    r = open_port(args.radio, args.baud, args.rtscts)
    r.dtr = True
    r.rts = True
    print("bridging %s <-> %s at %d (rtscts=%s)" % (args.virt, args.radio, args.baud, args.rtscts), flush=True)
    t1 = threading.Thread(target=pump, args=(v, r, "depot->radio"), daemon=True)
    t2 = threading.Thread(target=pump, args=(r, v, "radio->depot"), daemon=True)
    t1.start()
    t2.start()
    try:
        while True:
            time.sleep(0.5)
    except KeyboardInterrupt:
        pass
    globals()["stop"] = True
    time.sleep(0.2)
    v.close()
    r.close()


if __name__ == "__main__":
    main()
