# m5svenska

A pocket Swedish vocabulary trainer for **M5Stack CoreS3 / CoreS3 Lite (SE)**.
Fully controlled via the 320×240 touchscreen, no Wi‑Fi or external services.

> The interface and translations are Swedish ↔ Russian.

| Menu | Quiz SV → RU | Quiz RU → SV | Cards |
|---|---|---|---|
| ![](docs/menu.png) | ![](docs/quiz.png) | ![](docs/quiz-ru-sv.png) | ![](docs/cards.png) |

## Modes

At the top of the menu there is a dictionary switch: **Top 250 / Top 1000 / Top 10000**.
Quizzes and cards use only the N most frequent words. The choice is remembered.

- **SV → RU** — a Swedish word at the top, 4 Russian translation options below.
- **RU → SV** — a Russian word at the top, 4 Swedish options below.
- **Mix** — the direction is picked randomly for each question.
- **Kort (cards)** — the word is shown in both languages at once. Tap the left third
  of the screen to go back, anywhere else to go forward. The "auto" button flips
  cards by itself every 4 seconds (the bar at the bottom shows time until the next one).

In the quiz, after you answer, the correct option is highlighted green and a wrong
one red. The next question appears automatically (0.9 s after a correct answer,
2.2 s after a mistake); tap to skip ahead. Top right shows the score and the streak
(`×5`) of correct answers in a row.

## How it teaches

- Every word has a level 0–5 (dots under the word). A correct answer raises the
  level by 1, a mistake resets it to 0.
- Words are picked randomly with weight `(6 − level)²`: new and forgotten words
  come up ~36 times more often than learned ones. The last 10 words are not repeated.
- Distractor options come from the same part of speech (noun/verb/adjective/numbers/other),
  and synonyms (e.g. *att bo* / *att leva* — "to live") never appear together as
  options, so there are never two correct answers.
- Progress is stored in NVS flash (4 bits per word) and survives reboots.
  It is shared across all dictionary sizes. "Learned" in the menu means words with
  level ≥ 3 within the current top.
- A card shows the word's frequency rank (`#170`) and its CEFR level (A1–C2),
  if known.

## Design

Dark blue background with Swedish flag accent colors: Swedish words are yellow,
Russian ones white. All touch zones are at least 148×54 px. Fonts are anti-aliased
(Arial Bold 40/28/22 and Arial 16), generated in VLW format with Cyrillic, å/ä/ö/é.
Long text automatically shrinks to the next font size, and wraps onto two lines
if it still doesn't fit. Frames are drawn into a PSRAM buffer, so the screen doesn't flicker.

After 90 s without touches the backlight dims to minimum; the first tap only wakes the screen.
There is no sound — the speaker is disabled.

## Dictionary

10000 Swedish words sorted by frequency, with Russian translations:
nouns with their article (`en`/`ett`), verbs with `att`, adjectives,
adverbs, prepositions, set phrases (`i alla fall`, `ta hand om`), numbers.

Where the words come from:

- [Kelly list](https://spraakbanken.gu.se/resurser/kelly) (Språkbanken, University
  of Gothenburg) — 8425 lemmas selected for learners of Swedish, with CEFR levels
  and noun gender.
- [SALDO lemma frequencies](https://svn.spraakbanken.gu.se/sb-arkiv/pub/frekvens/) across all
  Språkbanken corpora. The whole list is sorted by them, and words missing from Kelly
  are taken from them.
- Junk was filtered out by hand: lemmatization errors, proper names, duplicates like
  `imorgon`/`i morgon`, profanity.
- Russian translations were made with the help of Claude. There are inaccuracies,
  especially in the tail of the list; corrections are welcome.

Files:

```
data/candidates.tsv   rank, lemma, part of speech, gender, CEFR — order is fixed
data/ru/*.tsv         translations: rank<TAB>translation[<TAB>custom spelling]
data/words.tsv        final dictionary (generated)
src/words.h           the same for the firmware (generated)
```

To fix a translation, edit the line in `data/ru/*.tsv`, then:

```bash
python3 tools/gen_words.py   # rebuilds data/words.tsv and src/words.h
pio run -t upload
```

In a translation line, `-` drops the word, and an `en:`/`ett:`/`pl:` prefix sets the
noun's gender. The third column sets a custom spelling of the Swedish word
(e.g. `ska` instead of `att skola`). `tools/build_candidates.py` shows how
`candidates.tsv` was built from Kelly and SALDO. Do not rebuild `candidates.tsv`
itself: the translations refer to its ranks.

The word order in `words.h` is the progress index. If the dictionary is rebuilt with
a different number of words, saved progress will be reset.

## Build and flash

```bash
pio run                     # build
pio run -t upload           # flash (port /dev/cu.usbmodem*)
```

If the port is not found: hold the RESET button for ~3 s until the green LED lights up
(bootloader mode) and try again.

### Fonts

```bash
python3 tools/make_vlw.py "/System/Library/Fonts/Supplemental/Arial Bold.ttf" 40 font_b40 src/fonts/font_b40.h
```

The character set is defined in `CHARSET` in `tools/make_vlw.py` (requires Pillow).

### Serial debugging (115200)

- `S` — screenshot: `SHOT 320 240\n` + raw RGB565 (big-endian).
  `python3 tools/screenshot.py /dev/cu.usbmodemXXXX out.png` (requires pyserial).
- `T x y` — emulate a tap at point (x, y).
- Every real touch is logged as `touch x y`.

## Structure

```
platformio.ini        configuration (board m5stack-cores3, M5Unified)
src/main.cpp          UI, quiz and card logic, progress
src/words.h           dictionary (generated)
src/fonts/*.h         VLW fonts (generated)
tools/make_vlw.py     TTF → VLW font generator
tools/screenshot.py   device screenshot tool
tools/gen_words.py    builds the dictionary from data/
tools/build_candidates.py  candidate selection from Kelly + SALDO
data/                 dictionary source data
docs/*.png            screenshots from a real device
```
