"""Encode the three additional approved backgrounds; preserve existing fonts/assets.

The clock font is a 1.6x bitmap enlargement of the existing DejaVu font, so no new
font file or font substitution is needed. Run with Pillow installed.
"""
from pathlib import Path
import math
import re
from PIL import Image

root = Path(__file__).resolve().parents[1]
out = ['#pragma once', '#include "assets.h"']
for name, symbol in [('screen1-clock.png','kClockBackground'),
                     ('screen3-miner-details.png','kDetailsBackground'),
                     ('screen4-btc-network.png','kNetworkBackground')]:
    image = Image.open(root / 'assets/backgrounds' / name).convert('RGB')
    assert image.size == (320,240)
    pixels = [((r >> 3) << 11) | ((g >> 2) << 5) | (b >> 3) for r,g,b in image.getdata()]
    out.append(f'static const uint16_t {symbol}[320 * 240] PROGMEM = {{')
    for i in range(0,len(pixels),16):
        out.append(','.join(f'0x{v:04x}' for v in pixels[i:i+16])+',')
    out.append('};')
out.append('static const uint16_t *const kPageBackgrounds[] = {kClockBackground,kBackground,kDetailsBackground,kNetworkBackground};')
assets = (root / 'src/assets.h').read_text()
bits = [int(v) for v in re.search(r'kBits30\[\].*?= \{(.*?)\};',assets,re.S)[1].replace('\n','').split(',') if v]
glyphs = [tuple(map(int,g)) for g in re.findall(r'\{(\d+),(\d+),(\d+)\}',re.search(r'kGlyphs30\[\] = \{(.*?)\};',assets,re.S)[1])]
height = int(re.search(r'kFont30 = .*?, (\d+)\};',assets)[1])
new_height = math.ceil(height * 1.6)
packed, scaled = [], []
for offset,width,advance in glyphs:
    new_width = math.ceil(width * 1.6)
    flags = []
    for y in range(new_height):
        for x in range(new_width):
            src_y, src_x = min(height-1,int(y / 1.6)),min(width-1,int(x / 1.6))
            bit = src_y * width + src_x
            flags.append(bool(bits[offset + bit // 8] & (0x80 >> (bit % 8))))
    scaled.append((len(packed),new_width,round(advance * 1.6)))
    for i in range(0,len(flags),8):
        packed.append(sum(int(v) << (7-j) for j,v in enumerate(flags[i:i+8])))
out.append('static const uint8_t kBits48[] PROGMEM = {')
for i in range(0,len(packed),32): out.append(','.join(map(str,packed[i:i+32]))+',')
out.append('};\nstatic const Glyph kGlyphs48[] = {')
out.extend(f'{{{a},{b},{c}}},' for a,b,c in scaled)
out.append(f'}};\nstatic const BitmapFont kFont48 = {{kBits48,kGlyphs48,{new_height}}};')
(root / 'src/pages_assets.h').write_text('\n'.join(out)+'\n')
print('Encoded three 320x240 backgrounds and clock font; original assets.h unchanged')
