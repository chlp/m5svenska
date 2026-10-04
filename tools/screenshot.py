#!/usr/bin/env python3
"""Снимок экрана с устройства: python3 tools/screenshot.py /dev/cu.usbmodemXXX out.png"""
import sys, time, serial
from PIL import Image

port, out = sys.argv[1], sys.argv[2]
s = serial.Serial(port, 115200, timeout=5)
time.sleep(0.3)
s.reset_input_buffer()
s.write(b"S")
line = b""
while not line.startswith(b"SHOT"):
    line = s.readline()
    if not line:
        sys.exit("нет ответа")
w, h = map(int, line.split()[1:3])
raw = s.read(w * h * 2)
img = Image.new("RGB", (w, h))
px = img.load()
for i in range(w * h):
    v = (raw[2 * i] << 8) | raw[2 * i + 1]
    px[i % w, i // w] = (((v >> 11) & 31) * 255 // 31, ((v >> 5) & 63) * 255 // 63, (v & 31) * 255 // 31)
img.resize((w * 2, h * 2), Image.NEAREST).save(out)
print("ok", out)
