function attach() {
    const base = Module.findBaseAddress('NextGen10.dll');
    if (base === null) { setTimeout(attach, 500); return; }
    const target = base.add(0x1F754);
    send({ t: 'hooked', base: base.toString(), target: target.toString() });
    Interceptor.attach(target, {
        onEnter(args) {
            try {
                const v = args[0];
                const psa = v.add(8).readPointer();
                if (psa.isNull()) { send({ t: 'parseconfig', null: true }); return; }
                const cb = psa.add(4).readU32();
                const pv = psa.add(0xC).readPointer();
                const n = psa.add(0x10).readU32();
                const len = cb * n;
                const a = new Uint8Array(pv.readByteArray(len));
                let hex = '';
                for (let i = 0; i < a.length; i++) hex += a[i].toString(16).padStart(2, '0');
                send({ t: 'parseconfig', len: len, hex: hex });
            } catch (e) { send({ t: 'parseconfig', err: '' + e }); }
        }
    });
}
attach();
