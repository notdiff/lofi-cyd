from pathlib import Path
import argparse
import hashlib
import json
import os
import re
import shutil
import struct
import subprocess
import sys

ROOT = Path(__file__).resolve().parents[1]
VARIANTS = (("cyd_st7789", "cyd-st7789"), ("cyd_ili9341", "cyd-ili9341"))


def digest(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def application_limit(partitions):
    data = partitions.read_bytes()
    for offset in range(0, len(data) - 31, 32):
        magic, kind, subtype, address, size, label, flags = struct.unpack_from(
            "<HBBII16sI", data, offset
        )
        if magic == 0x50AA and kind == 0 and address == 0x10000:
            return size
    raise ValueError("No application partition at 0x10000")


def package_variant(environment, name, core, output):
    build = ROOT / ".pio/build" / environment
    esptool = core / "packages/tool-esptoolpy/esptool.py"
    parts = (
        (0x1000, build / "bootloader.bin"),
        (0x8000, build / "partitions.bin"),
        (0xE000, core / "packages/framework-arduinoespressif32/tools/partitions/boot_app0.bin"),
        (0x10000, build / "firmware.bin"),
    )
    app_bytes = (build / "firmware.bin").stat().st_size
    limit = application_limit(build / "partitions.bin")
    assert app_bytes <= limit, f"{name}: application {app_bytes} exceeds partition {limit}"
    merged = output / f"{name}.bin"
    command = [
        sys.executable,
        str(esptool),
        "--chip",
        "esp32",
        "merge_bin",
        "-o",
        str(merged),
        "--target-offset",
        "0",
        "--flash_mode",
        "dio",
        "--flash_freq",
        "40m",
        "--flash_size",
        "4MB",
    ]
    for address, path in parts:
        command.extend((hex(address), str(path)))
    subprocess.run(command, check=True)
    binary = merged.read_bytes()
    for address, path in parts:
        content = path.read_bytes()
        assert (
            binary[address : address + len(content)] == content
        ), f"{name}: invalid offset {address:x}"
    info = subprocess.check_output(
        [
            sys.executable,
            str(esptool),
            "--chip",
            "esp32",
            "image_info",
            str(build / "firmware.bin"),
        ],
        text=True,
    )
    assert "Checksum:" in info and "Validation Hash:" in info and "(valid)" in info, info
    assert "(invalid)" not in info, info
    app = output / f"{name}-app.bin"
    shutil.copy2(build / "firmware.bin", app)
    return {
        "file": merged.name,
        "address": "0x0",
        "bytes": merged.stat().st_size,
        "sha256": digest(merged),
        "app_file": app.name,
        "app_address": "0x10000",
        "app_bytes": app_bytes,
        "app_sha256": digest(app),
        "app_partition_bytes": limit,
    }


def main():
    parser = argparse.ArgumentParser(
        description="Package verified full and application-only ESP32 images"
    )
    parser.add_argument(
        "--core-dir",
        type=Path,
        default=Path(os.environ.get("PLATFORMIO_CORE_DIR", Path.home() / ".platformio")),
    )
    args = parser.parse_args()
    output = ROOT / "dist/firmware"
    output.mkdir(parents=True, exist_ok=True)
    version = re.search(r'kVersion\[\] = "([^"]+)"', (ROOT / "src/config.h").read_text())[1]
    manifest = {
        "version": version,
        "chip": "esp32",
        "flash_size": "4MB",
        "images": {
            name: package_variant(env, name, args.core_dir.resolve(), output)
            for env, name in VARIANTS
        },
    }
    (output / "manifest.json").write_text(json.dumps(manifest, indent=2) + "\n")
    shutil.copy2(ROOT / "LICENSES.txt", output / "LICENSES.txt")
    if (ROOT / "LICENSE").exists():
        shutil.copy2(ROOT / "LICENSE", output / "LICENSE")
    print("Verified firmware images and SHA256 manifest written to", output)


if __name__ == "__main__":
    main()
