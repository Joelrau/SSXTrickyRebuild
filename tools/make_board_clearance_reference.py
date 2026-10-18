"""Drawn-board clearance in the original.
Usage:  python tools/make_board_clearance_reference.py
Reads   SSX_TRACES/live/ride_session_1|2/frames.bin + meta.json (Garibaldi race, six riders) and
        unpacked/CHAR/BRDPS2/data/char/board.mpf (models Bx / Fr / Al).
Prints  per board class and |board roll| bin the median of
"""
# FUN_00207C30 / 0x1DF478
import libpath  # noqa: F401,E402  (tools/lib on sys.path)
import traces  # noqa: E402

traces.require()
TR = traces.root()

import ast
import json
import math
import struct
import sys
from pathlib import Path

import numpy as np

ROOT = Path(__file__).resolve().parent.parent
LIVE = TR / "live"
BOARD_MPF = ROOT / "unpacked" / "CHAR" / "BRDPS2" / "data" / "char" / "board.mpf"
BLOCKS = [(0x0000, 0x600), (0x4600, 0x500), (0x57B0, 0x50)]
REC_RIDER = sum(b[1] for b in BLOCKS)
BINS = [(0, 5), (5, 15), (15, 30), (30, 50)]
FINE = list(range(0, 51))   # 1-degree bins of rel for the test's table


def off(o):
    base = 0
    for start, size in BLOCKS:
        if start <= o < start + size:
            return base + o - start
        base += size
    raise KeyError(hex(o))


