"""Derived texture maps for the Remastered look (ssx.Render.Look 1), made from the converted disc textures.

    python tools/make_remaster_maps.py [--force] [--only <substring>]

For every texture under converted/tracks/<track>/textures/ and converted/characters/<rider>/textures/ it writes,
under converted/remaster/<same relative path without .png>:
    _C.png  colour + alpha upscaled (x4 up to 128 px, x2 up to 256 px, else x1): Lanczos + a light unsharp mask
            (no machine-learning upscaler: nothing here invents detail the original texture does not have)
    _N.png  tangent-space normal map (DirectX convention, green = +v down) from the luminance height, wrapped at the
            edges because the textures tile
    _M.png  detail masks: R roughness offset (0.5 = none), G sparkle mask (bright desaturated texels), B emissive mask
            (texels much brighter and more saturated than their texture), A cavity occlusion (1 = open)
and converted/remaster/manifest.json listing every output. Game-derived data: everything stays git-ignored and
Scripts/import_remaster.py imports it into /Game/RemasterData (also git-ignored).
"""
import libpath  # noqa: F401,E402  (tools/lib on sys.path)
import argparse
import json
import os
import sys
from concurrent.futures import ProcessPoolExecutor
from pathlib import Path

import numpy as np
from PIL import Image, ImageFilter

ROOT = Path(__file__).resolve().parents[1]
CONVERTED = ROOT / "converted"
OUT = CONVERTED / "remaster"
SOURCES = ("tracks/*/textures/*.png", "characters/*/textures/*.png")

NORMAL_STRENGTH = 2.0     # height gradient scale at the upscaled resolution (heights 0..1)
SPARKLE_LUMA = 0.72       # fraction of the texture's brightest luminance above which a desaturated texel may sparkle
EMISSIVE_SIGMA = 2.0      # emissive: luminance this many standard deviations above the texture mean ...
EMISSIVE_SAT = 0.45       # ... and at least this saturated
VERSION = 2


def scale_for(w, h):
    m = max(w, h)
    return 4 if m <= 128 else 2 if m <= 256 else 1


def dilate_colour(img):
    """Give fully transparent texels the colour of their nearest opaque neighbours (repeated 1-texel growth, wrapped),
    so filtering and upscaling do not pull the PS2's transparent-texel colour into the edges."""
    a = np.asarray(img, dtype=np.float32)
    known = a[..., 3] > 0
    if known.all() or not known.any():
        return img
    rgb = a[..., :3] * known[..., None]
    for _ in range(max(img.width, img.height)):
        if known.all():
            break
        acc = np.zeros_like(rgb)
        cnt = np.zeros(known.shape, dtype=np.float32)
        for dy, dx in ((-1, 0), (1, 0), (0, -1), (0, 1), (-1, -1), (-1, 1), (1, -1), (1, 1)):
            k = np.roll(np.roll(known, dy, 0), dx, 1)
            acc += np.roll(np.roll(rgb, dy, 0), dx, 1) * k[..., None]
            cnt += k
        grow = (~known) & (cnt > 0)
        rgb[grow] = acc[grow] / cnt[grow][:, None]
        known = known | grow
    a[..., :3] = rgb
    return Image.fromarray(np.clip(a + 0.5, 0, 255).astype(np.uint8), "RGBA")


def upscale(img, s):
    img = dilate_colour(img)
    if s == 1:
        return img.copy()
    big = img.resize((img.width * s, img.height * s), Image.LANCZOS)
    rgb = big.convert("RGB").filter(ImageFilter.UnsharpMask(radius=1.2, percent=60, threshold=2))
    out = rgb.convert("RGBA")
    out.putalpha(big.getchannel("A"))
    return out


def blur_wrap(a, r):
    """Box blur of radius r with wrap-around (tiling textures)."""
    out = a.copy()
    for axis in (0, 1):
        acc = np.zeros_like(out)
        for d in range(-r, r + 1):
            acc += np.roll(out, d, axis=axis)
        out = acc / (2 * r + 1)
    return out


def luminance(rgb):
    return rgb[..., 0] * 0.299 + rgb[..., 1] * 0.587 + rgb[..., 2] * 0.114


def saturation(rgb):
    mx, mn = rgb.max(axis=-1), rgb.min(axis=-1)
    return np.where(mx > 1e-4, (mx - mn) / np.maximum(mx, 1e-4), 0.0)


