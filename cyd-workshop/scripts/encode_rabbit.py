"""Extract generated alpha cels, resize consistently and encode flash RGBA palettes.

No character is drawn here: artwork and transparency come from imagegen.
Palette/RLE output and decoded PNGs use identical RGB565/4-bit alpha colors.
"""
from pathlib import Path
import math
import json
from PIL import Image

root = Path(__file__).resolve().parents[1]
out = root / 'assets/mascot'
frames_dir = out / 'frames'
frames_dir.mkdir(exist_ok=True)
header = ['#pragma once', '#include "rabbit_sprite.h"']
manifest = {'version': 1, 'groups': {}}

def table(name, values, ctype, per_line=24):
    header.append(f'static const {ctype} {name}[] PROGMEM = {{')
    header.extend(','.join(str(v) for v in values[i:i+per_line])+',' for i in range(0,len(values),per_line))
    header.append('};')

specs = [('tool',4,2,100,132),('notebook',4,2,100,132),('sleeping',2,2,144,86),
         ('falltool',4,3,75,99),('fallnotebook',4,3,75,99)]
source_grids = {}

def extract(group, columns, rows):
    version = 2 if group in ('falltool','fallnotebook') else 1
    sheet = Image.open(out / 'source' / f'{group}-sheet-v{version}.png').convert('RGBA')
    row_edges = [i*sheet.height//rows for i in range(rows+1)]
    if version == 2:
        # The generated 4x3 sheets have three separated rows, but their gutters
        # are not at exact thirds. Split at actual transparent horizontal bands.
        alpha = sheet.getchannel('A')
        occupied = [sum(a >= 64 for a in alpha.crop((0,y,sheet.width,y+1)).getdata()) >= 8
                    for y in range(sheet.height)]
        gaps, start = [], None
        for y,active in enumerate(occupied+[True]):
            if not active and start is None: start = y
            if active and start is not None:
                if y-start >= 5 and start > 0 and y < sheet.height: gaps.append((start+y)//2)
                start = None
        assert len(gaps) >= rows-1, f'{group}: missing row gutters'
        for i in range(1,rows):
            edge = min(gaps,key=lambda y:abs(y-row_edges[i]))
            assert abs(edge-row_edges[i]) < sheet.height/(rows*3)
            row_edges[i] = edge
        assert row_edges == sorted(set(row_edges)), f'{group}: repeated row boundary'
    column_edges = [[i*sheet.width//columns for i in range(columns+1)] for _ in range(rows)]
    if version == 2:
        for row in range(rows):
            counts = [sum(a >= 64 for a in alpha.crop((x,row_edges[row],x+1,row_edges[row+1])).getdata())
                      for x in range(sheet.width)]
            for col in range(1,columns):
                target = col*sheet.width//columns; reach = sheet.width//(columns*5)
                column_edges[row][col] = min(range(target-reach,target+reach),
                    key=lambda x:(sum(counts[x-1:x+2]),abs(x-target)))
    source_grids[group] = {'columns':columns,'row_edges':row_edges,'column_edges':column_edges,'source_version':version}
    cels, bounds = [], []
    for row in range(rows):
        for col in range(columns):
            cel = sheet.crop((column_edges[row][col],row_edges[row],
                              column_edges[row][col+1],row_edges[row+1]))
            bbox = cel.getchannel('A').point(lambda a:255 if a >= 64 else 0).getbbox()
            assert bbox, f'{group}: empty cell'
            cels.append(cel.crop(bbox)); bounds.append(bbox)
    return cels,bounds

sources = {group:extract(group,columns,rows) for group,columns,rows,_,_ in specs}
# Props change a sheet's width: normalizing each sheet separately would shrink
# the screwdriver bunny more than the notebook bunny. Match idle height instead.
standing_height = math.floor(min(
    min(96/max(im.width for im in sources[group][0]),128/max(im.height for im in sources[group][0]))
    * sources[group][0][0].height for group in ('tool','notebook')))

for group,columns,rows,width,height in specs:
    cels,bounds = sources[group]
    # Use one shared scale; do not enlarge each pose to its individual rectangle.
    scale = min((width-4)/max(im.width for im in cels),(height-4)/max(im.height for im in cels))
    if group in ('tool','notebook'): scale = standing_height/cels[0].height
    if group in ('falltool','fallnotebook'):
        scale = min(scale,round(standing_height*height/132)/cels[0].height)
    frames = []
    for cel in cels:
        pose_scale = scale
        resized = cel.resize((round(cel.width*pose_scale),round(cel.height*pose_scale)),Image.Resampling.LANCZOS)
        frame = Image.new('RGBA',(width,height))
        frame.alpha_composite(resized,((width-resized.width)//2,height-2-resized.height))
        # Normalize existing transparency before quantization, not a new mask.
        frame.putdata([(r,g,b,round(a/17)*17) if a >= 9 else (0,0,0,0) for r,g,b,a in frame.getdata()])
        frames.append(frame)
    strip = Image.new('RGBA',(width,height*len(frames)))
    for i,frame in enumerate(frames): strip.paste(frame,(0,i*height))
    quantized = strip.quantize(colors=255,method=Image.Quantize.FASTOCTREE,dither=Image.Dither.NONE)
    rgba = quantized.convert('RGBA')
    unique = sorted(set(rgba.getdata()),key=lambda p:(p[3],p[0],p[1],p[2]))
    colors = [(0,0,0,0)] + [c for c in unique if c[3] > 0]
    assert len(colors) <= 256
    indexes = {color:i for i,color in enumerate(colors)}
    palette = [((r>>3)<<11)|((g>>2)<<5)|(b>>3) for r,g,b,a in colors]
    opacity = [min(15,round(a/17)) for r,g,b,a in colors]
    table(f'kRabbit{group}Palette',palette,'uint16_t')
    table(f'kRabbit{group}Alpha',opacity,'uint8_t')
    frame_meta = []
    for i in range(len(frames)):
        pixels = list(rgba.crop((0,i*height,width,(i+1)*height)).getdata())
        data, offsets = [], [0]
        decoded = Image.new('RGBA',(width,height))
        decoded_colors = []
        for r,g,b,a in pixels:
            idx = indexes[(r,g,b,a)] if a > 0 else 0
            value = palette[idx]
            decoded_colors.append(((value>>11&31)*255//31,(value>>5&63)*255//63,(value&31)*255//31,opacity[idx]*17))
        decoded.putdata(decoded_colors)
        decoded.save(frames_dir / f'{group}-{i}.png')
        for row in range(height):
            line = [indexes[p] if p[3] > 0 else 0 for p in pixels[row*width:(row+1)*width]]
            def repeated(at):
                end = at+1
                while end < width and end-at < 127 and line[end] == line[at]: end += 1
                return end-at
            x = 0
            while x < width:
                start = x
                if opacity[line[x]] == 0:
                    while x < width and x-start < 127 and opacity[line[x]] == 0: x += 1
                    data.append(128 | (x-start))
                elif repeated(x) >= 4:
                    count = repeated(x); data.extend([0,count,line[x]]); x += count
                else:
                    x += 1
                    while x < width and x-start < 127 and opacity[line[x]] > 0 and repeated(x) < 4: x += 1
                    data.append(x-start); data.extend(line[start:x])
            offsets.append(len(data))
        assert len(data) < 65536
        table(f'kRabbit{group}{i}Rows',offsets,'uint16_t')
        table(f'kRabbit{group}{i}Data',data,'uint8_t')
        frame_meta.append({'frame':i,'rle_bytes':len(data),'source_bbox':bounds[i],'scale':pose_scale})
    header.append(f'static const RabbitSprite kRabbit{group}[] = {{')
    for i in range(len(frames)):
        header.append(f'{{kRabbit{group}Palette,kRabbit{group}Alpha,kRabbit{group}{i}Rows,kRabbit{group}{i}Data,{width},{height}}},')
    header.append('};')
    manifest['groups'][group] = {'width':width,'height':height,'frames':frame_meta,'palette_entries':len(colors),
                               'source_grid':source_grids[group]}
    if group in ('tool','notebook'): manifest['groups'][group]['idle_height'] = standing_height
(root / 'src/rabbit_assets.h').write_text('\n'.join(header)+'\n')
(out / 'manifest.json').write_text(json.dumps(manifest,indent=2)+'\n')
print('Encoded 44 RGBA sprites:',sum(f['rle_bytes'] for g in manifest['groups'].values() for f in g['frames']),'RLE bytes')
