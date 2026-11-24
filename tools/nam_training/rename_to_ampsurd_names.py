# One-off tool (2026-11-24): gives the release pack AMPSURD's own capture names (HG1..HG4 amps, OD1..OD8 pedals).
# The real amp / pedal is named only as a description ("captured from ...") in the READ ME, the notice and the
# gear_make / gear_model metadata. Weights, amp.xml data and file entries inside the amps stay unchanged.
# Usage: python3 rename_to_ampsurd_names.py "<AMPSURD Ultimate Metal Pack>" "<output dir>"
import sys, os, re, io, json, zipfile, shutil

IN, OUT = sys.argv[1:3]
OWNER = 'AMPSURD (Radovan Glovňa)'
CONTACT = 'Contact: https://ampsurd.com · contact@ampsurd.com'

# old folder -> code, old name prefix (files / NAM names), maker, model, maker as written in the notice
AMPS = {
    'Ampsurd Amp Capture Mesa Dual Rectifier 100 W Head': ('HG1', 'Mesa Dual Rectifier', 'Mesa/Boogie', 'Dual Rectifier 100 W Head'),
    'Ampsurd Amp Capture EVH 5150 III 100 W 6L6':         ('HG2', 'EVH 5150 III', 'EVH', '5150 III 100 W 6L6'),
    'Ampsurd Amp Capture Peavey 6505+ 112 60 W Combo':    ('HG3', 'Peavey 6505+ 112', 'Peavey', '6505+ 112 60 W Combo'),
    'Ampsurd Amp Capture Framus Cobra 100 W':             ('HG4', 'Framus Cobra', 'Framus', 'Cobra 100 W'),
}
# old pedal name prefix -> code (alphabetical, as agreed with the owner)
PEDALS = {
    'Boss SD-1': 'OD1', 'DIY Klon clone': 'OD2', 'DigiTech Bad Monkey': 'OD3', 'EHX East River Drive': 'OD4',
    'Gerhat Screamer': 'OD5', 'Ibanez TS7': 'OD6', 'Ibanez TS9DX': 'OD7', 'NUX Steel Singer': 'OD8',
}

def amp_notice(code, maker, model):
    return (f"AMPSURD {code}: neural captures made by {OWNER} from a privately owned {maker} {model}. "
            f"\"AMPSURD {code}\" and the capture names are AMPSURD's own; {maker} and all other product names, trademarks "
            f"and brands belong to their respective owners and are used only to identify the equipment that was captured. "
            f"AMPSURD is not affiliated with, sponsored by or endorsed by {maker}, and the captures were made without the "
            f"manufacturer's involvement or approval. They represent the sound of one individual unit and are provided as "
            f"they are, without any warranty. Terms of use: you may use these captures to play, record, perform and publish "
            f"your own music, including commercial releases, without paying any royalties or further fees to AMPSURD. You "
            f"may not sell, rent or otherwise distribute the capture files themselves, alone or as part of another product, "
            f"without written permission from AMPSURD.")

def set_nam_meta(data, name, notice=None, prefix_notice=None):
    d = json.loads(data); m = d.setdefault('metadata', {})
    m['name'] = name
    if notice is not None: m['notice'] = notice
    if prefix_notice is not None and m.get('notice'):
        if not m['notice'].startswith(prefix_notice): m['notice'] = prefix_notice + m['notice']
    return json.dumps(d).encode()

def xml_attr(xml, attr, value):
    esc = value.replace('&', '&amp;').replace('"', '&quot;').replace('<', '&lt;')
    head = re.search(r'<AMPSURD_AMP\b[^>]*>', xml, re.S)
    tag = head.group(0)
    if re.search(rf'\s{attr}="[^"]*"', tag): new = re.sub(rf'(\s{attr}=)"[^"]*"', lambda mm: f'{mm.group(1)}"{esc}"', tag, count=1)
    else: new = tag.replace('<AMPSURD_AMP ', f'<AMPSURD_AMP {attr}="{esc}" ', 1)
    return xml[:head.start()] + new + xml[head.end():]

def rebuild_amp(data, disp, maker, model, notice):
    zi = zipfile.ZipFile(io.BytesIO(data)); buf = io.BytesIO(); zo = zipfile.ZipFile(buf, 'w', zipfile.ZIP_DEFLATED)
    for it in zi.infolist():
        b = zi.read(it.filename)
        if it.filename == 'amp.xml':
            x = b.decode()
            for a, v in (('name', disp), ('maker', maker), ('model', model), ('notice', notice)): x = xml_attr(x, a, v)
            b = x.encode()
        elif it.filename.endswith('.nam'):
            old = json.loads(b).get('metadata', {}).get('name', '')
            g = re.search(r'GAIN \d{4}$', old) or re.search(r'gain_(\d{4})', it.filename)
            gain = g.group(0) if g and g.group(0).startswith('GAIN') else (f'GAIN {g.group(1)}' if g else '')
            d = json.loads(b); m = d.setdefault('metadata', {})
            m.update(name=f'{disp} {gain}'.strip(), modeled_by=OWNER, gear_type='amp', gear_make=maker, gear_model=model, notice=notice)
            b = json.dumps(d).encode()
        zo.writestr(it.filename, b)
    zo.close(); return buf.getvalue()

