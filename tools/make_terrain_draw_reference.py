"""Drawn terrain height of the original.
Usage:  python tools/make_terrain_draw_reference.py
Reads   unpacked/MODELS/<track>/data/models/<track>.pbd and (Snowdream) SSX_TRACES/live/replay_rep0001
Writes  Unreal/SSXTrickyRebuild/SimData/terrain_draw_reference.csv (git-ignored):
        track,src,patch,u,v,x,y,z_analytic,z_draw,z_k8
"""
# 0x312AF0 / 0x30F960
import libpath  # noqa: F401,E402  (tools/lib on sys.path)
import traces  # noqa: E402

traces.require()
TR = traces.root()

import ast
import json
import random
import struct
import sys
from pathlib import Path

import numpy as np

ROOT = Path(__file__).resolve().parent.parent
sys.path.insert(0, str(ROOT / "tools"))
import pbd  # noqa: E402

TRACKS = {"snow": "SNOW", "gari": "GARI"}
SAMPLES = 1500
LIVE = TR / "live" / "replay_rep0001"
BLOCKS = [(0x0000, 0x600), (0x4600, 0x500), (0x57B0, 0x50)]
REC_RIDER = sum(b[1] for b in BLOCKS)


def off(o):
    base = 0
    for start, size in BLOCKS:
        if start <= o < start + size:
            return base + o - start
        base += size
    raise KeyError(hex(o))


def ev(c, u, v, du=0, dv=0):
    pu = np.array([1, u, u * u, u ** 3]) if du == 0 else np.array([0, 1, 2 * u, 3 * u * u])
    pv = np.array([1, v, v * v, v ** 3]) if dv == 0 else np.array([0, 1, 2 * v, 3 * v * v])
    return np.einsum("i,ijk,j->k", pu, c, pv)


def grid_tris(c, n):
    """Triangles of the n x n quad grid at k/n, split along (u1, v0)-(u0, v1) (the original's strips)."""
    t = np.linspace(0.0, 1.0, n + 1)
    g = np.array([[ev(c, a, b) for b in t] for a in t])
    out = []
    for i in range(n):
        for j in range(n):
            out.append((g[i, j], g[i + 1, j], g[i, j + 1]))
            out.append((g[i + 1, j], g[i + 1, j + 1], g[i, j + 1]))
    return out


def height_at(tris, x, y, ref):
    best = None
    for a, b, c in tris:
        d = (b[1] - c[1]) * (a[0] - c[0]) + (c[0] - b[0]) * (a[1] - c[1])
        if abs(d) < 1e-9:
            continue
        l0 = ((b[1] - c[1]) * (x - c[0]) + (c[0] - b[0]) * (y - c[1])) / d
        l1 = ((c[1] - a[1]) * (x - c[0]) + (a[0] - c[0]) * (y - c[1])) / d
        l2 = 1 - l0 - l1
        if min(l0, l1, l2) < -1e-9:
            continue
        z = l0 * a[2] + l1 * b[2] + l2 * c[2]
        if best is None or abs(z - ref) < abs(best - ref):
            best = z
    return best


def project(c, q, n):
    """(u, v, t) with P(u, v) + t n = q, or None."""
    u = v = 0.5
    t = 0.0
    for _ in range(30):
        f = ev(c, u, v) + t * n - q
        j = np.stack([ev(c, u, v, 1, 0), ev(c, u, v, 0, 1), n], 1)
        try:
            d = np.linalg.solve(j, -f)
        except np.linalg.LinAlgError:
            return None
        u, v, t = u + d[0], v + d[1], t + d[2]
        if abs(u) > 3 or abs(v) > 3:
            return None
    return (u, v, t) if -1e-4 <= u <= 1 + 1e-4 and -1e-4 <= v <= 1 + 1e-4 else None


def drawn_along(c, u, v, q, n, k):
    """q's height above the k-grid triangle that contains (u, v), along n."""
    i, j = min(int(u * k), k - 1), min(int(v * k), k - 1)
    a, b = u * k - i, v * k - j
    g = lambda ii, jj: ev(c, ii / k, jj / k)
    tri = (g(i, j), g(i + 1, j), g(i, j + 1)) if a + b <= 1 else (g(i + 1, j), g(i + 1, j + 1), g(i, j + 1))
    nn = np.cross(tri[1] - tri[0], tri[2] - tri[0])
    return ((q - tri[0]) @ nn) / (n @ nn)


