"""Live ground height samples for SSX.World.BoardOnSnow.
Usage:  python tools/make_ground_height_reference.py
Reads   SSX_TRACES/live/ride_session_1|2/frames.bin + meta.json (Garibaldi race, six riders)
Writes  Unreal/SSXTrickyRebuild/SimData/gari.ground_h_live.csv (git-ignored derived data): one row per rider frame on the
        ground (motion +0x424 = 2) on ordinary snow (surface +0x290 = 1) on the static world (contact kind +0x2E0 = 0):
"""
# 0x129628 / 0x12AE18
import libpath  # noqa: F401,E402  (tools/lib on sys.path)
import traces  # noqa: E402

traces.require()
TR = traces.root()

import ast
import csv
import json
import math
import struct
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
LIVE = TR / "live"
OUT = ROOT / "Unreal" / "SSXTrickyRebuild" / "SimData" / "gari.ground_h_live.csv"
BLOCKS = [(0x0000, 0x600), (0x4600, 0x500), (0x57B0, 0x50)]
REC_RIDER = sum(b[1] for b in BLOCKS)


def off(o):
    base = 0
    for start, size in BLOCKS:
        if start <= o < start + size:
            return base + o - start
        base += size
    raise KeyError(hex(o))


def quat_rows(w, x, y, z):
    """Root rotation rows of 0x129628 step 1 (row vectors: side, forward, up)."""
    return [[1 - 2 * y * y - 2 * z * z, 2 * x * y + 2 * w * z, 2 * x * z - 2 * w * y],
            [2 * x * y - 2 * w * z, 1 - 2 * x * x - 2 * z * z, 2 * y * z + 2 * w * x],
            [2 * x * z + 2 * w * y, 2 * y * z - 2 * w * x, 1 - 2 * x * x - 2 * y * y]]


def orient(Q, R, B, n, steer, cls):
    """Board orientation terms (deg). Q body rows (quat), R root rows (+0x4AA0: lean x Q), B board rows (+0x4A60:
    long, up, across), n unit smoothed up +0x2C0. Returns stance, align, lean, lean residual, body roll, body pitch,
    root roll, board yaw, board roll, animation roll, stale."""
    dt = lambda a, b: sum(a[k] * b[k] for k in range(3))
    cr = lambda a, b: [a[1] * b[2] - a[2] * b[1], a[2] * b[0] - a[0] * b[2], a[0] * b[1] - a[1] * b[0]]
    deg = math.degrees
    s = 1.0 if dt(B[0], R[1]) >= 0 else -1.0
    s2 = 1.0 if dt(B[2], R[0]) >= 0 else -1.0
    align = deg(math.asin(max(-1.0, min(1.0, dt(n, B[0])))))
    lean = deg(math.atan2(dt(R[0], Q[2]), dt(R[0], Q[0])))
    lean_res = lean - deg(steer * (1.134464 if cls == 2 else 0.87266463))
    body_roll = deg(math.atan2(dt(n, Q[0]), dt(n, Q[2])))
    body_pitch = deg(math.atan2(dt(n, Q[1]), dt(n, Q[2])))
    root_roll = deg(math.atan2(dt(n, R[0]), dt(n, R[2])))
    f = [R[1][k] - dt(R[1], n) * n[k] for k in range(3)]
    fl = math.sqrt(dt(f, f)) or 1.0
    f = [x / fl for x in f]
    b0 = [s * x for x in B[0]]
    yaw = deg(math.atan2(dt(n, cr(f, b0)), dt(f, b0)))
    roll = deg(math.atan2(dt(n, B[2]), dt(n, B[1])))
    anim_roll = s2 * (roll - deg(math.atan2(s2 * dt(n, R[0]), dt(n, R[2]))))
    stale = 1 if dt(R[1], Q[1]) < 0.99999 else 0
    return [s, align, lean, lean_res, body_roll, body_pitch, root_roll, yaw, s * roll, anim_roll, stale]


