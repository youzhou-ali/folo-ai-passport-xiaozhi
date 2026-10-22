#!/usr/bin/env python3
"""Render the actual Passport LVGL view: python3 scripts/preview_eye_ui.py.

Requires a C/C++ host compiler and Pillow. LVGL v9.5.0 is cloned into /tmp on
first use. Firmware sources/configuration and the connected device are untouched.
"""

import argparse
import concurrent.futures
import hashlib
import json
import os
import shutil
import struct
import subprocess
import tempfile
from pathlib import Path

from PIL import Image, ImageDraw


ROOT = Path(__file__).resolve().parents[1]
SUPPORT = ROOT / "scripts" / "eye_preview"
BOARD = ROOT / "main" / "boards" / "folotoy" / "ai-passport"
LVGL_COMMIT = "85aa60d18b3d5e5588d7b247abf90198f07c8a63"


def extract_font(assets_path, destination):
    data = assets_path.read_bytes()
    manifest_path = assets_path.with_name("verified-flash-manifest.json")
    if manifest_path.exists():
        manifest = json.loads(manifest_path.read_text())
        expected = next(
            item["sha256"]
            for item in manifest["components"]
            if item["filename"] == assets_path.name
        )
        if hashlib.sha256(data).hexdigest() != expected:
            raise ValueError("Release assets SHA256 does not match verified manifest")
    count, checksum, stored_length = struct.unpack_from("<III", data)
    if stored_length != len(data) - 12:
        raise ValueError("Invalid asset pack length")
    if sum(data[12:]) & 0xFFFF != checksum:
        raise ValueError("Invalid asset pack checksum")
    base = 12 + count * 44
    for index in range(count):
        name, size, offset, _, _ = struct.unpack_from("<32sIIHH", data, 12 + index * 44)
        if name.split(b"\0")[0] != b"font_noto_sans_common_20_4.bin":
            continue
        if data[base + offset : base + offset + 2] != b"ZZ":
            raise ValueError("Invalid font asset prefix")
        payload = data[base + offset + 2 : base + offset + 2 + size]
        if len(payload) != size:
            raise ValueError("Truncated font")
        destination.write_bytes(payload)
        print(f"Verified release font: {size} bytes", flush=True)
        return
    raise ValueError("The common 20px Chinese font is missing from release assets")


def build_renderer(lvgl, build, jobs):
    config = SUPPORT / "lv_conf.h"
    common = [
        "-O1",
        "-g",
        f'-DLV_CONF_PATH="{config}"',
        "-I",
        str(lvgl),
        "-I",
        str(SUPPORT),
        "-I",
        str(BOARD),
        "-I",
        str(ROOT / "main"),
    ]
    sources = sorted((lvgl / "src").rglob("*.c")) + [
        BOARD / "eye_model.cc",
        BOARD / "eye_view.cc",
        SUPPORT / "render_eye.cc",
    ]
    dependency_time = max(
        path.stat().st_mtime
        for path in [config, BOARD / "eye_model.h", BOARD / "eye_view.h"]
    )

    def compile_source(source):
        key = hashlib.sha256(str(source).encode()).hexdigest()[:16]
        obj = build / f"{source.stem}-{key}.o"
        if obj.exists() and obj.stat().st_mtime >= max(source.stat().st_mtime, dependency_time):
            return obj
        compiler = shutil.which("cc" if source.suffix == ".c" else "c++")
        if compiler is None:
            raise RuntimeError("A C and C++ host compiler is required")
        standard = "-std=c99" if source.suffix == ".c" else "-std=c++17"
        subprocess.run(
            [compiler, standard, *common, "-c", str(source), "-o", str(obj)],
            check=True,
            capture_output=True,
            text=True,
        )
        return obj

    print(f"Building LVGL v9.5.0 software renderer ({len(sources)} sources)...", flush=True)
    with concurrent.futures.ThreadPoolExecutor(max_workers=jobs) as executor:
        objects = list(executor.map(compile_source, sources))
    binary = build / "render_eye"
    subprocess.run(["c++", *(str(obj) for obj in objects), "-o", str(binary)], check=True)
    return binary


