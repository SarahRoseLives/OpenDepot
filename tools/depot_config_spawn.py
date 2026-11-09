import frida, time, os, ctypes, json
from ctypes import wintypes

EXE = r"C:\Program Files (x86)\Motorola\ASTRO 25 Products\ASTRO 25 Portable Depot\PatPortDepot.exe"
CPG = r"C:\Users\Rose\Projects\OpenDepot\build\radio-backups\205CNX2873-test-radio-backup.cpg"
HOOK = r"C:\Users\Rose\Projects\OpenDepot\tools\depot_config_hook.js"
OUT = r"C:\Users\Rose\Projects\OpenDepot\build\out\depot-config.jsonl"

os.makedirs(os.path.dirname(OUT), exist_ok=True)
js = open(HOOK, "r", encoding="utf-8").read()
pid = frida.spawn([EXE, CPG], cwd=os.path.dirname(EXE))
session = frida.attach(pid)
script = session.create_script(js)

def on_message(msg, data):
    if msg.get("type") != "send":
        return
    p = msg["payload"]
    print(json.dumps({k: v for k, v in p.items() if k != "hex"})[:500], flush=True)
    if p.get("hex"):
        print("HEX " + p["hex"][:400], flush=True)
    with open(OUT, "a", encoding="utf-8") as f:
        f.write(json.dumps(p) + "\n")

script.on("message", on_message)
script.load()
frida.resume(pid)
print("SPAWNED pid=%d" % pid, flush=True)

user32 = ctypes.windll.user32

def cls(h):
    b = ctypes.create_unicode_buffer(256); user32.GetClassNameW(h, b, 256); return b.value

def txt(h):
    b = ctypes.create_unicode_buffer(512); user32.GetWindowTextW(h, b, 512); return b.value

def wins(pid):
    out = []
    @ctypes.WINFUNCTYPE(wintypes.BOOL, wintypes.HWND, wintypes.LPARAM)
    def cb(h, lp):
        p = wintypes.DWORD(); user32.GetWindowThreadProcessId(h, ctypes.byref(p))
        if p.value == pid: out.append(h)
        return True
    user32.EnumWindows(cb, 0)
    return out

posted = False
start = time.time()
while time.time() - start < 75:
    for h in wins(pid):
        c = cls(h); t = txt(h)
        if c == "#32770" and t == "ASTRO 25 Portable Depot":
            user32.PostMessageW(h, 0x111, 22784, 0)
        if c == "AfxMDIFrame42" and " - " in t and not posted:
            user32.PostMessageW(h, 0x111, 32790, 0)
            posted = True
            print("posted Write Device", flush=True)
    time.sleep(1)

try:
    session.detach()
except Exception:
    pass
