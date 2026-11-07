"""Run host-side parser/layout checks and render the exact firmware bitmap fonts.

Requires an existing PlatformIO build (ArduinoJson headers) and Pillow.
"""
from pathlib import Path
import argparse
import json
import re
import subprocess
import sys
from PIL import Image, ImageDraw

root = Path(__file__).resolve().parents[1]
parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('--btc-response', type=Path)
args = parser.parse_args()
build = root / 'tests/.build'
build.mkdir(parents=True, exist_ok=True)
exe = build / 'metrics-test'
swipe_exe = build / 'swipe-test'
subprocess.run(['c++', '-std=c++11', '-Wall', '-Wextra', '-Werror',
                '-I' + str(root / 'src'), str(root / 'tests/swipe_test.cpp'),
                '-o', str(swipe_exe)], check=True)
subprocess.run([str(swipe_exe)], check=True)
touch_exe = build / 'touch-gesture-test'
subprocess.run(['c++','-std=c++11','-Wall','-Wextra','-Werror',
                '-I'+str(root/'src'),str(root/'tests/touch_gesture_test.cpp'),
                '-o',str(touch_exe)],check=True)
subprocess.run([str(touch_exe)],check=True)
subprocess.run(['c++', '-std=c++17', '-Wall', '-Wextra', '-Werror',
                '-I' + str(root / 'tests/include'), '-I' + str(root / 'src'),
                '-I' + str(root / '.pio/libdeps/cyd_st7789/ArduinoJson/src'),
                str(root / 'tests/metrics_test.cpp'), '-o', str(exe)], check=True)
subprocess.run([str(exe)], check=True)
if args.btc_response:
    subprocess.run([str(exe), '--check-btc', str(args.btc_response.resolve())], check=True)
scenarios = json.loads(subprocess.check_output([str(exe), '--preview-pages']))
assets = (root / 'src/assets.h').read_text() + (root / 'src/pages_assets.h').read_text()
fonts = {}
for size in (10, 13, 14, 30, 48):
    bits = [int(n) for n in re.search(rf'kBits{size}\[\].*?= \{{(.*?)\}};', assets, re.S)[1].replace('\n','').split(',') if n]
    glyphs = [tuple(map(int,m)) for m in re.findall(r'\{(\d+),(\d+),(\d+)\}', re.search(rf'kGlyphs{size}\[\] = \{{(.*?)\}};',assets,re.S)[1])]
    height = int(re.search(rf'kFont{size} = .*?,\s*(\d+)\}};', assets)[1])
    fonts[size] = bits, glyphs, height

def color(value):
    return ((value >> 11 & 31) * 255 // 31, (value >> 5 & 63) * 255 // 63, (value & 31) * 255 // 31)

def text_bitmap(img, text, x, y, size, value):
    bits, glyphs, height = fonts[size]
    for char in text:
        offset, width, advance = glyphs[ord(char)-32]
        for row in range(height):
            for col in range(width):
                bit = row * width + col
                if bits[offset + bit // 8] & (0x80 >> (bit % 8)):
                    img.putpixel((x + col,y + row),color(value))
        x += advance

out = root / 'firmware/previews'
out.mkdir(exist_ok=True)
backgrounds = ['screen1-clock.png','screen2-overview.png','screen3-miner-details.png','screen4-btc-network.png']
names = ['clock','overview','miner-details','btc-network']
for scenario in scenarios:
    page = scenario['page']
    img = Image.open(root / 'assets/backgrounds' / backgrounds[page]).convert('RGB')
    img.putdata([color(((r >> 3) << 11) | ((g >> 2) << 5) | (b >> 3)) for r,g,b in img.getdata()])
    for field in scenario['fields']:
        text_bitmap(img,field['text'],field['x'],field['y'],field['font'],field['color'])
    if page == 1 and scenario['name'] != 'setup':
        indicator = 0x07FD if scenario['name']=='live' else (0x8C71 if scenario['name']=='waiting' else 0xFD20)
        ImageDraw.Draw(img).ellipse((30,209,34,213),fill=color(indicator))
    filename = 'wifi-setup.png' if scenario['name']=='setup' else f"{names[page]}-{scenario['name']}.png"
    img.save(out / filename)
print(f'{len(scenarios)} exact 320x240 firmware previews written to {out}')
subprocess.run([sys.executable,str(root/'scripts/render_rabbit.py')],check=True)