def board_points(patches, coeffs):
    """Racer 4's lowest drawn board vertex per REP0001 ground frame: (frame, q, n)."""
    if not (LIVE / "frames.bin").exists():
        print(f"missing {LIVE / 'frames.bin'}: no board samples")
        return []
    import mpf  # noqa: E402
    models = {m.name: m for m in mpf.load(str(ROOT / "unpacked" / "CHAR" / "BRDPS2" / "data" / "char" / "board.mpf"))}
    meta = json.loads((LIVE / "meta.json").read_text())
    profiles = meta["profiles"] if isinstance(meta["profiles"], dict) else ast.literal_eval(meta["profiles"])
    racer = 4
    cls = bytes.fromhex(profiles[meta["riders"][racer]])[0x44]
    pos = np.concatenate([ch.positions for g in models[("Bx", "Fr", "Al")[cls]].groups for blk in g.blocks
                          for ch in blk.chunks]).astype(float)
    verts = np.stack([pos[:, 0], pos[:, 2], -pos[:, 1]], 1)
    raw = (LIVE / "frames.bin").read_bytes()
    n_riders = struct.unpack_from("<I", raw, 4)[0]
    rec = 8 + n_riders * REC_RIDER
    out = []
    for i in range(len(raw) // rec):
        frame = struct.unpack_from("<i", raw, i * rec)[0] - 127
        b = i * rec + 8 + racer * REC_RIDER
        rd = lambda o, t="<f": struct.unpack_from(t, raw, b + off(o))[0]
        if rd(0x424, "<i") != 2 or rd(0x2E0, "<i") != 0:
            continue
        p = np.array(struct.unpack_from("<3f", raw, b + off(0x140)))
        n = np.array(struct.unpack_from("<3f", raw, b + off(0x2A0)))
        m = np.array([struct.unpack_from("<4f", raw, b + off(0x4A60 + 16 * j)) for j in range(4)])
        if abs(np.linalg.norm(n) - 1) > 0.01 or abs(np.linalg.norm(m[1, :3]) - 1) > 0.01:
            continue
        w = verts @ m[:3, :3] + m[3, :3]
        q = w[np.argmin((w - p) @ n)]
        out.append((frame, q, n))
    return out


def find_patch(q, n, coeffs, bmin, bmax):
    best = None
    for k in np.nonzero(np.all((bmin - 60 <= q) & (q <= bmax + 60), 1))[0]:
        r = project(coeffs[k], q, n)
        if r and (best is None or abs(r[2]) < abs(best[1][2])):
            best = (k, r)
    return best


def main():
    rnd = random.Random(20261011)
    lines = ["track,src,patch,u,v,x,y,z_analytic,z_draw,z_k8"]
    for stem, folder in TRACKS.items():
        level = pbd.load(ROOT / "unpacked" / "MODELS" / folder / "data" / "models" / f"{stem}.pbd")
        ids = [k for k, p in enumerate(level.patches) if p.texture >= 0 and p.flag_word >= 0]
        coeffs = np.array([level.patches[k].coeffs for k in ids], dtype=np.float64).reshape(-1, 4, 4, 3)
        rows = []
        for _ in range(SAMPLES):
            j = rnd.randrange(len(ids))
            rows.append(("rand", j, rnd.random(), rnd.random()))
        if stem == "snow":
            bmin = np.array([level.patches[k].bbox_min for k in ids])
            bmax = np.array([level.patches[k].bbox_max for k in ids])
            carve = []
            for frame, q, n in board_points(level.patches, coeffs)[::4]:
                hit = find_patch(q, n, coeffs, bmin, bmax)
                if not hit:
                    continue
                j, (u, v, t) = hit
                rows.append((f"rep{frame}", j, u, v))
                if 1754 <= frame <= 1877:
                    carve.append((t, drawn_along(coeffs[j], u, v, q, n, 7), drawn_along(coeffs[j], u, v, q, n, 8)))
            if carve:
                a = np.array(carve)
                print(f"REP0001 carve 1754-1877 ({len(a)} samples): lowest board vertex above the analytic snow p50 "
                      f"{np.median(a[:, 0]):.2f} cm, above the drawn k/7 snow {np.median(a[:, 1]):.2f}, k/8 {np.median(a[:, 2]):.2f}")
        cache = {}
        dz7, dz8 = [], []
        for src, j, u, v in rows:
            c = coeffs[j]
            if j not in cache:
                cache[j] = (grid_tris(c, 7), grid_tris(c, 8))
            p = ev(c, u, v)
            nrm = np.cross(ev(c, u, v, 1, 0), ev(c, u, v, 0, 1))
            if abs(nrm[2]) < 0.3 * np.linalg.norm(nrm):
                continue
            z7 = height_at(cache[j][0], p[0], p[1], p[2])
            z8 = height_at(cache[j][1], p[0], p[1], p[2])
            if z7 is None or z8 is None or abs(z7 - p[2]) > 50:
                continue
            dz7.append(z7 - p[2])
            dz8.append(z8 - z7)
            lines.append(f"{stem},{src},{ids[j]},{u:.6f},{v:.6f},{p[0]:.4f},{p[1]:.4f},{p[2]:.4f},{z7:.4f},{z8:.4f}")
        dz7, dz8 = np.array(dz7), np.array(dz8)
        print(f"{stem}: {len(dz7)} points; drawn k/7 - analytic p5 {np.percentile(dz7, 5):.2f} p50 {np.median(dz7):.2f} "
              f"p95 {np.percentile(dz7, 95):.2f} cm; k/8 - k/7 p5 {np.percentile(dz8, 5):.2f} p50 {np.median(dz8):.2f} "
              f"p95 {np.percentile(dz8, 95):.2f} max |.| {np.abs(dz8).max():.2f}")
    out = ROOT / "Unreal" / "SSXTrickyRebuild" / "SimData" / "terrain_draw_reference.csv"
    out.write_text("\n".join(lines) + "\n")
    print(f"wrote {out} ({len(lines) - 1} rows)")


if __name__ == "__main__":
    main()