def maps(big):
    a = np.asarray(big, dtype=np.float32) / 255.0
    rgb = a[..., :3]
    lum = luminance(rgb)
    h = blur_wrap(lum, 1)
    dx = (np.roll(h, -1, axis=1) - np.roll(h, 1, axis=1)) * 0.5
    dy = (np.roll(h, -1, axis=0) - np.roll(h, 1, axis=0)) * 0.5
    n = np.stack([-dx * NORMAL_STRENGTH * 4.0, -dy * NORMAL_STRENGTH * 4.0, np.ones_like(h)], axis=-1)
    n /= np.linalg.norm(n, axis=-1, keepdims=True)
    normal = np.clip((n * 0.5 + 0.5) * 255.0 + 0.5, 0, 255).astype(np.uint8)

    sat = saturation(rgb)
    local = blur_wrap(lum, 2)
    var = blur_wrap((lum - local) ** 2, 2)
    rough = np.clip(0.5 + (np.sqrt(var) - 0.04) * 3.0, 0.3, 0.8)
    rel = lum / max(float(np.percentile(lum, 99.5)), 0.05)   # PS2 textures: 0x80 is full brightness
    sparkle = np.clip((rel - SPARKLE_LUMA) / (1.0 - SPARKLE_LUMA), 0, 1) * np.clip(1.0 - sat * 3.0, 0, 1)
    mean, std = float(lum.mean()), float(lum.std()) + 1e-3
    emissive = np.clip((lum - (mean + EMISSIVE_SIGMA * std)) / 0.1, 0, 1) * (sat >= EMISSIVE_SAT)
    cavity = np.clip(1.0 - np.maximum(local - lum, 0) * 4.0, 0.4, 1.0)
    masks = np.stack([rough, sparkle, emissive, cavity], axis=-1)
    masks = np.clip(masks * 255.0 + 0.5, 0, 255).astype(np.uint8)
    return normal, masks


def build(job):
    src, dst_stem, force = job
    src, dst_stem = Path(src), Path(dst_stem)
    outs = [dst_stem.with_name(dst_stem.name + s) for s in ("_C.png", "_N.png", "_M.png")]
    stamp = dst_stem.with_name(dst_stem.name + f".v{VERSION}")
    if not force and stamp.is_file() and all(o.is_file() and o.stat().st_mtime >= src.stat().st_mtime for o in outs):
        return str(src), "up to date", outs
    img = Image.open(src).convert("RGBA")
    big = upscale(img, scale_for(img.width, img.height))
    normal, masks = maps(big)
    dst_stem.parent.mkdir(parents=True, exist_ok=True)
    big.save(outs[0])
    Image.fromarray(normal, "RGB").save(outs[1])
    Image.fromarray(masks, "RGBA").save(outs[2])
    for old in dst_stem.parent.glob(dst_stem.name + ".v*"):
        old.unlink()
    stamp.touch()
    return str(src), f"{img.width}x{img.height} -> {big.width}x{big.height}", outs


def main():
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument("--force", action="store_true", help="rebuild every map")
    ap.add_argument("--only", default="", help="only sources whose path contains this text")
    args = ap.parse_args()
    if not (CONVERTED / "tracks").is_dir():
        sys.exit(f"{CONVERTED / 'tracks'} not found: convert the disc first (build_data.bat)")
    jobs = []
    for pattern in SOURCES:
        for src in sorted(CONVERTED.glob(pattern)):
            rel = src.relative_to(CONVERTED)
            if args.only and args.only not in rel.as_posix():
                continue
            jobs.append((str(src), str(OUT / rel.with_suffix("")), args.force))
    built = 0
    manifest = []
    with ProcessPoolExecutor(max_workers=max(1, (os.cpu_count() or 2) - 1)) as pool:
        for src, what, outs in pool.map(build, jobs, chunksize=8):
            built += what != "up to date"
            rel = Path(src).relative_to(CONVERTED).with_suffix("").as_posix()
            manifest.append({"source": rel, "maps": [o.relative_to(OUT).as_posix() for o in outs]})
    OUT.mkdir(parents=True, exist_ok=True)
    (OUT / "manifest.json").write_text(json.dumps({"version": VERSION, "textures": manifest}, indent=1))
    print(f"remaster maps: {len(jobs)} textures, {built} rebuilt -> {OUT}")


if __name__ == "__main__":
    main()
