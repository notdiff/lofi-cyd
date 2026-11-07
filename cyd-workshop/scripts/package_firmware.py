"""Merge both PlatformIO builds into installable ESP32 images at address 0x0."""
from pathlib import Path
import argparse
import hashlib
import json
import subprocess
import sys

root = Path(__file__).resolve().parents[1]
parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('--core-dir', type=Path, default=Path.home() / '.platformio')
args = parser.parse_args()
core = args.core_dir.resolve()
esptool = core / 'packages/tool-esptoolpy/esptool.py'
boot_app = core / 'packages/framework-arduinoespressif32/tools/partitions/boot_app0.bin'
manifest = {'version':'0.6.1','chip':'esp32','flash_size':'4MB','flash_address':'0x0','images':{}}
for env, name in [('cyd_st7789','cyd-st7789.bin'),('cyd_ili9341','cyd-ili9341.bin')]:
    build = root / '.pio/build' / env
    parts = [(0x1000,build/'bootloader.bin'),(0x8000,build/'partitions.bin'),
             (0xe000,boot_app),(0x10000,build/'firmware.bin')]
    output = root / 'firmware' / name
    command = [sys.executable,str(esptool),'--chip','esp32','merge_bin','-o',str(output),
               '--target-offset','0','--flash_mode','dio','--flash_freq','40m','--flash_size','4MB']
    for address,path in parts:
        command.extend([hex(address),str(path)])
    subprocess.run(command,check=True)
    merged = output.read_bytes()
    for address,path in parts:
        data = path.read_bytes()
        assert merged[address:address+len(data)] == data, f'{name}: incorrect offset {address:x}'
    info = subprocess.check_output([sys.executable,str(esptool),'--chip','esp32','image_info',
                                    str(build/'firmware.bin')],text=True)
    assert 'Checksum:' in info and '(valid)' in info and 'Validation Hash:' in info, info
    assert '(invalid)' not in info, info
    manifest['images'][name] = {'bytes':len(merged),'sha256':hashlib.sha256(merged).hexdigest()}
(root/'firmware/manifest.json').write_text(json.dumps(manifest,indent=2)+'\n')
print('Both merged images verified; SHA256 written to firmware/manifest.json')
