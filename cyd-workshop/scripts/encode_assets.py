"""Encode the unmodified background and font glyphs as flash data for the ESP32."""
from pathlib import Path
from math import ceil
import argparse
from PIL import Image, ImageDraw, ImageFont

root = Path(__file__).resolve().parents[1]
background = Image.open(root / 'assets/background.png').convert('RGB')
assert background.size == (320, 240)
parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('--font', default='/usr/share/fonts/truetype/dejavu/DejaVuSans-Bold.ttf', help='Path to DejaVuSans-Bold.ttf')
font_path = parser.parse_args().font
out = ['#pragma once', '#include <Arduino.h>',
       'struct Glyph { uint32_t offset; uint8_t width; uint8_t advance; };',
       'struct BitmapFont { const uint8_t *bits; const Glyph *glyphs; uint8_t height; };']

pixels = [((r >> 3) << 11) | ((g >> 2) << 5) | (b >> 3)
          for r, g, b in background.getdata()]
out.append('static const uint16_t kBackground[320 * 240] PROGMEM = {')
for i in range(0, len(pixels), 16):
    out.append(','.join(f'0x{v:04x}' for v in pixels[i:i+16]) + ',')
out.append('};')

for size in [10, 13, 14, 30]:
    font = ImageFont.truetype(font_path, size)
    ascent, descent = font.getmetrics()
    height = ascent + descent
    packed, glyphs = [], []
    for c in map(chr, range(32, 127)):
        advance = max(1, round(font.getlength(c)))
        width = max(advance, ceil(font.getbbox(c, anchor='ls')[2]))
        glyph = Image.new('L', (width, height), 0)
        ImageDraw.Draw(glyph).text((0, ascent), c, font=font, anchor='ls', fill=255)
        bits = [v >= 100 for v in glyph.getdata()]
        data = []
        for i in range(0, len(bits), 8):
            data.append(sum(int(v) << (7-j) for j, v in enumerate(bits[i:i+8])))
        glyphs.append((len(packed), width, advance))
        packed.extend(data)
    out.append(f'static const uint8_t kBits{size}[] PROGMEM = {{')
    for i in range(0, len(packed), 32):
        out.append(','.join(str(v) for v in packed[i:i+32]) + ',')
    out.append('};')
    out.append(f'static const Glyph kGlyphs{size}[] = {{')
    out.extend(f'{{{a},{b},{c}}},' for a,b,c in glyphs)
    out.append('};')
    out.append(f'static const BitmapFont kFont{size} = {{kBits{size}, kGlyphs{size}, {height}}};')
    for text, limit in [('BTC/USD  DEMO', 140)] if size == 10 else []:
        assert sum(glyphs[ord(c)-32][2] for c in text) <= limit
    if size == 30:
        assert sum(glyphs[ord(c)-32][2] for c in '$96,420') <= 140
    if size == 14:
        for text in ['1.10 TH/s', '58 C  23.9 W', '23 SHARES']:
            assert sum(glyphs[ord(c)-32][2] for c in text) <= 139
    if size == 13:
        assert sum(glyphs[ord(c)-32][2] for c in '+2.4% / 24h') <= 140

(root / 'src/assets.h').write_text('\n'.join(out) + '\n')
print(f'Encoded {len(pixels)} RGB565 pixels and four fonts.')
