"""Verify firmware sprite decoding and export its exact compositor/animation as GIFs."""
from pathlib import Path
import json
import subprocess
import sys
from PIL import Image

root = Path(__file__).resolve().parents[1]
exe = root / 'tests/.build/rabbit-test'
subprocess.run(['c++','-std=c++11','-Wall','-Wextra','-Werror',
                '-I'+str(root/'tests/include'),'-I'+str(root/'src'),
                str(root/'tests/rabbit_test.cpp'),'-o',str(exe)],check=True)
subprocess.run([str(exe)],check=True)
decoded = subprocess.check_output([str(exe),'--frames'])
at = 0
manifest = json.loads((root/'assets/mascot/manifest.json').read_text())
for group,meta in manifest['groups'].items():
    count,width,height = len(meta['frames']),meta['width'],meta['height']
    if group in ('falltool','fallnotebook'):
        assert len({f['scale'] for f in meta['frames']}) == 1, 'reaction changes scale between frames'
    for frame in range(count):
        size = width*height*4
        golden = Image.open(root/'assets/mascot/frames'/f'{group}-{frame}.png').convert('RGBA')
        assert decoded[at:at+size] == golden.tobytes(), f'{group}/{frame}: firmware decoding differs from PNG'
        at += size
assert at == len(decoded)
print('PASS: all 44 firmware sprite decodes match PNG pixel-for-pixel; reaction scale stays fixed')

names = ['clock','overview','miner-details','btc-network']
previews = root / 'firmware/previews'
for page,name in enumerate(names):
    base = Image.open(previews/f'{name}-live.png').convert('RGBA')
    x,y,w,h = json.loads(subprocess.check_output([str(exe),'--region',str(page)]))
    if not w:
        base.save(previews/f'{name}-rabbit.png')
        base.convert('RGB').save(previews/f'{name}-rabbit.gif',duration=45000,loop=0)
        continue
    cache = {}
    def compose(frame,lift,action='idle'):
        group = action if action == 'fall' else 'idle'
        key = (frame,lift,group)
        if key not in cache:
            pixels = subprocess.check_output([str(exe),'--composite',str(page),str(frame),str(lift),group])
            region = Image.frombytes('RGBA',(w,h),pixels)
            image = base.copy(); image.paste(region,(x,y)); cache[key] = image
        return cache[key]
    compose(0,0).save(previews/f'{name}-rabbit.png')
    for mode,suffix in [('--timeline','rabbit'),('--reaction','tap')]:
        timeline = json.loads(subprocess.check_output([str(exe),mode,str(page)]))
        frames,durations = [],[]
        for i,state in enumerate(timeline[:-1]):
            duration = timeline[i+1]['ms']-state['ms']
            frames.append(compose(state['frame'],state['lift'],state['action']).convert('RGB'))
            durations.append(max(10,round(duration/10)*10))
        frames[0].save(previews/f'{name}-{suffix}.gif',save_all=True,append_images=frames[1:],
                       duration=durations,loop=0,optimize=True,disposal=1)
    if page in (1,3):
        for frame in range(12): compose(frame,0,'fall').save(previews/f'{name}-tap-{frame}.png')
    else:
        for path in previews.glob(f'{name}-tap-*.png'): path.unlink()
    # Explicit per-action stills make all generated poses reviewable on the scene.
    if page in (1,3):
        for label,frame in [('blink',2),('wave',4),('helmet',7)]:
            compose(frame,0).save(previews/f'{name}-rabbit-{label}.png')
print('Four exact 320x240 PNGs and GIF previews (miner details without mascot) written to',previews)
