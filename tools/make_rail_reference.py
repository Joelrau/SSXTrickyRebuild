"""Reference data for the SSX.Rail automation tests, generated from tools/lib/models/rail.py
(needs SSX_TRACES for the live Garibaldi grind; prints "skipped (no traces)" without it).
Usage:  python tools/make_rail_reference.py [track]        (default gari; run tools/export_rails.py first)
Writes  Unreal/SSXTrickyRebuild/SimData/<track>.rail_query.csv   query results for sampled points
        Unreal/SSXTrickyRebuild/SimData/<track>.rail_slide.csv   synthetic catch + RailSlide scenarios
        Unreal/SSXTrickyRebuild/SimData/gari.rail_live.csv       live Garibaldi grind (kind 0 slide steps,
"""
import libpath  # noqa: F401,E402  (tools/lib on sys.path)
import traces  # noqa: E402

traces.require()
TR = traces.root()

import csv
import json
import math
import struct
import sys
from pathlib import Path

import numpy as np

ROOT = Path(__file__).resolve().parent.parent
import rail  # noqa: E402
import world_data as wd  # noqa: E402

SIMDATA = ROOT / "Unreal" / "SSXTrickyRebuild" / "SimData"
TRACE = TR / "live" / "ride_session_1"
TRACE_BLOCKS = [(0, 1536), (17920, 1280), (22448, 80)]


def f(x):
    return repr(float(x))


def check_export(track, world):
    p = SIMDATA / f"{track}.rails.bin"
    b = p.read_bytes()
    assert b[:4] == b"SSXR", p
    _ver, nspl, nseg, _flags, _neff, _r0, _r1 = struct.unpack_from("<7I", b, 4)
    assert nseg == len(world.coef) and nspl == len(world.spline_enabled), (nspl, nseg)
    so = 32 + 48 * nspl
    for k in range(nspl):
        en = struct.unpack_from("<I", b, 32 + 48 * k + 40)[0]
        surf = struct.unpack_from("<i", b, 32 + 48 * k + 32)[0]
        assert bool(en) == bool(world.spline_enabled[k]) and surf == int(world.spline_surface[k]), k
    for s in range(nseg):
        coef = np.array(struct.unpack_from("<12f", b, so + 112 * s)).reshape(4, 3)
        spl = struct.unpack_from("<i", b, so + 112 * s + 72)[0]
        assert np.array_equal(coef, world.coef[s]) and spl == world.seg_spline[s], s
    print(f"{p.name}: matches world_data ({nspl} splines, {nseg} segments)")


def query_rows(world, rng, n_near=600, n_far=100):
    rows = []
    en_segs = [s for s in range(len(world.coef)) if world.spline_enabled[world.seg_spline[s]]]
    dis_segs = [s for s in range(len(world.coef)) if not world.spline_enabled[world.seg_spline[s]]]
    pts = []
    for _ in range(n_near):
        s = en_segs[rng.integers(len(en_segs))]
        p = rail.eval_seg(world.coef[s], rng.uniform(-0.05, 1.05)) + rng.uniform(-260, 260, 3)
        pts.append(p)
    for _ in range(min(100, len(dis_segs))):
        s = dis_segs[rng.integers(len(dis_segs))]
        pts.append(rail.eval_seg(world.coef[s], rng.uniform(0, 1)) + rng.uniform(-60, 60, 3))
    lo, hi = world.seg_min.min(0), world.seg_max.max(0)
    for _ in range(n_far):
        pts.append(rng.uniform(lo, hi))
    for p in pts:
        h = rail.query_rail(world, p)
        if h is None:
            rows.append([*map(f, p), 0] + ["0"] * 10)
        else:
            rows.append([*map(f, p), 1, *map(f, h.point), *map(f, h.tangent), h.surface, h.segment, f(h.t),
                         f(h.distance)])
    return rows


