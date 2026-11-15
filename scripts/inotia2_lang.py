#!/usr/bin/env python3
"""Extra text languages for Inotia 2 (strindex.dat + strdata.dat).

  python3 -I scripts/inotia2_lang.py export es   # write/refresh templates (keeps existing "tr")
  python3 -I scripts/inotia2_lang.py build  es   # pack them into build/lang/spa/ (+ zip with every built lang)
  python3 -I scripts/inotia2_lang.py check  es   # list token/length problems only
  python3 -I scripts/inotia2_lang.py test        # self-check: identity repack == original, byte for byte

Sources are ALWAYS the untouched English files in ux0_data/inotia2/Inotia2.app/ (or game_app/).
Templates: inotia2_translation/<lang>/NN_<group>.json = list of {"id", "en", "tr"}.
Empty/missing "tr" = keep English.

Format (LoadString / LoadStringData in the binary):
  strindex.dat = u32 BE count, count x {char name[32] (NUL + junk), u32 LE start, u32 LE end}
  strdata.dat  = the strings back to back, each NUL-terminated; [start, end) includes the NUL.
The game looks strings up by name and copies end-start bytes into its media heap, so lengths
are free. Text is UTF-8 and the engine steps UTF-8 properly (Str_Split / Text_*), FreeType
draws it with the Vita system font: accents stay (no ASCII folding needed).
"""
import json, os, re, struct, sys, zipfile

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
APP = next(p for p in (os.path.join(ROOT, 'ux0_data/inotia2/Inotia2.app'), os.path.join(ROOT, 'game_app'))
           if os.path.exists(os.path.join(p, 'strdata.dat')))
TPL = os.path.join(ROOT, 'inotia2_translation')
OUT = os.path.join(ROOT, 'build/lang')
ZIP = os.path.join(ROOT, 'build/inotia2_vita_traduccion_ES_PT.zip')
LANG_DIR = {'es': 'spa', 'pt': 'por'}  # must match langs[] in source/controls.c
CHUNK = 12000  # bytes of English per template file

# Never translated: formulas, file names, IPs/URLs/version, separator symbols.
SKIP = re.compile(r'^(SAVE_FILE_NAME|StrClr_|StatusD_strExpress|ItemClass_strExpress'
                  r'|Symbol_strText\[([0-9]|1[0-3])\]$'
                  r'|WV_CONST_STRING(0|3|12|19|20|87|88)$)')
# Engine codes: printf ("5% of" in prose is not one: no space flag), $2..$0 colours, $Onn options, ^n speaker, & line break.
TOKEN = re.compile(r'%[-+#0-9.]*[a-zA-Z%]|\$O\d\d|\$[0-9A-Z]|\^\d|&')


def load():
    idx = open(os.path.join(APP, 'strindex.dat'), 'rb').read()
    dat = open(os.path.join(APP, 'strdata.dat'), 'rb').read()
    n = struct.unpack('>I', idx[:4])[0]
    recs = []
    for i in range(n):
        r = idx[4 + i * 40:4 + i * 40 + 40]
        s, e = struct.unpack('<II', r[32:])
        recs.append((r[:32], r[:32].split(b'\0')[0].decode(), dat[s:e - 1].decode('utf-8')))
    return idx[:4], recs


def translatable(name, en):
    return not SKIP.match(name) and re.search(r'[A-Za-z]{2}', TOKEN.sub('', en))


def templates(lang):
    d = os.path.join(TPL, lang)
    out = {}
    for f in sorted(os.listdir(d)) if os.path.isdir(d) else []:
        if f.endswith('.json') and f[0] not in '._':  # _*.json = glossaries
            for e in json.load(open(os.path.join(d, f), encoding='utf-8')):
                out[e['id']] = e
    return out


