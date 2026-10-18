"""Batch-convert every track (.pbd) under unpacked/MODELS to glTF in converted/tracks/.
Usage: python tools/convert_tracks.py [--tess N]
Expects tools/sshtex.py to have already written converted/textures/.
"""
import libpath  # noqa: F401,E402  (tools/lib on sys.path)
import argparse
from pathlib import Path

import pbd2gltf

ROOT = Path(__file__).resolve().parent.parent


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--tess", type=int, default=pbd2gltf.DEFAULT_TESS)
    args = ap.parse_args()
    for level in sorted((ROOT / "unpacked/MODELS").rglob("*.pbd")):
        rel = level.relative_to(ROOT / "unpacked").with_suffix("")
        tex_dir = ROOT / "converted/textures" / rel
        if not tex_dir.is_dir():
            print(f"skip {level.name}: no textures at {tex_dir}")
            continue
        try:
            pbd2gltf.build(level, tex_dir, ROOT / "converted/tracks" / level.stem, args.tess)
        except Exception as e:
            print(f"FAILED {level.name}: {e}")


if __name__ == "__main__":
    main()