SLIDE_HEADER = ["scenario", "step", "result", "steer", "boost", "ox", "oy", "oz",
                "r0x", "r0y", "r0z", "r1x", "r1y", "r1z", "r2x", "r2y", "r2z",
                "upx", "upy", "upz", "sx", "sy", "sz",
                "px", "py", "pz", "vx", "vy", "vz", "rail_time", "roll", "filter", "filter_target",
                "grind", "surface", "rpx", "rpy", "rpz"]
BOARD_OFF = 4.0   # synthetic board origin = body position + 4 cm along the board up (fixed rows)


def _frame_from(t, yaw_deg):
    h = np.array([t[0], t[1], 0.0])
    h /= np.linalg.norm(h)
    c, s = math.cos(math.radians(yaw_deg)), math.sin(math.radians(yaw_deg))
    r0 = np.array([c * h[0] - s * h[1], s * h[0] + c * h[1], 0.0])
    r1 = np.array([0.0, 0.0, 1.0])
    r2 = np.cross(r0, r1)
    return np.array([r0, r1, r2])


def run_scenario(world, sid, seg, t0, yaw, speed, lat, steer_fn, boost_fn, steps, out):
    coef = world.coef[seg]
    p_rail = rail.eval_seg(coef, t0)
    tan = rail.eval_tangent(coef, t0)
    tan /= np.linalg.norm(tan)
    rows = _frame_from(tan, yaw)
    origin = p_rail + 3.0 * rows[1] - lat * rows[2] - 2.0 * rows[0]
    rd = rail.RailRider(pos=origin - BOARD_OFF * rows[1], vel=speed * (math.cos(0.2) * tan + math.sin(0.2) * rows[2]),
                        board_rows=rows.copy(), board_origin=origin.copy(), up=rows[1].copy(), side=rows[2].copy())

    def emit(step, result):
        rp = rd.rail_point if rd.rail_point is not None else np.zeros(3)
        out.append([sid, step, result, f(rd.steer), f(rd.boost), *map(f, rd.board_origin), *map(f, rd.board_rows.ravel()),
                    *map(f, rd.up), *map(f, rd.side), *map(f, rd.pos), *map(f, rd.vel), f(rd.rail_time), f(rd.roll),
                    f(rd.box_filter), f(rd.box_target), rd.grind_type, rd.surface, *map(f, rp)])

    emit(-1, 0)   # initial state (before the catch)
    ok = rail.attach_test(rd, world)
    emit(0, 0 if ok else 3)   # 3 = no catch
    if not ok:
        return False
    for k in range(1, steps + 1):
        rd.steer, rd.boost = steer_fn(k), boost_fn(k)
        rail.filter_step(rd)
        rd.board_origin = rd.pos + BOARD_OFF * rd.board_rows[1]
        no_hit = rail.query_rail(world, rd.board_origin) is None
        on = rail.slide_step(rd, world)
        if on:
            emit(k, 0)
            continue
        emit(k, 2 if no_hit else 1)
        return True
    return True


def slide_rows(world):
    out = []
    best = None
    for s in range(len(world.coef)):
        if not world.spline_enabled[world.seg_spline[s]]:
            continue
        a, b = rail.eval_seg(world.coef[s], 0.0), rail.eval_seg(world.coef[s], 1.0)
        L = np.linalg.norm(b - a)
        tz = abs((b - a)[2]) / max(L, 1e-9)
        if 1500 < L and tz < 0.35 and (best is None or L > best[1]):
            best = (s, L)
    seg = best[0]
    run_scenario(world, 0, seg, 0.15, 12.0, 900.0, 6.0, lambda k: 0.0 if k < 70 else 0.7,
                 lambda k: 1.0 if 20 <= k < 50 else 0.0, 400, out)
    run_scenario(world, 1, seg, 0.4, -60.0, 300.0, -4.0, lambda k: -0.5, lambda k: 0.0, 400, out)
    term = [s for s in range(len(world.coef)) if world.spline_enabled[world.seg_spline[s]]]
    for s in term:
        a, b = rail.eval_seg(world.coef[s], 0.9), rail.eval_seg(world.coef[s], 1.0)
        L = np.linalg.norm(b - a)
        d = (b - a) / max(L, 1e-9)
        if 100 < L < 400 and abs(d[2]) < 0.3:
            if world_next(world, s) < 0:
                run_scenario(world, 2, s, 0.9, 0.0, 700.0, 0.0, lambda k: 0.0, lambda k: 0.0, 400, out)
                break
    return out


