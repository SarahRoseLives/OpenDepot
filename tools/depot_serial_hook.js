// Capture PatPortDepot's serial I/O on COM20 (also logs DCB/timeouts).
const k32 = 'kernel32.dll';
const handles = {};

function hx(ptr, n) {
    if (!ptr || n <= 0) return '';
    try {
        const a = new Uint8Array(ptr.readByteArray(n));
        let s = '';
        for (let i = 0; i < a.length; i++) s += a[i].toString(16).padStart(2, '0');
        return s;
    } catch (e) { return 'ERR:' + e; }
}

function isCom(p) { return p && /COM20/i.test(p); }

['CreateFileA', 'CreateFileW'].forEach(function (name) {
    const addr = Module.getExportByName(k32, name);
    Interceptor.attach(addr, {
        onEnter(args) {
            this.path = args[1].isNull() ? null : (name.endsWith('W') ? args[1].readUtf16String() : args[1].readAnsiString());
            this.access = args[2].toInt32();
            this.share = args[3].toInt32();
            this.disposition = args[5].toInt32();
        },
        onLeave(ret) {
            const h = ret.toInt32();
            if (isCom(this.path)) {
                handles[h] = this.path;
                send({ t: 'open', h, path: this.path, access: this.access, share: this.share, disposition: this.disposition });
            }
        }
    });
});

Interceptor.attach(Module.getExportByName(k32, 'ReadFile'), {
    onEnter(args) { this.h = args[0].toInt32(); this.buf = args[1]; this.n = args[2].toInt32(); },
    onLeave(ret) {
        if (handles[this.h] && ret.toInt32())
            send({ t: 'r', h: this.h, n: this.n, b: hx(this.buf, this.n), ok: ret.toInt32() });
    }
});

Interceptor.attach(Module.getExportByName(k32, 'WriteFile'), {
    onEnter(args) { this.h = args[0].toInt32(); this.buf = args[1]; this.n = args[2].toInt32(); },
    onLeave(ret) {
        if (handles[this.h] && ret.toInt32())
            send({ t: 'w', h: this.h, n: this.n, b: hx(this.buf, this.n), ok: ret.toInt32() });
    }
});

['SetCommState', 'GetCommState'].forEach(function (name) {
    Interceptor.attach(Module.getExportByName(k32, name), {
        onEnter(args) {
            const h = args[0].toInt32();
            if (!handles[h]) return;
            send({ t: name, h, dcb: hx(args[1], 28), baud: args[1].add(4).readU32() });
        }
    });
});

Interceptor.attach(Module.getExportByName(k32, 'SetupComm'), {
    onEnter(args) {
        const h = args[0].toInt32();
        if (handles[h]) send({ t: 'setup', h, in: args[1].toInt32(), out: args[2].toInt32() });
    }
});

Interceptor.attach(Module.getExportByName(k32, 'SetCommTimeouts'), {
    onEnter(args) {
        const h = args[0].toInt32();
        if (handles[h]) {
            const p = args[1];
            send({ t: 'timeouts', h, readInterval: p.readU32(), readTotalMult: p.add(4).readU32(),
                   readTotalConst: p.add(8).readU32(), writeTotalMult: p.add(12).readU32(),
                   writeTotalConst: p.add(16).readU32() });
        }
    }
});

Interceptor.attach(Module.getExportByName(k32, 'EscapeCommFunction'), {
    onEnter(args) {
        const h = args[0].toInt32();
        if (handles[h]) send({ t: 'escape', h, func: args[1].toInt32() });
    }
});

send({ t: 'armed' });
