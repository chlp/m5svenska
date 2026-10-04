import re, statistics
POSMAP = {'noun-en':'NOUN','noun-ett':'NOUN','noun':'NOUN','noun-en/-ett':'NOUN','verb':'VERB','aux verb':'VERB',
          'adjective':'ADJ','particip':'ADJ','numeral':'NUM'}
def clean(s):
    s = re.sub(r"\s*\(.*?\)\s*", " ", s).strip()
    return re.sub(r"\s+", " ", s)

saldo = {}
order = []
for l in open("saldo_freq.txt"):
    lid, a, b = l.rstrip("\n").split("\t")
    m = re.match(r"(.*)\.\.([a-z]*)\.\d+$", lid)
    if not m: continue
    lemma, pos = m.group(1).replace("_", " "), m.group(2)
    score = int(a) + int(b)
    key = lemma.lower()
    saldo[key] = max(saldo.get(key, 0), score)
    order.append((score, lemma, pos))

kelly = []
for l in open("kelly.tsv"):
    id_, wpm, cefr, src, gram, sv, cls, ex = l.rstrip("\n").split("\t")
    if cls == 'proper name': continue
    sv = clean(sv)
    if not sv or sv[0].isupper(): continue
    kelly.append(dict(wpm=float(wpm), cefr=cefr, src=src, gram=gram.strip(), sv=sv, cls=cls))

# WPM для ручных записей Kelly (у них фиктивное 1e6) — пересчёт из частоты SALDO
ratios = [k['wpm'] / saldo[k['sv'].lower()] for k in kelly
          if k['src'] != 'manual' and saldo.get(k['sv'].lower())]
ratio = statistics.median(ratios)
for k in kelly:
    if k['src'] == 'manual':
        k['wpm'] = saldo.get(k['sv'].lower(), 0) * ratio or 5.0
kelly.sort(key=lambda k: -k['wpm'])

out, seen = [], {}
for k in kelly:
    key = k['sv'].lower()
    pos = POSMAP.get(k['cls'], 'OTHER')
    if key in seen:
        seen[key]['cls'] += '/' + k['cls']
        continue
    gender = 'en' if k['cls'] == 'noun-en' else 'ett' if k['cls'] == 'noun-ett' else ''
    e = dict(sv=k['sv'], pos=pos, gender=gender, cefr=k['cefr'], cls=k['cls'], src='kelly')
    seen[key] = e
    out.append(e)
print("kelly unique", len(out))

SPOS = {'nn':'NOUN','vb':'VERB','vbm':'VERB','av':'ADJ','ab':'OTHER','abm':'OTHER','pp':'OTHER','pn':'OTHER',
        'kn':'OTHER','sn':'OTHER','in':'OTHER','nl':'NUM','ppm':'OTHER','avm':'ADJ','inm':'OTHER','pnm':'OTHER'}
order.sort(key=lambda x: -x[0])
extra = []
for score, lemma, pos in order:
    if pos not in SPOS: continue
    key = lemma.lower()
    if key in seen or lemma != key or re.search(r"[0-9\-.:]", lemma) or len(lemma) < 2: continue
    e = dict(sv=lemma, pos=SPOS[pos], gender='', cefr='', cls=pos, src='saldo')
    seen[key] = e
    extra.append(e)
    if len(out) + len(extra) >= 11200: break
out += extra
for e in out:
    e['score'] = saldo.get(e['sv'].lower()) or 0
for e in out:
    if not e['score']:
        k = next((k for k in kelly if k['sv'] == e['sv']), None)
        e['score'] = k['wpm'] / ratio if k else 0
out.sort(key=lambda e: -e['score'])
with open("candidates.tsv", "w") as f:
    for i, e in enumerate(out, 1):
        f.write(f"{i}\t{e['sv']}\t{e['pos']}\t{e['gender']}\t{e['cefr']}\t{e['cls']}\t{e['src']}\n")
print("total", len(out), "extra", len(extra))
