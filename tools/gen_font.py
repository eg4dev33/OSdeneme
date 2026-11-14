#!/usr/bin/env python3
"""kernel/font.cpp dosyasini uretir (8x16 bitmap font, ASCII 32-126).
Gerekenler: pip install pillow  ve  DejaVu Sans Mono (Bitstream Vera lisansi, serbest)."""
import sys
from PIL import Image, ImageDraw, ImageFont

FONT = "/usr/share/fonts/truetype/dejavu/DejaVuSansMono.ttf"
f = ImageFont.truetype(FONT, 13)

def glyph(ch):
    for shift in (0, -1):                      # tasan harfleri 1 px sola kaydir
        img = Image.new("L", (24, 32), 0)
        d = ImageDraw.Draw(img); d.fontmode = "1"
        d.text((8 + shift, 8), ch, font=f, fill=255)
        bb = img.getbbox()
        if not bb or (bb[2] - 8 <= 8):
            break
    rows = []
    for y in range(16):
        bits = 0
        for x in range(8):
            if img.getpixel((8 + x, 8 + y)) > 127:
                bits |= 0x80 >> x
        rows.append(bits)
    return rows

out = ["// Otomatik uretildi: tools/gen_font.py (DejaVu Sans Mono 13 px -> 8x16 bitmap)",
       "#include <stdint.h>", "",
       "extern const uint8_t FONT8X16[128][16] = {"]
for c in range(128):
    rows = glyph(chr(c)) if 32 <= c <= 126 else [0] * 16
    label = repr(chr(c)) if 32 <= c <= 126 else str(c)
    out.append("    {" + ",".join("0x%02X" % r for r in rows) + "},  // " + label.replace("\\", "/"))
out.append("};")
open(sys.argv[1] if len(sys.argv) > 1 else "kernel/font.cpp", "w").write("\n".join(out) + "\n")
