import frida, json, time, os, sys

EXE = r"C:\Program Files (x86)\Motorola\ASTRO 25 Products\ASTRO 25 Portable Depot\PatPortDepot.exe"
CPG = r"C:\Users\Rose\Projects\OpenDepot\build\radio-backups\205CNX2873-test-radio-backup.cpg"
OUT = os.path.join("build", "out", "depot-serial.jsonl")
HOOK = os.path.join("tools", "depot_serial_hook.js")


def main():
    os.makedirs(os.path.dirname(OUT), exist_ok=True)
    with open(HOOK, "r", encoding="utf-8") as f:
        js = f.read()
    pid = frida.spawn([EXE, CPG], cwd=os.path.dirname(EXE))
    session = frida.attach(pid)
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
            print(("READ " if t == "r" else "WRITE") + " " + str(len(b) // 2) + " bytes: " + b[:200], flush=True)
        elif t in ("SetCommState", "timeouts", "setup", "escape"):
            print(t, json.dumps(p), flush=True)

    script.on("message", on_message)
    script.load()
    frida.resume(pid)
    print("SPAWNED pid=%d ARMED - Depot is starting; wait for it to load" % pid, flush=True)
    try:
        while True:
            time.sleep(1)
    except KeyboardInterrupt:
        pass
    print("captured read=%d write=%d" % (total["r"], total["w"]), flush=True)
    try:
        session.detach()
    except Exception:
        pass


if __name__ == "__main__":
    main()
