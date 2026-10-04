#!/usr/bin/env python3
"""Собирает словарь src/words.h и data/words.tsv.

Источники:
  data/candidates.tsv — леммы по убыванию частоты (Kelly + SALDO, Språkbanken):
      ранг, лемма, часть речи, род (en/ett), CEFR, класс, источник
  data/ru/*.tsv       — переводы: ранг<TAB>перевод[<TAB>своё написание]
      перевод "-"           — слово выкидывается
      префикс en:/ett:/pl:  — род существительного, если его нет в кандидатах

    python3 tools/gen_words.py
"""
import glob
import os
import re
import sys

ROOT = os.path.join(os.path.dirname(__file__), "..")
LIMIT = 10000
sys.path.insert(0, os.path.dirname(__file__))
from make_vlw import CHARSET  # noqa: E402

CEFR = {"A1": 1, "A2": 2, "B1": 3, "B2": 4, "C1": 5, "C2": 6}


def c_str(s):
    return '"' + s.replace("\\", "\\\\").replace('"', '\\"') + '"'


def main():
    tr = {}
    for path in sorted(glob.glob(os.path.join(ROOT, "data/ru/*.tsv"))):
        for n, line in enumerate(open(path, encoding="utf-8"), 1):
            line = line.rstrip("\n")
            if not line.strip():
                continue
            parts = line.split("\t")
            if len(parts) < 2:
                sys.exit(f"{path}:{n}: нет перевода: {line!r}")
            tr[int(parts[0])] = (parts[1].strip(), parts[2].strip().lstrip("=") if len(parts) > 2 else "")

    words, seen = [], set()
    for line in open(os.path.join(ROOT, "data/candidates.tsv"), encoding="utf-8"):
        rank, lemma, pos, gender, cefr, _cls, _src = line.rstrip("\n").split("\t")
        if int(rank) not in tr:
            continue
        ru, sv = tr[int(rank)]
        if ru == "-":
            continue
        m = re.match(r"(en|ett|pl):\s*", ru)
        if m:
            gender = "" if m.group(1) == "pl" else m.group(1)
            ru = ru[m.end():]
        m = re.match(r"(en|ett|pl):", sv)  # род, указанный в колонке написания
        if m:
            gender = "" if m.group(1) == "pl" else m.group(1)
            sv = ""
        if not sv:
            if pos == "NOUN" and gender:
                sv = f"{gender} {lemma}"
            elif pos == "VERB":
                sv = f"att {lemma}"
            else:
                sv = lemma
        if sv in seen:
            continue
        seen.add(sv)
        words.append((sv, ru, pos, CEFR.get(cefr, 0)))
        if len(words) >= LIMIT:
            break

    allowed = set(map(chr, CHARSET))
    bad = {c for sv, ru, *_ in words for c in sv + ru if c not in allowed}
    if bad:
        sys.exit("символов нет в шрифтах: " + " ".join(f"{c!r} U+{ord(c):04X}" for c in sorted(bad)))

    with open(os.path.join(ROOT, "data/words.tsv"), "w", encoding="utf-8") as f:
        f.write("# rank\tsv\tru\tpos\tcefr\n")
        for i, (sv, ru, pos, cefr) in enumerate(words, 1):
            f.write(f"{i}\t{sv}\t{ru}\t{pos}\t{'-' if not cefr else list(CEFR)[cefr - 1]}\n")

    with open(os.path.join(ROOT, "src/words.h"), "w", encoding="utf-8") as f:
        f.write("// Сгенерировано tools/gen_words.py — не редактировать вручную.\n")
        f.write("// Слова отсортированы по частоте: первые N — «топ N».\n")
        f.write("#pragma once\n#include <stdint.h>\n\n")
        f.write("enum Pos : uint8_t { NOUN, VERB, ADJ, OTHER, NUM };\n\n")
        f.write("struct Word {\n  const char* sv;\n  const char* ru;\n  Pos pos;\n"
                "  uint8_t cefr;  // 0 — нет данных, 1..6 — A1..C2\n};\n\n")
        f.write("static const Word WORDS[] = {\n")
        for sv, ru, pos, cefr in words:
            f.write(f"  {{{c_str(sv)}, {c_str(ru)}, {pos}, {cefr}}},\n")
        f.write("};\n\nstatic const uint16_t WORD_COUNT = sizeof(WORDS) / sizeof(WORDS[0]);\n")
    print(f"{len(words)} слов → src/words.h, data/words.tsv")


if __name__ == "__main__":
    main()
