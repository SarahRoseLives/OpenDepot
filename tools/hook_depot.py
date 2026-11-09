import frida, sys, time, os

exe = r"C:\Program Files (x86)\Motorola\ASTRO 25 Products\ASTRO 25 Portable Depot\PatPortDepot.exe"
cpg = sys.argv[1] if len(sys.argv) > 1 else r"C:\Users\Rose\Projects\OpenDepot\build\radio-backups\prewrite-q387.cpg"
js_path = os.path.join(os.path.dirname(os.path.abspath(__file__)), "auth_hook.js")
js = open(js_path, encoding="utf-8").read()

pid = frida.spawn([exe, cpg])
session = frida.attach(pid)
script = session.create_script(js)

def on_message(message, data):
    if isinstance(message, dict):
        print(message.get('payload', message), flush=True)
    else:
        print(message, flush=True)

script.on('message', on_message)
script.load()
frida.resume(pid)
print("SPAWNED pid=%d file=%s" % (pid, cpg), flush=True)
print("Do: wait for the file to load, then Tools -> Depot -> Write Codeplug, pick COM20, confirm.", flush=True)

deadline = time.time() + 170
try:
    while time.time() < deadline:
        time.sleep(1)
except KeyboardInterrupt:
    pass
print("HOOK WINDOW DONE", flush=True)