def contact_sheet(output):
    names = ["idle", "listening", "thinking", "speaking", "happy", "error"]
    sheet = Image.new("RGB", (3 * 264 + 24, 2 * 362 + 24), "#101c29")
    draw = ImageDraw.Draw(sheet)
    for index, name in enumerate(names):
        frame = Image.open(output / f"{name}.ppm")
        frame.save(output / f"{name}.png")
        x = 24 + index % 3 * 264
        y = 32 + index // 3 * 362
        sheet.paste(frame, (x, y))
        draw.text((x, y - 19), name.upper(), fill="#cedde7")
    sheet.save(output / "eye-states.png")
    scroll_sheet = Image.new("RGB", (552, 376), "#101c29")
    scroll_draw = ImageDraw.Draw(scroll_sheet)
    for index, name in enumerate(["subtitle-start", "subtitle-scrolled"]):
        frame = Image.open(output / f"{name}.ppm")
        frame.save(output / f"{name}.png")
        x = 24 + index * 264
        scroll_sheet.paste(frame, (x, 32))
        scroll_draw.text((x, 13), "START" if index == 0 else "AFTER 5 SECONDS", fill="#cedde7")
    scroll_sheet.save(output / "subtitle-scroll.png")
    for path in output.glob("*.ppm"):
        path.unlink()


def animated_gif(frames_directory, output):
    frames = []
    hashes = set()
    for path in sorted(frames_directory.glob("frame-*.ppm")):
        with Image.open(path) as image:
            frame = image.convert("RGB")
            hashes.add(hashlib.sha256(frame.tobytes()).digest())
            frames.append(frame)
    if len(frames) != 120 or len(hashes) < 60:
        raise ValueError("Expected 120 real animation samples with substantial frame variance")
    # Preserve the sampled UI's colors in one shared palette for stable glow.
    palette_source = Image.new("RGB", (240 * 6, 320))
    for index in range(6):
        palette_source.paste(frames[index * 20], (index * 240, 0))
    palette = palette_source.quantize(colors=128)
    frames = [frame.quantize(palette=palette, dither=Image.Dither.NONE) for frame in frames]
    destination = output / "eye-animation.gif"
    frames[0].save(
        destination,
        save_all=True,
        append_images=frames[1:],
        duration=100,
        loop=0,
        optimize=True,
        disposal=2,
    )
    with Image.open(destination) as animation:
        duration = 0
        encoded_hashes = set()
        for index in range(animation.n_frames):
            animation.seek(index)
            duration += animation.info["duration"]
            encoded_hashes.add(hashlib.sha256(animation.convert("RGB").tobytes()).digest())
        if duration != 12000 or animation.size != (240, 320) or len(encoded_hashes) < 60:
            raise ValueError("Animation dimensions, playback duration, or frame variance are incorrect")
    print(f"Animation: {len(frames)} samples, {len(encoded_hashes)} unique GIF frames, 12 seconds; "
          f"{destination.stat().st_size} bytes")
    print(destination)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--lvgl-dir", type=Path, default=Path("/tmp/folo-eye-lvgl"))
    parser.add_argument("--build-dir", type=Path, default=Path("/tmp/folo-eye-lvgl-build"))
    built_assets = ROOT / "build/generated_assets.bin"
    default_assets = built_assets if built_assets.exists() else ROOT / "flash-artifacts/v2.5.0-folo.1/assets.bin"
    parser.add_argument("--assets", type=Path, default=default_assets)
    parser.add_argument("--output", type=Path, default=ROOT / "flash-artifacts/eye-preview")
    parser.add_argument("--jobs", type=int, default=min(6, os.cpu_count() or 1))
    parser.add_argument("--animate", action="store_true", help="Also render a 12-second, 10 fps GIF")
    args = parser.parse_args()
    if not args.lvgl_dir.exists():
        subprocess.run(
            ["git", "clone", "--depth", "1", "--branch", "v9.5.0",
             "https://github.com/lvgl/lvgl.git", str(args.lvgl_dir)],
            check=True,
        )
    actual_commit = subprocess.check_output(
        ["git", "-C", str(args.lvgl_dir), "rev-parse", "HEAD"], text=True
    ).strip()
    if actual_commit != LVGL_COMMIT:
        raise ValueError(f"Expected official LVGL v9.5.0 at {LVGL_COMMIT}, got {actual_commit}")
    args.build_dir.mkdir(parents=True, exist_ok=True)
    args.output.mkdir(parents=True, exist_ok=True)
    font = args.build_dir / "font_noto_sans_common_20_4.bin"
    extract_font(args.assets, font)
    binary = build_renderer(args.lvgl_dir, args.build_dir, args.jobs)
    with tempfile.TemporaryDirectory(prefix="folo-eye-frames-") as temporary:
        frames_directory = Path(temporary)
        command = [str(binary), str(font), str(args.output)]
        if args.animate:
            command.append(str(frames_directory))
        subprocess.run(command, check=True, timeout=30)
        if args.animate:
            animated_gif(frames_directory, args.output)
    contact_sheet(args.output)
    print(args.output / "eye-states.png")
    print(args.output / "subtitle-scroll.png")


if __name__ == "__main__":
    try:
        main()
    except subprocess.CalledProcessError as error:
        if error.stderr:
            print(error.stderr)
        raise
