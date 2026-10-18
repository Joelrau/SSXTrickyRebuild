"""Export the per-patch terrain LOD vertex blocks the run-time terrain component draws (World/SSXTerrainLod.h).
Usage:
    python tools/export_terrain_lod.py                 # every track under unpacked/MODELS
    python tools/export_terrain_lod.py snow pipe       # selected tracks
"""
import libpath  # noqa: F401,E402  (tools/lib on sys.path)
import struct
import sys
from pathlib import Path

import numpy as np

ROOT = Path(__file__).resolve().parent.parent
sys.path.insert(0, str(ROOT / "tools"))
import pbd  # noqa: E402
import pbd2gltf  # noqa: E402
import terrain_lod  # noqa: E402

OUT = ROOT / "Unreal/SSXTrickyRebuild/SimData"
VERTS = len(terrain_lod.LATTICE)


def export(level_path: Path, out_path: Path) -> int:
    level = pbd.load(level_path)
    ids = [k for k, p in enumerate(level.patches) if p.texture >= 0]
    patches = [level.patches[k] for k in ids]
    grids = {n: pbd2gltf.tessellate(patches, n - 1) for n in (8, 6, 4)}
    pick = []
    for u, v in terrain_lod.LATTICE:
        n = next(n for n in (8, 6, 4) if (u * (n - 1)).denominator == 1 and (v * (n - 1)).denominator == 1)
        pick.append((n, int(u * (n - 1)), int(v * (n - 1))))
    out = bytearray(struct.pack("<4sIII4I", b"SSXL", 1, len(patches), VERTS, 0, 0, 0, 0))
    for k, p in zip(ids, patches):
        flags = 1 if p.flag_word < 0 else 0
        out += struct.pack("<ihH", k, p.texture, flags)
        for c in p.corners:
            out += struct.pack("<3f", *c)
        out += struct.pack("<2fI", *pbd2gltf.lightmap_origin(p), 0)
    for idx in range(len(patches)):
        for n, a, b in pick:
            pos, nrm, uv, local = (g[idx, a, b] for g in grids[n])
            q = np.clip(np.round(nrm * 127.0), -127, 127).astype(np.int8)
            out += struct.pack("<3f3bx2f2f", *pos, *q.tolist(), *uv, *local)
    out_path.write_bytes(out)
    return len(patches)


def main():
    want = {a.lower() for a in sys.argv[1:]}
    OUT.mkdir(parents=True, exist_ok=True)
    for level_path in sorted((ROOT / "unpacked/MODELS").rglob("*.pbd")):
        stem = level_path.stem
        if stem.endswith("_sky") or (want and stem not in want):
            continue
        n = export(level_path, OUT / f"{stem}.terrainlod.bin")
        print(f"{stem}: {n} patches -> {stem}.terrainlod.bin")


if __name__ == "__main__":
    main()