def pedal_code(old_name):
    for k, c in PEDALS.items():
        if old_name.startswith(k + ' '): return c, k, old_name[len(k) + 1:]
    raise SystemExit('unknown pedal: ' + old_name)

src_root = IN; dst_root = os.path.join(OUT, os.path.basename(os.path.normpath(IN))); os.makedirs(dst_root)
count = {'amp': 0, 'nam': 0, 'readme': 0}
amp_map = {}   # (old prefix + rest) -> new display name, for the preset pack

for old_dir, (code, pre, maker, model) in AMPS.items():
    s = os.path.join(src_root, old_dir); d = os.path.join(dst_root, f'AMPSURD {code} Captures'); os.makedirs(d)
    note = amp_notice(code, maker, model); files = []
    for fn in sorted(os.listdir(s)):
        p = os.path.join(s, fn)
        if fn.endswith('.ampsurdamp'):
            rest = fn[:-len('.ampsurdamp')].split(pre + ' ', 1)[1]           # "CH2 LEAD"
            disp = f'AMPSURD {code} {rest}'
            out = f'AMPSURD {code} Capture - {rest}.ampsurdamp'
            open(os.path.join(d, out), 'wb').write(rebuild_amp(open(p, 'rb').read(), disp, maker, model, note))
            files.append((out, rest)); count['amp'] += 1
        elif os.path.isdir(p):                                               # "<pre> NAMs/<pre> <CH>/<pre> <CH> GAIN hhmm.nam"
            nd = os.path.join(d, f'AMPSURD {code} NAMs'); os.makedirs(nd)
            for sub in sorted(os.listdir(p)):
                rest = sub.split(pre + ' ', 1)[1]; sd = os.path.join(nd, f'AMPSURD {code} {rest}'); os.makedirs(sd)
                for f in sorted(os.listdir(os.path.join(p, sub))):
                    if not f.endswith('.nam'): continue
                    tail = f[:-4].split(pre + ' ', 1)[1]                     # "CH2 LEAD GAIN 0730"
                    new = f'AMPSURD {code} {tail}'
                    b = json.loads(open(os.path.join(p, sub, f), 'rb').read())
                    m = b.setdefault('metadata', {}); m.update(name=new, modeled_by=OWNER, gear_type='amp', gear_make=maker, gear_model=model, notice=note)
                    open(os.path.join(sd, new + '.nam'), 'wb').write(json.dumps(b).encode()); count['nam'] += 1
        elif fn.startswith('READ ME'):
            readme_text = open(p, encoding='utf-8').read()
    # the READ ME is written after all amp files are known
    t = readme_text
    rules = t[t.index('GROUND RULES'):]
    body = (f"AMPSURD {code} - AMPSURD amp captures\n\n"
            f"AMPSURD {code} was captured from a privately owned {maker} {model}.\n\n"
            "THE FILES\n\n" + ''.join(f"  {o[:-len('.ampsurdamp')]}  (captured from the {maker} {model}, {r})\n" for o, r in files) +
            f"  AMPSURD {code} NAMs\\  (the single NAM captures inside each AMPSURD amp, one per GAIN position)\n\n"
            "LEGAL NOTICE AND TERMS OF USE\n\n" + note.replace('. ', '.\n') + "\n\n" + rules)
    open(os.path.join(d, f'READ ME - AMPSURD {code}.txt'), 'w', encoding='utf-8', newline='\r\n').write(body.replace('\r\n', '\n')); count['readme'] += 1
    amp_map[pre] = code

# pedals
s = os.path.join(src_root, 'Ampsurd Pedal Capture Overdrives'); d = os.path.join(dst_root, 'AMPSURD Overdrive Captures'); os.makedirs(d)
lines_map = {}
for fn in sorted(os.listdir(s)):
    p = os.path.join(s, fn)
    if fn.endswith('.nam'):
        code, real, mode = pedal_code(fn[:-4]); new = f'AMPSURD {code} {mode}'
        b = json.loads(open(p, 'rb').read()); m = b.setdefault('metadata', {})
        m['name'] = new
        if m.get('notice') and not m['notice'].startswith('AMPSURD '):
            m['notice'] = (f"AMPSURD {code}: " + m['notice'][0].lower() + m['notice'][1:]).replace(
                'AMPSURD is not affiliated', f"\"AMPSURD {code}\" and the capture names are AMPSURD's own. AMPSURD is not affiliated", 1)
        open(os.path.join(d, f'AMPSURD {code} Capture - {mode}.nam'), 'wb').write(json.dumps(b).encode()); count['nam'] += 1
        lines_map[fn[:-4]] = (code, real, mode)