def main():
    sys.path.insert(0, str(ROOT / "tools"))
    import mpf  # noqa: E402
    models = {m.name: m for m in mpf.load(str(BOARD_MPF))}
    verts = {}
    for c, name in enumerate(("Bx", "Fr", "Al")):
        pos = np.concatenate([ch.positions for g in models[name].groups for blk in g.blocks for ch in blk.chunks]).astype(float)
        verts[c] = np.stack([pos[:, 0], pos[:, 2], -pos[:, 1]], 1)
    rows = {c: [] for c in range(3)}   # (|roll|, low, orig)
    for sess in (1, 2):
        d = LIVE / f"ride_session_{sess}"
        if not (d / "frames.bin").exists():
            print(f"missing {d / 'frames.bin'}")
            continue
        meta = json.loads((d / "meta.json").read_text())
        profiles = meta["profiles"] if isinstance(meta["profiles"], dict) else ast.literal_eval(meta["profiles"])
        classes = [bytes.fromhex(profiles[r])[0x44] for r in meta["riders"]]
        scales = [struct.unpack_from("<f", bytes.fromhex(profiles[r]), 0x60)[0] for r in meta["riders"]]
        raw = (d / "frames.bin").read_bytes()
        n_riders = struct.unpack_from("<I", raw, 4)[0]
        rec = 8 + n_riders * REC_RIDER
        for i in range(len(raw) // rec):
            for r in range(n_riders):
                b = i * rec + 8 + r * REC_RIDER
                rd = lambda o, t="<f": struct.unpack_from(t, raw, b + off(o))[0]
                if rd(0x424, "<i") != 2 or rd(0x290, "<i") != 1 or rd(0x2E0, "<i") != 0:
                    continue
                p = np.array(struct.unpack_from("<3f", raw, b + off(0x140)))
                n = np.array(struct.unpack_from("<3f", raw, b + off(0x2A0)))
                B = np.array([struct.unpack_from("<4f", raw, b + off(0x4A60 + 16 * j)) for j in range(4)])
                if abs(np.linalg.norm(n) - 1) > 0.01 or abs(np.linalg.norm(B[1, :3]) - 1) > 0.01:
                    continue
                hit = p - rd(0x1BC) * n
                c = classes[r]
                low = ((verts[c] @ (B[:3, :3] * scales[r]) + B[3, :3] - hit) @ n).min()
                orig = (B[3, :3] - hit) @ n
                roll = abs(math.degrees(math.atan2(n @ B[2, :3], n @ B[1, :3])))
                pitch = abs(math.degrees(math.asin(max(-1.0, min(1.0, n @ B[0, :3])))))
                rows[c].append((roll, low, orig, pitch, (low - orig) / scales[r]))
    for c in range(3):
        a = np.array(rows[c])
        if not len(a):
            continue
        line = f"class {c} ({len(a)} frames, |roll| p50 {np.median(a[:, 0]):.1f} p90 {np.percentile(a[:, 0], 90):.1f}):"
        for lo, hi in BINS:
            m = (a[:, 0] >= lo) & (a[:, 0] < hi)
            line += (f"\n  |roll| {lo}-{hi} n {m.sum()}: low {np.median(a[m, 1]):.2f} orig {np.median(a[m, 2]):.2f}"
                     f" rel {np.median(a[m, 1] - a[m, 2]):.2f}")
        print(line)
    print("rel by 1-degree |roll| bin, frames with |board pitch| (asin(n . row 0)) < 0.3 deg (SSX.Render.BoardClearance OriginalRel table), class rows, bins 0-1 .. 49-50; rel / S, the rider size scale (profile +0x60) divided out, the board part scales about its origin so rel is linear in S:")
    for c in range(3):
        a = np.array(rows[c])
        vals = []
        for lo in FINE[:-1]:
            m = (a[:, 0] >= lo) & (a[:, 0] < lo + 1) & (a[:, 3] < 0.3)
            vals.append(f"{np.median(a[m, 4]):.2f}" if m.sum() >= 20 else "NAN")
        print("  { " + ", ".join(vals) + " },")
    replay_board(verts)


def replay_board(verts):
    d = LIVE / "replay_rep0001"
    if not (d / "frames.bin").exists():
        print(f"missing {d / 'frames.bin'}")
        return
    meta = json.loads((d / "meta.json").read_text())
    profiles = meta["profiles"] if isinstance(meta["profiles"], dict) else ast.literal_eval(meta["profiles"])
    racer = 4
    c = bytes.fromhex(profiles[meta["riders"][racer]])[0x44]
    scale = struct.unpack_from("<f", bytes.fromhex(profiles[meta["riders"][racer]]), 0x60)[0]
    raw = (d / "frames.bin").read_bytes()
    n_riders = struct.unpack_from("<I", raw, 4)[0]
    rec = 8 + n_riders * REC_RIDER
    out = ROOT / "Unreal" / "SSXTrickyRebuild" / "SimData" / "replay_rep0001.board.csv"
    lines = ["frame,surface,px,py,pz,roll,low,orig"]
    for i in range(len(raw) // rec):
        counter = struct.unpack_from("<i", raw, i * rec)[0]
        b = i * rec + 8 + racer * REC_RIDER
        rd = lambda o, t="<f": struct.unpack_from(t, raw, b + off(o))[0]
        if rd(0x424, "<i") != 2 or rd(0x2E0, "<i") != 0:
            continue
        p = np.array(struct.unpack_from("<3f", raw, b + off(0x140)))
        n = np.array(struct.unpack_from("<3f", raw, b + off(0x2A0)))
        B = np.array([struct.unpack_from("<4f", raw, b + off(0x4A60 + 16 * j)) for j in range(4)])
        if abs(np.linalg.norm(n) - 1) > 0.01 or abs(np.linalg.norm(B[1, :3]) - 1) > 0.01:
            continue
        hit = p - rd(0x1BC) * n
        low = ((verts[c] @ (B[:3, :3] * scale) + B[3, :3] - hit) @ n).min()
        orig = (B[3, :3] - hit) @ n
        roll = abs(math.degrees(math.atan2(n @ B[2, :3], n @ B[1, :3])))
        lines.append(f"{counter - 127},{rd(0x290, '<i')},{p[0]:.4f},{p[1]:.4f},{p[2]:.4f},{roll:.3f},{low:.3f},{orig:.3f}")
    out.parent.mkdir(parents=True, exist_ok=True)
    out.write_text("\n".join(lines) + "\n")
    print(f"wrote {out} ({len(lines) - 1} samples, class {c})")


if __name__ == "__main__":
    main()