def export(lang):
    _, recs = load()
    old = templates(lang)
    d = os.path.join(TPL, lang)
    os.makedirs(d, exist_ok=True)
    chunks, cur, size = [], [], 0
    for _, name, en in recs:
        if not translatable(name, en):
            continue
        tr = old.get(name, {}).get('tr', '') if old.get(name, {}).get('en') == en else ''
        cur.append({'id': name, 'en': en, 'tr': tr})
        size += len(en.encode())
        if size >= CHUNK:
            chunks.append(cur)
            cur, size = [], 0
    if cur:
        chunks.append(cur)
    for i, c in enumerate(chunks):
        group = re.sub(r'\[\d+\]$', '', c[0]['id'])
        with open(os.path.join(d, '%02d_%s.json' % (i, group)), 'w', encoding='utf-8') as f:
            json.dump(c, f, ensure_ascii=False, indent=1)
    print('%s: %d templates, %d strings' % (lang, len(chunks), sum(map(len, chunks))))


def problems(e):
    en, tr = e['en'], e.get('tr') or ''
    if not tr:
        return None
    if sorted(TOKEN.findall(en)) != sorted(TOKEN.findall(tr)):
        return 'tokens %s != %s' % (sorted(TOKEN.findall(en)), sorted(TOKEN.findall(tr)))
    if '\0' in tr:
        return 'NUL'
    # ponytail: names/labels are drawn in fixed UI slots; long texts wrap and page.
    lim = len(en.encode()) * 3 // 2 + 6
    if len(tr.encode()) > lim:
        return 'too long %d > %d' % (len(tr.encode()), lim)
    return None


def pack(head, recs, tr):
    idx, dat = bytearray(head), bytearray()
    for raw, name, en in recs:
        s = len(dat)
        dat += (tr.get(name) or en).encode('utf-8') + b'\0'
        idx += raw + struct.pack('<II', s, len(dat))
    return bytes(idx), bytes(dat)


def build(lang, zip_it=True):
    head, recs = load()
    tr, bad = {}, 0
    for name, e in templates(lang).items():
        p = problems(e)
        if p:
            bad += 1
            print('  SKIP %s: %s' % (name, p))
        elif e.get('tr') and e['en'] == dict((r[1], r[2]) for r in recs).get(name):
            tr[name] = e['tr']
    idx, dat = pack(head, recs, tr)
    d = os.path.join(OUT, LANG_DIR[lang])
    os.makedirs(d, exist_ok=True)
    open(os.path.join(d, 'strindex.dat'), 'wb').write(idx)
    open(os.path.join(d, 'strdata.dat'), 'wb').write(dat)
    total = sum(1 for _, n, en in recs if translatable(n, en))
    print('%s -> %s: %d/%d strings translated, %d rejected' % (lang, d, len(tr), total, bad))
    if zip_it:
        with zipfile.ZipFile(ZIP, 'w', zipfile.ZIP_DEFLATED) as z:
            for ld in sorted(os.listdir(OUT)):
                for f in ('strindex.dat', 'strdata.dat'):
                    p = os.path.join(OUT, ld, f)
                    if os.path.exists(p):
                        z.write(p, 'inotia2/lang/%s/%s' % (ld, f))
        print('zip:', ZIP)
    return bad


def check(lang):
    n = 0
    for name, e in templates(lang).items():
        p = problems(e)
        if p:
            n += 1
            print('%s: %s\n  en: %s\n  tr: %s' % (name, p, e['en'], e['tr']))
    left = sum(1 for e in templates(lang).values() if not e.get('tr'))
    print('%s: %d problems, %d untranslated' % (lang, n, left))


def test():
    head, recs = load()
    idx, dat = pack(head, recs, {})
    assert idx == open(os.path.join(APP, 'strindex.dat'), 'rb').read(), 'strindex differs'
    assert dat == open(os.path.join(APP, 'strdata.dat'), 'rb').read(), 'strdata differs'
    assert problems({'en': 'Hunt $210 wolves$0&%d', 'tr': 'Caza $210 lobos$0&%d'}) is None
    assert problems({'en': 'Hunt $2wolves$0', 'tr': 'Caza lobos'})
    print('OK: identity repack is byte-identical (%d strings)' % len(recs))


if __name__ == '__main__':
    cmd = sys.argv[1] if len(sys.argv) > 1 else ''
    if cmd == 'test':
        test()
    elif cmd in ('export', 'build', 'check') and len(sys.argv) > 2 and sys.argv[2] in LANG_DIR:
        {'export': export, 'build': build, 'check': check}[cmd](sys.argv[2])
    else:
        sys.exit(__doc__)
