"""Generate ground-probe reference cases for the Unreal automation test (SSX.Sim.PatchProbe).
Usage: python tools/make_probe_cases.py [track] [count] [seed]      (default: alaska 300 1)
"""
import libpath  # noqa: F401,E402  (tools/lib on sys.path)
import csv
import sys
from pathlib import Path

import numpy as np

import pbd

ROOT = Path(__file__).resolve().parent.parent
import collision as col  # noqa: E402  reference model (tools/lib/models)

OUT_DIR = ROOT / "Unreal" / "SSXTrickyRebuild" / "SimData"
FIELDS = (["case", "px", "py", "pz", "nx", "ny", "nz", "fx", "fy", "fz", "lx", "ly", "lz", "lean", "scale",
           "found", "surface", "kind", "cx", "cy", "cz", "gnx", "gny", "gnz", "height",
           "cfx", "cfy", "cfz", "clx", "cly", "clz", "flx", "fly", "flz"])


class GridWorld(col.World):
    """collision.World with an XY bucket grid in front of the AABB loop (same results, faster)."""

    CELL = 2500.0

    def __init__(self, patches):
        super().__init__(patches)
        self.bounds = [p.aabb() for p in self.patches]
        self.buckets = {}
        for k, (lo, hi) in enumerate(self.bounds):
            for i in range(int(np.floor(lo[0] / self.CELL)), int(np.floor(hi[0] / self.CELL)) + 1):
                for j in range(int(np.floor(lo[1] / self.CELL)), int(np.floor(hi[1] / self.CELL)) + 1):
                    self.buckets.setdefault((i, j), []).append(k)

    def _candidates(self, lo, hi):
        ks = set()
        for i in range(int(np.floor(lo[0] / self.CELL)), int(np.floor(hi[0] / self.CELL)) + 1):
            for j in range(int(np.floor(lo[1] / self.CELL)), int(np.floor(hi[1] / self.CELL)) + 1):
                ks.update(self.buckets.get((i, j), ()))
        for k in sorted(ks):
            patch = self.patches[k]
            if patch.surface in col.IGNORED_SURFACES:
                continue
            pmin, pmax = self.bounds[k]
            if np.all(pmin <= hi) and np.all(lo <= pmax):
                yield patch


def unit(v):
    return v / np.linalg.norm(v)


def main():
    track = sys.argv[1] if len(sys.argv) > 1 else "alaska"
    count = int(sys.argv[2]) if len(sys.argv) > 2 else 300
    rng = np.random.default_rng(int(sys.argv[3]) if len(sys.argv) > 3 else 1)
    src = next((ROOT / "unpacked" / "MODELS").rglob(f"{track}.pbd"))
    level = pbd.load(src)
    patches = [col.Patch(np.array(p.coeffs, dtype=np.float64), int(p.surface_type)) for p in level.patches]
    world = GridWorld(patches)
    rideable = [k for k, p in enumerate(patches) if p.surface in (0, 1, 2, 3, 4, 5, 18)]
    rows = []
    for case in range(count):
        k = int(rng.choice(rideable))
        u, v = rng.uniform(0.02, 0.98, 2)
        sp, du, dv = patches[k].eval(u, v)
        n_true = unit(np.cross(du, dv))
        lift = 5000.0 if case % 25 == 24 else rng.uniform(-40.0, 80.0)
        pos = sp + lift * n_true
        n_prev = unit(n_true + rng.normal(0.0, 0.05, 3)) if case % 3 else np.array([0.0, 0.0, 1.0])
        yaw = rng.uniform(-np.pi, np.pi)
        fwd = np.array([np.cos(yaw), np.sin(yaw), rng.uniform(-0.3, 0.3)])
        fwd = unit(fwd - np.dot(fwd, n_prev) * n_prev)
        left = np.cross(n_prev, fwd)
        lean = rng.uniform(-45.0, 45.0) if case % 2 else 0.0
        scale = 1.0
        res = col.ground_probe(world, col.ProbeInput(pos, n_prev, fwd, left, lean, scale))
        if res.found:
            c, h, s = res.contact_point, res.height, res.surface
        else:
            c, h, s = np.zeros(3), 0.0, -1
        rows.append([case, *pos, *n_prev, *fwd, *left, lean, scale, int(res.found), s, res.contact_kind,
                     *c, *res.ground_normal, h, *res.contact_forward, *res.contact_side, *res.fall_line])
    OUT_DIR.mkdir(parents=True, exist_ok=True)
    out = OUT_DIR / f"{track}.probe_cases.csv"
    with open(out, "w", newline="", encoding="utf-8") as fh:
        w = csv.writer(fh)
        w.writerow(FIELDS)
        for r in rows:
            w.writerow([r[0]] + [repr(float(x)) for x in r[1:15]] + [r[15], r[16], r[17]]
                       + [repr(float(x)) for x in r[18:]])
    found = sum(r[15] for r in rows)
    print(f"{out}: {len(rows)} cases, {found} hits, {len(rows) - found} misses")


if __name__ == "__main__":
    main()