_NEXT = {}


def world_next(world, s):
    return _NEXT.get(s, -1)


def live_rows(world):
    meta = json.loads((TRACE / "meta.json").read_text())
    riders = [int(r, 16) for r in meta["riders"]]
    rec = sum(sz for _, sz in TRACE_BLOCKS)
    fr = 8 + len(riders) * rec
    raw = np.memmap(TRACE / "frames.bin", dtype=np.uint8, mode="r")
    n = len(raw) // fr

    def rd(b, off, fmt):
        base = 0
        for start, size in TRACE_BLOCKS:
            if start <= off < start + size:
                return struct.unpack_from(fmt, b, base + off - start)
            base += size
        raise KeyError(hex(off))

    def board(b):
        return [*rd(b, 0x4A90, "<3f"), *rd(b, 0x4A60, "<3f"), *rd(b, 0x4A70, "<3f"), *rd(b, 0x4A80, "<3f")]

    def extra(pb, b):
        return [*map(f, rd(pb, 0x180, "<3f")), *map(f, rd(b, 0x180, "<3f")), *map(f, rd(b, 0x1A0, "<3f")),
                *map(f, rd(b, 0x2A0, "<3f")), rd(b, 0x46C8, "<i")[0], f(rd(b, 0x46E0, "<f")[0])]

    out = []
    for ri in range(len(riders)):
        def blk(k):
            o = k * fr + 8 + ri * rec
            return bytes(raw[o:o + rec])
        steps, last = [], None
        for k in range(n):
            b = blk(k)
            pos = rd(b, 0x140, "<3f")
            if pos == last:
                continue
            last = pos
            steps.append((k, b))
        for j in range(1, len(steps)):
            k, b = steps[j]
            _, pb = steps[j - 1]
            if rd(b, 0x424, "<i")[0] != 3 or rd(pb, 0x424, "<i")[0] != 3:
                continue
            out.append([0, k, *map(f, board(pb)), *map(f, board(b)),
                        *map(f, rd(pb, 0x140, "<3f")), *map(f, rd(pb, 0x150, "<3f")),
                        *map(f, rd(pb, 0x1A0, "<3f")), *map(f, rd(pb, 0x190, "<3f")),
                        f(rd(b, 0x214, "<f")[0]), f(rd(b, 0x130, "<f")[0]), f(rd(b, 0x12C, "<f")[0]),
                        rd(b, 0x1DC, "<i")[0], f(rd(b, 0x1C4, "<f")[0]), f(rd(pb, 0x284, "<f")[0]),
                        *map(f, rd(b, 0x2D0, "<3f")), rd(b, 0x290, "<i")[0],
                        *map(f, rd(b, 0x140, "<3f")), *map(f, rd(b, 0x150, "<3f")), rd(b, 0x1DC, "<i")[0],
                        *extra(pb, b)])
        for j in range(1, len(steps)):
            k, b = steps[j]
            _, pb = steps[j - 1]
            if rd(pb, 0x424, "<i")[0] != 3 or rd(b, 0x424, "<i")[0] == 3:
                continue
            out.append([2, k, *map(f, board(pb)), *map(f, board(b)),
                        *map(f, rd(pb, 0x140, "<3f")), *map(f, rd(pb, 0x150, "<3f")),
                        *map(f, rd(pb, 0x1A0, "<3f")), *map(f, rd(pb, 0x190, "<3f")),
                        f(rd(b, 0x214, "<f")[0]), f(rd(b, 0x130, "<f")[0]), f(rd(b, 0x12C, "<f")[0]),
                        rd(pb, 0x1DC, "<i")[0], f(rd(b, 0x1C4, "<f")[0]), f(rd(pb, 0x284, "<f")[0]),
                        *map(f, rd(b, 0x2D0, "<3f")), rd(b, 0x290, "<i")[0],
                        *map(f, rd(b, 0x140, "<3f")), *map(f, rd(b, 0x150, "<3f")), rd(b, 0x1DC, "<i")[0],
                        *extra(pb, b)])
        for k in range(1, n):
            b, pb = blk(k), blk(k - 1)
            if rd(b, 0x1DC, "<i")[0] != 0 and rd(pb, 0x1DC, "<i")[0] == 0 and rd(pb, 0x424, "<i")[0] != 3:
                for kk in range(k - 10, k + 1):
                    b2, pb2 = blk(kk), blk(kk - 1)
                    out.append([1, kk, *map(f, board(b2)), *map(f, board(b2)),
                                *map(f, rd(pb2, 0x140, "<3f")), *map(f, rd(pb2, 0x150, "<3f")),
                                *map(f, rd(pb2, 0x1A0, "<3f")), *map(f, rd(pb2, 0x190, "<3f")),
                                "0", "0", "1", 0, "0", f(rd(pb2, 0x284, "<f")[0]),
                                "0", "0", "0", 0,
                                *map(f, rd(b2, 0x140, "<3f")), *map(f, rd(b2, 0x150, "<3f")),
                                rd(blk(k), 0x1DC, "<i")[0] if kk == k else 0, *extra(pb2, b2)])
    return out


