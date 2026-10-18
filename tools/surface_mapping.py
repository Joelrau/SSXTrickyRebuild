"""Settle how patch surface types map to the game's surface-table index, using live traces.
Usage:  python tools/surface_mapping.py [session ...]
Reads   SSX_TRACES/live/<session>/summary.csv and Unreal/SSXTrickyRebuild/SimData/*.patches.bin
Prints  per-track match rate, then the (patch type -> game surface id) confusion table.
"""
import libpath  # noqa: F401,E402  (tools/lib on sys.path)
import traces  # noqa: E402

traces.require()
TR = traces.root()

import collections
import csv
import struct
import sys
from pathlib import Path

import numpy as np

ROOT = Path(__file__).resolve().parent.parent
SIMDATA = ROOT / "Unreal" / "SSXTrickyRebuild" / "SimData"
TRACES = TR / "live"


def load_patches(path: Path):
    raw = path.read_bytes()
    count = struct.unpack_from("<I", raw, 8)[0]
    coeffs = np.zeros((count, 16, 3))
    bmin = np.zeros((count, 3))
    bmax = np.zeros((count, 3))
    types = np.zeros(count, dtype=int)
    for k in range(count):
        off = 96 + k * 232
        coeffs[k] = np.array(struct.unpack_from("<48f", raw, off)).reshape(16, 3)
        b = struct.unpack_from("<6f", raw, off + 192)
        bmin[k], bmax[k] = b[:3], b[3:]
        types[k] = struct.unpack_from("<i", raw, off + 216)[0]
    return coeffs, bmin, bmax, types


def evaluate(C, u, v):
    """P, dP/du, dP/dv for P(u, v) = sum C[4i+j] u^i v^j."""
    ui = np.array([1, u, u * u, u ** 3])
    vj = np.array([1, v, v * v, v ** 3])
    dui = np.array([0, 1, 2 * u, 3 * u * u])
    dvj = np.array([0, 1, 2 * v, 3 * v * v])
    W = np.outer(ui, vj).ravel()
    Wu = np.outer(dui, vj).ravel()
    Wv = np.outer(ui, dvj).ravel()
    return W @ C, Wu @ C, Wv @ C


def vertical_hit(C, x, y):
    """Solve P(u,v).xy = (x,y) by Newton; returns (z, u, v) or None."""
    best = None
    for u0 in (0.25, 0.75):
        for v0 in (0.25, 0.75):
            u, v = u0, v0
            for _ in range(12):
                P, Pu, Pv = evaluate(C, u, v)
                J = np.array([[Pu[0], Pv[0]], [Pu[1], Pv[1]]])
                r = np.array([P[0] - x, P[1] - y])
                try:
                    du, dv = np.linalg.solve(J, -r)
                except np.linalg.LinAlgError:
                    break
                u, v = u + du, v + dv
                if abs(u) > 3 or abs(v) > 3:
                    break
            if -1e-3 <= u <= 1.001 and -1e-3 <= v <= 1.001:
                P, _, _ = evaluate(C, u, v)
                if abs(P[0] - x) < 0.5 and abs(P[1] - y) < 0.5:
                    return P[2], u, v
    return best


def under(track, x, y, z):
    coeffs, bmin, bmax, types = track
    m = (bmin[:, 0] <= x) & (x <= bmax[:, 0]) & (bmin[:, 1] <= y) & (y <= bmax[:, 1]) \
        & (bmin[:, 2] - 150 <= z) & (z <= bmax[:, 2] + 150)
    best = None
    for k in np.nonzero(m)[0]:
        h = vertical_hit(coeffs[k], x, y)
        if h is None:
            continue
        dz = z - h[0]
        if -100 <= dz <= 100 and (best is None or abs(dz) < abs(best[1])):
            best = (k, dz)
    return best


def main():
    sessions = sys.argv[1:] or ["ride_session_1", "ride_session_2"]
    rows = []
    for s in sessions:
        with open(TRACES / s / "summary.csv", encoding="utf-8") as fh:
            for r in csv.DictReader(fh):
                if r["motion"] == "2":
                    rows.append((s, r["rider"], float(r["pos_x"]), float(r["pos_y"]), float(r["pos_z"]),
                                 int(r["surface"])))
    rng = np.random.default_rng(1)
    probe = [rows[i] for i in rng.choice(len(rows), size=min(300, len(rows)), replace=False)]
    tracks = {p.name.split(".")[0]: p for p in sorted(SIMDATA.glob("*.patches.bin"))}
    scores = {}
    for name, path in tracks.items():
        t = load_patches(path)
        hits = sum(1 for (_, _, x, y, z, _) in probe if under(t, x, y, z) is not None)
        scores[name] = hits
        print(f"{name:10s} {hits}/{len(probe)} grounded samples have a patch within 1 m below/above")
    best = max(scores, key=scores.get)
    print(f"track: {best}")
    t = load_patches(tracks[best])
    table = collections.Counter()
    player = collections.Counter()
    miss = 0
    for (s, rider, x, y, z, sid) in rows[::3]:
        h = under(t, x, y, z)
        if h is None:
            miss += 1
            continue
        table[(int(t[3][h[0]]), sid)] += 1
        if rider == "0xdd8cc0":
            player[(int(t[3][h[0]]), sid)] += 1
    print(f"samples {len(rows[::3])}, no patch found {miss}")
    print("all riders: (patch type, game surface id) -> frames")
    for k, n in sorted(table.items()):
        print(f"  patch {k[0]:2d} game {k[1]:2d}: {n}")
    print("player 0xdd8cc0 only:")
    for k, n in sorted(player.items()):
        print(f"  patch {k[0]:2d} game {k[1]:2d}: {n}")


if __name__ == "__main__":
    main()