def tilt(Q, n, g):
    """Slope of n, body tilt (body up row vs n) and body up vs the tilt target normalize(g + (0, 0, 0.5 g.z)) (deg)."""
    ang = lambda a, b: math.degrees(math.acos(max(-1.0, min(1.0, sum(a[k] * b[k] for k in range(3)) / math.sqrt(sum(x * x for x in a) * sum(x * x for x in b))))))
    return [ang(n, (0, 0, 1)), ang(Q[2], n), ang(Q[2], (g[0], g[1], g[2] * 1.5))]


def main():
    rows = []
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
            frame = struct.unpack_from("<I", raw, i * rec)[0]
            for r in range(n_riders):
                b = i * rec + 8 + r * REC_RIDER
                rd = lambda o, t="<f": struct.unpack_from(t, raw, b + off(o))[0]
                if rd(0x424, "<i") != 2 or rd(0x290, "<i") != 1 or rd(0x2E0, "<i") != 0:
                    continue
                p = struct.unpack_from("<3f", raw, b + off(0x140))
                o = struct.unpack_from("<3f", raw, b + off(0x4A90))
                u = struct.unpack_from("<3f", raw, b + off(0x2C0))
                un = math.sqrt(sum(x * x for x in u))
                if un < 1e-6:
                    continue
                dot = lambda a: sum(a[k] * u[k] for k in range(3)) / un
                pose = dot([o[k] - p[k] for k in range(3)])
                t308 = rd(0x308) * dot(u)
                tlean = scales[r] * rd(0x244) * dot(struct.unpack_from("<3f", raw, b + off(0x330)))
                chat = rd(0x1CC)
                tch = 0.0
                if chat > 0:
                    n = struct.unpack_from("<3f", raw, b + off(0x2A0))
                    rows3 = [struct.unpack_from("<3f", raw, b + off(0x4A60 + 16 * j)) for j in range(3)]
                    tch = dot([sum(n[j] * chat * rows3[j][k] for j in range(3)) for k in range(3)])
                anim = pose - t308 - tlean - tch
                Q = quat_rows(*struct.unpack_from("<4f", raw, b + off(0x170)))
                R = [struct.unpack_from("<3f", raw, b + off(0x4AA0 + 16 * j)) for j in range(3)]
                B = [struct.unpack_from("<3f", raw, b + off(0x4A60 + 16 * j)) for j in range(3)]
                g = struct.unpack_from("<3f", raw, b + off(0x2A0))
                ori = orient(Q, R, B, [x / un for x in u], rd(0x214), classes[r])
                rows.append([sess, r, classes[r], frame, f"{p[0]:.3f}", f"{p[1]:.3f}", f"{p[2]:.3f}", f"{rd(0x1BC):.5f}", f"{pose:.4f}",
                             f"{scales[r]:.4f}", f"{t308:.4f}", f"{tlean:.4f}", f"{tch:.4f}", f"{anim:.4f}",
                             f"{rd(0x208):.5f}", f"{rd(0x268):.5f}", f"{rd(0x1FC):.5f}", f"{rd(0x214):.5f}", int(ori[0])]
                            + [f"{x:.4f}" for x in ori[1:10]] + [ori[10]] + [f"{x:.4f}" for x in tilt(Q, [x / un for x in u], g)])
    OUT.parent.mkdir(parents=True, exist_ok=True)
    with open(OUT, "w", newline="", encoding="utf-8") as fh:
        w = csv.writer(fh)
        w.writerow(["session", "rider", "board_class", "frame", "px", "py", "pz", "h", "board_pose",
                    "scale", "t308", "tlean", "tchatter", "tanim", "crouch", "steer_follow", "brake",
                    "steer", "stance", "align", "lean", "lean_res", "body_roll", "body_pitch", "root_roll", "board_yaw",
                    "board_roll", "anim_roll", "stale", "slope", "tilt", "tilt_target"])
        w.writerows(rows)
    print(f"wrote {OUT} ({len(rows)} rows)")
    for c in range(3):
        hs = sorted(float(x[7]) for x in rows if x[2] == c)
        ps = sorted(float(x[8]) for x in rows if x[2] == c)
        if hs:
            print(f"class {c}: n {len(hs)} h p50 {hs[len(hs) // 2]:.3f} board pose p50 {ps[len(ps) // 2]:.3f}")


if __name__ == "__main__":
    main()
