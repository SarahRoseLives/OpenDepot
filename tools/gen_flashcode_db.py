import json, re, os

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
SRC = os.path.join(ROOT, 'tools', 'flashcode_options.json')
OUT = os.path.join(ROOT, 'src', 'flashcode_db.cpp')

data = json.load(open(SRC, encoding='utf-8'))

def esc(s):
    return s.replace('\\', '\\\\').replace('"', '\\"')

def codes(seg):
    return re.findall(r'\b[A-Z]\d{2,3}\b', seg)

def rules(opts):
    auto = {}
    req = {}
    for o in opts:
        t = o.get('tooltip', '')
        code = o.get('code', '')
        if not code or not t:
            continue
        m = re.search(r'automatically included with the following:(.*?)(?:This option|$)', t, re.I)
        if m:
            auto[code] = codes(m.group(1))
        m = re.search(r'requires(?: the following:?)?(.*?)(?:This option|$)', t, re.I)
        if m:
            req[code] = codes(m.group(1))
    inv = {}
    for opt, pkgs in auto.items():
        for p in pkgs:
            inv.setdefault(p, []).append(opt)
    for k in inv:
        inv[k] = sorted(set(inv[k]))
    return inv, req

lines = []
lines.append('#include "adk.h"')
lines.append('')
lines.append('namespace opendepot {')
lines.append('')
lines.append('const std::vector<FlashcodePlatform>& FlashcodePlatforms() {')
lines.append('    static const std::vector<FlashcodePlatform> kPlatforms = {')
for name, opts in data.items():
    opts = sorted(opts, key=lambda o: (o['nibble'], o['bit']))
    lines.append('        {')
    lines.append('            "%s",' % esc(name))
    lines.append('            {')
    for o in opts:
        lines.append('                {%d, %d, "%s", "%s"},' % (
            o['nibble'], o['bit'], esc(o.get('code', '')), esc(o.get('label', ''))))
    lines.append('            },')
    pkgs, reqs = rules(opts)
    lines.append('            {')
    for k in sorted(pkgs):
        codes_s = ', '.join('"%s"' % esc(c) for c in pkgs[k])
        lines.append('                {"%s", {%s}},' % (esc(k), codes_s))
    lines.append('            },')
    lines.append('            {')
    for k in sorted(reqs):
        codes_s = ', '.join('"%s"' % esc(c) for c in reqs[k])
        lines.append('                {"%s", {%s}},' % (esc(k), codes_s))
    lines.append('            },')
    lines.append('        },')
lines.append('    };')
lines.append('    return kPlatforms;')
lines.append('}')
lines.append('')
lines.append('}  // namespace opendepot')
lines.append('')

open(OUT, 'w', encoding='utf-8').write('\n'.join(lines))
print('wrote', OUT, 'platforms', len(data))
