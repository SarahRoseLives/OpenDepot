import frida, json, sys, time, os

OUT = os.path.join("build", "out", "depot-serial.jsonl")
HOOK = os.path.join("tools", "depot_serial_hook.js")


def main():
    os.makedirs(os.path.dirname(OUT), exist_ok=True)
    session = frida.attach("PatPortDepot.exe")
    with open(HOOK, "r", encoding="utf-8") as f:
        js = f.read()
    script = session.create_script(js)
    total = {"r": 0, "w": 0}

    def on_message(msg, data):
        if msg.get("type") != "send":
            return
        p = msg["payload"]
        p["_ts"] = time.time()
        with open(OUT, "a", encoding="utf-8") as f:
            f.write(json.dumps(p) + "\n")
        t = p.get("t")
        if t == "open":
            print("OPEN", p.get("path"), "handle", p.get("h"), flush=True)
        elif t in ("r", "w"):
            total[t] += 1
            b = p.get("b", "")
            print(("READ " if t == "r" else "WRITE") + " " + str(len(b) // 2) + " bytes: " + b[:160], flush=True)
        elif t in ("SetCommState", "timeouts", "setup", "escape"):
            print(t, json.dumps(p), flush=True)

    script.on("message", on_message)
    script.load()
    print("ARMED - waiting for Depot serial I/O on COM20", flush=True)
    try:
        while True:
            time.sleep(1)
    except KeyboardInterrupt:
        pass
    print("captured read=%d write=%d" % (total["r"], total["w"]), flush=True)
    session.detach()


if __name__ == "__main__":
    main()
