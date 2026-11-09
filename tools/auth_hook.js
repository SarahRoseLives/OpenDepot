function hookMod(m) {
  ['_encrypt_function@12', '_decrypt_function@12'].forEach(function (name) {
    var fn = null;
    try { fn = m.getExportByName(name); } catch (e) {}
    if (!fn) return;
    console.log('[hook] attaching ' + name + ' @ ' + fn);
    Interceptor.attach(fn, {
      onEnter(args) {
        this.c = args[2];
        console.log('[enter] ' + name + ' a=' + args[0] + ' b=' + args[1] + ' c=' + args[2]);
        try { console.log('  key=' + hexdump(args[0], { length: 16 })); } catch (e) {}
        try { console.log('  in =' + hexdump(args[1], { length: 48 })); } catch (e) {}
      },
      onLeave(ret) {
        try { console.log('  out=' + hexdump(this.c, { length: 48 }) + ' ret=' + ret); } catch (e) {}
      }
    });
  });
}
var done = false;
function hookOnce() {
  if (done) return;
  try {
    var m = Process.getModuleByName('authAES.dll');
    if (m) { hookMod(m); done = true; return; }
  } catch (e) {}
  setTimeout(hookOnce, 20);
}
try {
  Process.attachModuleObserver({
    onAdded: function (m) {
      if (m && m.name && m.name.toLowerCase().indexOf('authaes') >= 0) hookMod(m);
    }
  });
} catch (e) { console.log('observer err ' + e); }
hookOnce();
