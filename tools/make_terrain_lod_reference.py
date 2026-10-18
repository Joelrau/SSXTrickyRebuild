"""Reference of SSX.World.TerrainLod.*.
Usage:  python tools/make_terrain_lod_reference.py
Reads   unpacked/MODELS/<track>/data/models/<track>.pbd, SSX_TRACES/ram/paused_pipedream_moby_bx.bin (optional)
Writes  Unreal/SSXTrickyRebuild/SimData/terrain_lod_reference.json (git-ignored):
  levels  : [{corners, eye, levels}] random corner / eye sets around both radii, both radii from either side (0.05 cm), and per track the drawn patches at random eyes (corners from the .pbd)
"""
import libpath  # noqa: F401,E402  (tools/lib on sys.path)
import traces  # noqa: E402

traces.require()
TR = traces.root()

import json
import random
import struct
import sys
from pathlib import Path

import numpy as np

ROOT = Path(__file__).resolve().parent.parent
sys.path.insert(0, str(ROOT / "tools"))
import pbd  # noqa: E402
import terrain_lod as T  # noqa: E402

RAM = TR / "ram" / "paused_pipedream_moby_bx.bin"
OUT = ROOT / "Unreal/SSXTrickyRebuild/SimData/terrain_lod_reference.json"
TRACKS = ("snow", "gari", "pipe")


def pbd_path(stem):
    return next((ROOT / "unpacked/MODELS").rglob(f"{stem}.pbd"))


def f32(x):
    return float(np.float32(x))


def level_cases(rng, levels_out, tracks):
    for _ in range(400):
        corners = [[f32(rng.uniform(-2e4, 2e4)) for _ in range(3)] for _ in range(4)]
        r = rng.choice((rng.uniform(0, 4000), rng.uniform(2500, 13000), rng.uniform(9000, 30000)))
        d = np.array([rng.gauss(0, 1) for _ in range(3)])
        eye = [f32(x) for x in np.array(corners[0]) + r * d / np.linalg.norm(d)]
        levels_out.append({"corners": corners, "eye": eye, "levels": T.edge_levels(corners, eye)})
    rb = float(np.sqrt((3000.0 ** 2 + 15000.0 ** 2) / 2))
    for r, want in ((0.0, 8), (2999.95, 8), (3000.05, 6), (rb - 0.05, 6), (rb + 0.05, 4), (20000.0, 4)):
        corners = [[0.0, 0.0, 0.0]] * 4
        eye = [f32(r), 0.0, 0.0]
        lv = T.edge_levels(corners, eye)
        assert lv == [want] * 4, (r, lv)
        levels_out.append({"corners": corners, "eye": eye, "levels": lv})
    for stem in tracks:
        level = pbd.load(pbd_path(stem))
        drawn = [p for p in level.patches if p.texture >= 0]
        for _ in range(100):
            p = rng.choice(drawn)
            c = np.array(p.corners)
            eye = c.mean(axis=0) + np.array([rng.uniform(-15000, 15000), rng.uniform(-15000, 15000), rng.uniform(0, 3000)])
            corners = [[f32(x) for x in q] for q in p.corners]
            eye = [f32(x) for x in eye]
            levels_out.append({"corners": corners, "eye": eye, "levels": T.edge_levels(corners, eye)})


def ram_case():
    if not RAM.is_file():
        return None
    ram = RAM.read_bytes()
    u32 = lambda a: struct.unpack_from("<I", ram, a)[0]
    fv = lambda a, n: np.array(struct.unpack_from(f"<{n}f", ram, a))
    rend = u32(u32(0x337C58) + 0x724)
    vp = np.array([fv(rend + 0x510 + 16 * j, 4) for j in range(4)])
    eye = np.linalg.solve(vp[:3, [0, 1, 3]].T, -vp[3, [0, 1, 3]])
    bm = 0x7B4A00
    assert struct.unpack_from("<2f", ram, bm + 0x10) == (3000.0, 15000.0)
    lst, n = u32(bm + 0x544), u32(bm + 0x548)
    level = pbd.load(pbd_path("pipe"))
    by_corners = {tuple(np.float32(np.array(p.corners)).ravel().tolist()): k for k, p in enumerate(level.patches)}
    queued = []
    for i in range(n):
        e = lst + i * 0x18
        rec = u32(e + 0x10)
        key = tuple(np.float32(np.array([fv(rec + o, 3) for o in (0x168, 0x178, 0x188, 0x198)])).ravel().tolist())
        queued.append({"patch": by_corners[key], "levels": list(ram[e + 8:e + 12])})
    return {"track": "pipe", "eye": [float(x) for x in eye], "queued": queued}


def frac(x):
    return f"{x.numerator}/{x.denominator}"


def draw_cases(rng, tracks):
    out = []
    sigs = [s for s in T.signatures() if len(set(s)) > 1] + [[4] * 4, [6] * 4]
    for stem in tracks:
        level = pbd.load(pbd_path(stem))
        drawn = [k for k, p in enumerate(level.patches) if p.texture >= 0]
        for _ in range(300):
            k = rng.choice(drawn)
            p = level.patches[k]
            lv = rng.choice(sigs)
            tris = T.template(lv)
            pts = [np.array(p.evaluate(float(u), float(v))) for u, v in T.LATTICE]
            a, b, c = (pts[i] for i in rng.choice(tris))
            w = np.array([rng.random() for _ in range(3)])
            w /= w.sum()
            q = w[0] * a + w[1] * b + w[2] * c
            out.append({"track": stem, "patch": k, "levels": lv, "x": float(q[0]), "y": float(q[1]), "z": float(q[2])})
    return out


def main():
    rng = random.Random(20261014)
    levels = []
    level_cases(rng, levels, TRACKS)
    ref = {"levels": levels, "ram": ram_case(),
           "strips": [{"levels": s, "strips": [[[frac(u), frac(v)] for u, v in st] for st in T.patch_strips(s)]}
                      for s in T.signatures()],
           "draw": draw_cases(rng, TRACKS)}
    OUT.write_text(json.dumps(ref))
    r = ref["ram"]
    print(f"{OUT.name}: {len(levels)} level cases, RAM case {'%d patches' % len(r['queued']) if r else 'missing'}, "
          f"81 signatures, {len(ref['draw'])} draw points")
    if r:
        level = pbd.load(pbd_path("pipe"))
        bad = sum(T.edge_levels(level.patches[q["patch"]].corners, r["eye"]) != q["levels"] for q in r["queued"])
        print(f"RAM frame: rule vs queued levels, {bad} mismatches")


if __name__ == "__main__":
    main()
