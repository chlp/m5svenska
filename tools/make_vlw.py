#!/usr/bin/env python3
"""Генератор сглаженных VLW-шрифтов (формат LovyanGFX/M5GFX) из TTF.

Пишет C-заголовок с массивом PROGMEM, который грузится через
canvas.loadFont(font_xxx).

    python3 tools/make_vlw.py "/System/Library/Fonts/Supplemental/Arial Bold.ttf" 40 font_b40 src/fonts/font_b40.h
"""
import struct
import sys

from PIL import Image, ImageDraw, ImageFont

CHARSET = (
    list(range(0x20, 0x7F))            # ASCII
    + list(range(0xC0, 0x100))         # Latin-1: å ä ö é ü …
    + [0xAB, 0xBB, 0xB7]               # « » ·
    + [0x401] + list(range(0x410, 0x450)) + [0x451]  # кириллица + Ё ё
    + [0x2013, 0x2014, 0x2026, 0x2039, 0x203A, 0x2190, 0x2192]       # – — … ← →
)


def glyph(font, cp):
    ch = chr(cp)
    adv = int(round(font.getlength(ch)))
    l, t, r, b = font.getbbox(ch, anchor="ls")  # координаты от базовой линии
    w, h = max(0, r - l), max(0, b - t)
    if w == 0 or h == 0:
        return dict(cp=cp, w=0, h=0, adv=adv, dy=0, dx=0, bmp=b"")
    img = Image.new("L", (w, h), 0)
    ImageDraw.Draw(img).text((-l, -t), ch, font=font, fill=255, anchor="ls")
    return dict(cp=cp, w=w, h=h, adv=adv, dy=-t, dx=l, bmp=img.tobytes())


def main():
    ttf, size, name, out = sys.argv[1], int(sys.argv[2]), sys.argv[3], sys.argv[4]
    font = ImageFont.truetype(ttf, size)
    notdef = glyph(font, 0xE000)["bmp"]
    glyphs = []
    for cp in sorted(set(CHARSET)):
        g = glyph(font, cp)
        if cp != 0x20 and g["bmp"] == notdef:
            print(f"  skip U+{cp:04X} (нет в шрифте)")
            continue
        glyphs.append(g)

    ascent = -font.getbbox("d", anchor="ls")[1]
    descent = font.getbbox("p", anchor="ls")[3]
    data = bytearray(struct.pack(">6i", len(glyphs), 11, size, 0, ascent, descent))
    for g in glyphs:
        data += struct.pack(">7i", g["cp"], g["h"], g["w"], g["adv"], g["dy"], g["dx"], 0)
    for g in glyphs:
        data += g["bmp"]

    with open(out, "w") as f:
        f.write(f"// Сгенерировано tools/make_vlw.py из {ttf.split('/')[-1]} {size}px\n#pragma once\n#include <Arduino.h>\n\n")
        f.write(f"const uint8_t {name}[] PROGMEM = {{\n")
        for i in range(0, len(data), 24):
            f.write("  " + ",".join(f"0x{b:02x}" for b in data[i:i + 24]) + ",\n")
        f.write("};\n")
    print(f"{out}: {len(glyphs)} глифов, {len(data)} байт")


if __name__ == "__main__":
    main()