LIVE_HEADER = (["kind", "sample"] + [f"a{c}" for c in ("ox oy oz r0x r0y r0z r1x r1y r1z r2x r2y r2z".split())]
               + [f"b{c}" for c in ("ox oy oz r0x r0y r0z r1x r1y r1z r2x r2y r2z".split())]
               + ["px", "py", "pz", "vx", "vy", "vz", "upx", "upy", "upz", "sx", "sy", "sz",
                  "steer", "boost", "time_scale", "grind", "cap", "filter",
                  "rpx", "rpy", "rpz", "surface", "npx", "npy", "npz", "nvx", "nvy", "nvz", "ngrind",
                  "pfx", "pfy", "pfz", "nfx", "nfy", "nfz", "nux", "nuy", "nuz", "nrux", "nruy", "nruz", "nclip", "nayaw"])


def write(path, header, rows):
    with open(path, "w", newline="") as fh:
        w = csv.writer(fh)
        w.writerow(header)
        w.writerows(rows)
    print(f"{path.name}: {len(rows)} rows")


def main():
    track = sys.argv[1] if len(sys.argv) > 1 else "gari"
    tdir = wd.REBUILD / "unpacked" / "MODELS" / track.upper()
    world = wd.build_rail_world(tdir)
    check_export(track, world)
    d, stem = wd.track_files(tdir)
    _spl, segs = wd.read_splines((d / f"{stem}.pbd").read_bytes())
    for i, s in enumerate(segs):
        _NEXT[i] = s.next
    rng = np.random.default_rng(1234)
    write(SIMDATA / f"{track}.rail_query.csv",
          ["px", "py", "pz", "found", "hx", "hy", "hz", "tx", "ty", "tz", "surface", "segment", "t", "dist"],
          query_rows(world, rng))
    write(SIMDATA / f"{track}.rail_slide.csv", SLIDE_HEADER, slide_rows(world))
    if track == "gari" and (TRACE / "frames.bin").is_file():
        write(SIMDATA / "gari.rail_live.csv", LIVE_HEADER, live_rows(world))


if __name__ == "__main__":
    main()
