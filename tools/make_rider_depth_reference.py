"""Rider depth pull reference.
Usage:  python tools/make_rider_depth_reference.py
Reads   SSX_TRACES/gs/paused_pipedream_moby_bx.gs.zst (PCSX2 GS dump, frame 0) and
        SSX_TRACES/ram/paused_pipedream_moby_bx.bin (EE RAM of the same paused frame).
Writes  Unreal/SSXTrickyRebuild/SimData/pipe.rider_depth.csv (git-ignored):
"""
import libpath  # noqa: F401,E402  (tools/lib on sys.path)
import traces  # noqa: E402

traces.require()
TR = traces.root()

import struct
import sys
from pathlib import Path

import numpy as np

ROOT = Path(__file__).resolve().parent.parent
GS = TR / "gs" / "paused_pipedream_moby_bx.gs.zst"
RAM = TR / "ram" / "paused_pipedream_moby_bx.bin"
OUT = ROOT / "Unreal" / "SSXTrickyRebuild" / "SimData" / "pipe.rider_depth.csv"
BOARDER = 0xAF75C0
RENDERER = 0x411780         # 0x337C58
PALETTE19 = 0x341FC0 + 19 * 64
BOARD_TBP, BOARD_CBP = 0x2CC0, 0x3ECC
NEAR = 15.0   # projection n = 15
FAR = 49548.51953   # Pipedream 1P draw distance


def main():
    sys.path.insert(0, str(ROOT / "tools"))
    import gsdump  # noqa: E402
    import mpf  # noqa: E402
    hdr, _, _, prims = gsdump.load(str(GS))
    frame = prims[:hdr["events"][0][1]]
    ram = RAM.read_bytes()
    fv = lambda a, n: np.array(struct.unpack_from("<%df" % n, ram, a))
    mat = lambda a: np.array([fv(a + 16 * j, 4) for j in range(4)])
    vp = mat(RENDERER + 0x510)
    sc = fv(RENDERER + 0x550, 3) * np.array([1.0, -1.0, 1.0])
    of = fv(RENDERER + 0x560, 3)
    p = fv(BOARDER + 0x140, 3)
    wp = float(np.r_[p, 1.0] @ vp[:, 3])

    is_board = lambda q: (q["TEX0"] & 0x3FFF == BOARD_TBP and (q["TEX0"] >> 37) & 0x3FFF == BOARD_CBP
                          and q["TEST"] == 0x500AD and not q["prim"] & 0x200)
    first = next(q["idx"] for q in frame if q["idx"] > 50000 and is_board(q))
    board = []
    for q in frame[first:]:
        if q["prim"] & 0x200:
            continue
        if not is_board(q):
            break
        board.append(q)
    models = {m.name: m for m in mpf.load(str(ROOT / "unpacked" / "CHAR" / "BRDPS2" / "data" / "char" / "board.mpf"))}
    mv = np.concatenate([c.positions for g in models["BxGoofy"].groups for b in g.blocks for c in b.chunks]).astype(float)
    world = (np.c_[mv, np.ones(len(mv))] @ mat(PALETTE19))[:, :3]
    clip = np.c_[world, np.ones(len(world))] @ vp
    scr = clip[:, :3] / clip[:, 3:4] * sc + of
    gv = np.unique(np.array([(v[0] / 16, v[1] / 16, v[2]) for q in board for v in q["verts"]]), axis=0)
    near_v = np.linalg.norm(gv[:, None, :2] - scr[None, :, :2], axis=2).argmin(1)
    offs = gv[:, 2] - scr[near_v, 2]

    def tris(qs):
        return [[(v[0] / 16, v[1] / 16, float(v[2])) for v in q["verts"]] for q in qs]
    bt = tris(board)
    allv = np.array([v for t in bt for v in t])
    x0, x1 = int(allv[:, 0].min()), int(allv[:, 0].max()) + 1
    y0, y1 = int(allv[:, 1].min()), int(allv[:, 1].max()) + 1
    xs, ys = np.meshgrid(np.arange(x0, x1 + 1) + 0.0, np.arange(y0, y1 + 1) + 0.0)

    def cover(t):
        (ax, ay, az), (bx, by, bz), (cx, cy, cz) = t
        d = (by - cy) * (ax - cx) + (cx - bx) * (ay - cy)
        if d == 0:
            return None, None
        a = ((by - cy) * (xs - cx) + (cx - bx) * (ys - cy)) / d
        b = ((cy - ay) * (xs - cx) + (ax - cx) * (ys - cy)) / d
        c = 1 - a - b
        return (a >= 0) & (b >= 0) & (c >= 0), a * az + b * bz + c * cz
    zb = np.full(xs.shape, -1.0)
    for t in bt:
        m, z = cover(t)
        if m is not None:
            zb = np.where(m & (z > zb), z, zb)
    zs = np.full(xs.shape, -1.0)
    for q in frame:
        if q["idx"] >= first:
            break
        if (q["ZBUF"] >> 32) & 1 or q["type"] not in (3, 4, 5):
            continue
        m, z = cover(tris([q])[0])
        if m is None:
            continue
        zs = np.where(m & (zb >= 0) & (z >= zs), z, zs)
    sel = (zb >= 0) & (zs >= 0)
    OUT.parent.mkdir(parents=True, exist_ok=True)
    lines = ["wp,near,far,offset,offset_min,offset_max,vertices",
             f"{wp:.4f},{NEAR},{FAR},{np.median(offs):.1f},{offs.min():.1f},{offs.max():.1f},{len(gv)}",
             "x,y,zb,zs"]
    lines += [f"{x:.0f},{y:.0f},{b:.3f},{s:.3f}" for x, y, b, s in zip(xs[sel], ys[sel], zb[sel], zs[sel])]
    OUT.write_text("\n".join(lines) + "\n")
    off = np.median(offs)
    print(f"wrote {OUT}: {sel.sum()} board pixels, w_p {wp:.2f} cm, rider Z offset {off:.1f} ({offs.min():.1f}..{offs.max():.1f});"
          f" visible {(zb[sel] >= zs[sel]).sum()}, without the offset {(zb[sel] - off >= zs[sel]).sum()}")


if __name__ == "__main__":
    main()