t = open(os.path.join(s, 'READ ME - Overdrive pedals.txt'), encoding='utf-8').read()
legal = t[t.index('LEGAL NOTICE'):t.index('THE CAPTURES')]
legal = legal.replace('All product names,', "The AMPSURD capture names (OD1 to OD8) are AMPSURD's own. All product names,", 1)
caps = t[t.index('THE CAPTURES'):t.index('GROUND RULES')]
def cap_line(mm):
    old = mm.group(2); code, real, mode = lines_map[old]
    return f"{mm.group(1)}AMPSURD {code} {mode} (captured from the {real}):"
caps = re.sub(r'^(\s+)(.+?):(?= )', cap_line, caps, flags=re.M)
cl = caps.split('\n'); idx = [i for i, l in enumerate(cl) if l.startswith('  AMPSURD OD')]
for i, l in zip(idx, sorted((cl[i] for i in idx), key=lambda l: l.split()[1])): cl[i] = l   # list in OD order
caps = '\n'.join(cl)
mapping = "THE PEDALS\n\n" + ''.join(f"  AMPSURD {c}: captured from the {k}\n" for k, c in PEDALS.items()) + "\n"
body = "AMPSURD OD1 to OD8 - AMPSURD overdrive pedal captures\n\n" + mapping + legal + caps + t[t.index('GROUND RULES'):]
open(os.path.join(d, 'READ ME - AMPSURD Overdrives.txt'), 'w', encoding='utf-8', newline='\r\n').write(body.replace('\r\n', '\n')); count['readme'] += 1

# the artist preset pack: rename its files, set AMPSURD names + notices inside, and point the preset at the new names
PACK_AMPS = {'Dual ': ('HG1', 'Mesa/Boogie', 'Dual Rectifier 100 W Head'), 'EVH 5150 III ': ('HG2', 'EVH', '5150 III 100 W 6L6')}
PACK_PEDALS = {'Screamer standard': 'STANDARD', 'Screamer halfdrivew': 'HALF DRIVE'}
for fn in os.listdir(src_root):
    if not fn.endswith('.ampsurdpack'): continue
    zi = zipfile.ZipFile(os.path.join(src_root, fn)); ren = {}; data = {}
    gerhat = json.load(open(os.path.join(s, 'Gerhat Screamer STANDARD.nam')))['metadata']['notice']
    for e in zi.namelist():
        b = zi.read(e)
        if e.endswith('.ampsurdamp'):
            for pfx, (code, maker, model) in PACK_AMPS.items():
                if e.startswith(pfx):
                    rest = e[len(pfx):-len('.ampsurdamp')]; disp = f'AMPSURD {code} {rest}'
                    ren[e] = f'AMPSURD {code} Capture - {rest}.ampsurdamp'
                    data[ren[e]] = rebuild_amp(b, disp, maker, model, amp_notice(code, maker, model)); break
            else: raise SystemExit('unknown amp in pack: ' + e)
        elif e.endswith('.nam'):
            base = e[:-4]; m = re.match(r'(.+?)( \(\d+\))?$', base)
            mode = PACK_PEDALS[m.group(1)]; suf = m.group(2) or ''
            ren[e] = f'AMPSURD OD5 Capture - {mode}{suf}.nam'
            d2 = json.loads(b); md = d2.setdefault('metadata', {})
            md.update(name=f'AMPSURD OD5 {mode}', modeled_by=OWNER, gear_type='pedal', gear_make='Gerhat', gear_model='Screamer')
            md['notice'] = json.load(open(os.path.join(d, f'AMPSURD OD5 Capture - {mode}.nam')))['metadata']['notice']
            data[ren[e]] = json.dumps(d2).encode()
    for e in zi.namelist():
        if e.endswith('.ampsurd'):
            x = zi.read(e).decode()
            for o, n in ren.items(): x = x.replace(f'="{o}"', f'="{n}"')
            data[e] = x.encode()
        elif e == 'README.txt':
            r = zi.read(e).decode()
            for o, n in ren.items(): r = r.replace('  ' + o + '\r\n', '  ' + n + '\r\n').replace('  ' + o + '\n', '  ' + n + '\n')
            data[e] = r.encode()
    order = [ren.get(e, e) for e in zi.namelist()]
    zo = zipfile.ZipFile(os.path.join(dst_root, fn), 'w', zipfile.ZIP_DEFLATED)
    for e in order: zo.writestr(e, data[e])
    zo.close()
print(count)
