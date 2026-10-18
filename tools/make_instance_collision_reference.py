"""Reference data for the SSX.Collision.Instances / SSX.Collision.PropResponse tests, generated from
tools/lib/models/instance_collision.py (instance_query, prop_query, prop_response, ltg_order, SphereTree).
Usage:  python tools/make_instance_collision_reference.py      (run tools/export_instance_collision.py first)
Writes (git-ignored, derived from the user's disc; the tests skip when missing) in Unreal/SSXTrickyRebuild/SimData:
  instcol_counts.csv    per track: collidable / surfaced / unsurfaced / modes-3/5-only / sphere-tree instances,
                        geometry nodes, two-sided mesh nodes, unique trees and their leaf spheres
"""
import libpath  # noqa: F401,E402  (tools/lib on sys.path)
import traces  # noqa: E402

traces.require()
TR = traces.root()

import csv
import json
import struct
import sys
from pathlib import Path

import numpy as np

ROOT = Path(__file__).resolve().parent.parent
import collision as col  # noqa: E402
import instance_collision as ic  # noqa: E402

SIMDATA = ROOT / "Unreal" / "SSXTrickyRebuild" / "SimData"
MODELS = ROOT / "unpacked" / "MODELS"
LINE_TRACKS = ["megaple", "merquer", "snow", "aloha"]
SPHERE_TRACKS = ["gari", "merquer", "mesa", "elysium", "untrack"]
SURF_SPHERE_TRACKS = ["megaple", "merquer"]
PROP_TRACKS = ["gari", "merquer", "mesa", "elysium", "snow", "alaska"]
CASES_PER_TRACK = 150
ORDER_PER_TRACK = 40
LIVE_SESSION = TR / "live" / "ride_session_1"
LIVE_PER_CLASS = 400


def stem_for(track):
    return next(p for p in MODELS.rglob(f"{track}.pbd")).with_suffix("")


def f(x):
    return repr(float(x))


def vec(v):
    return [f(x) for x in v]


def unique_trees(insts):
    seen, out = set(), []
    for i in insts:
        for n in i.nodes:
            if n.tree is not None and id(n.tree) not in seen:
                seen.add(id(n.tree))
                out.append(n.tree)
    return out


def random_shape(rng, c, big, n_sub_max, mask):
    subs = []
    for _k in range(rng.integers(0, n_sub_max + 1)):
        off = rng.normal(size=3)
        off *= rng.uniform(0, big * 0.6) / np.linalg.norm(off)
        subs.append((c + off, rng.uniform(10, big * 0.5)))
    return col.SphereSet(c, big, subs, mask)


def shape_txt(shape):
    return ";".join(" ".join(f(x) for x in (*sc, sr)) for sc, sr in shape.subs)


def hit_cols(h):
    if h is None:
        return [0, f(-1), "0", "0", "0", -1, -1, -1]
    return [1, f(h.t), *vec(h.normal), h.surface, h.cell[0], h.kind]


HEADER = ["query", "track", "alt", "x0", "y0", "z0", "x1_or_r", "y1", "z1", "tref", "fine", "mask", "extra",
          "hit", "t", "nx", "ny", "nz", "surface", "instance", "kind"]


def main():
    rng = np.random.default_rng(21)
    tracks = sorted(p.stem for p in MODELS.rglob("*.pbd") if not p.stem.endswith("_sky"))
    loaded = {}
    with open(SIMDATA / "instcol_counts.csv", "w", newline="") as fc, open(SIMDATA / "instcol_trees.csv", "w", newline="") as ft:
        wc, wt = csv.writer(fc), csv.writer(ft)
        wc.writerow(["track", "collidable", "surfaced", "unsurfaced", "alt_only", "sphere_tree", "nodes", "two_sided",
                     "trees", "leaves"])
        wt.writerow(["track", "tree", "depth", "cx", "cy", "cz", "leaves"])
        for t in tracks:
            insts, ltg = ic.load_track(stem_for(t), keep_ltg=True)
            loaded[t] = (insts, ltg)
            trees = unique_trees(insts)
            leaves = [len(tr.leaves()) for tr in trees]
            for k, tr in enumerate(trees):
                wt.writerow([t, k, tr.depth, *vec(tr.center), leaves[k]])
            wc.writerow([t, len(insts), sum(i.surface != -1 for i in insts), sum(i.surface == -1 for i in insts),
                         sum(i.alternate_only for i in insts), sum(i.mtype == ic.TYPE_SPHERE_TREE for i in insts),
                         sum(len(i.nodes) for i in insts), sum(n.two_sided for i in insts for n in i.nodes if n.kind == 1),
                         len(trees), sum(leaves)])
            print(f"{t}: {len(insts)} collidable, {len(trees)} trees, {sum(leaves)} leaves")

    rows = []
    for t in LINE_TRACKS:
        insts, ltg = loaded[t]
        surfaced = [i for i in insts if i.surface != -1 and i.tris]
        for alt in (False, True):
            world = ic.InstanceWorld(insts, alternate_mode=alt, ltg=ltg)
            for _ in range(CASES_PER_TRACK // 2):
                inst = surfaced[rng.integers(len(surfaced))]
                p = inst.lo + rng.random(3) * (inst.hi - inst.lo)
                if rng.random() < 0.6:
                    a, b = np.array([p[0], p[1], inst.hi[2] + 150.0]), np.array([p[0], p[1], inst.lo[2] - 150.0])
                else:
                    d = rng.normal(size=3)
                    d *= rng.uniform(50, 400) / np.linalg.norm(d)
                    a, b = p - d, p + d
                t_ref = [0.5, -1.0, 0.25][rng.integers(3)]
                h = world.line_query(a, b, t_ref)
                rows.append(["line", t, int(alt), *vec(a), *vec(b), f(t_ref), 0, 0, "", *hit_cols(h)])

    def sphere_cases(query, track_list, pick, n_sub_max):
        for t in track_list:
            insts, ltg = loaded[t]
            pool = [i for i in insts if pick(i) and i.nodes]
            for alt in (False, True):
                world = ic.InstanceWorld(insts, alternate_mode=alt, ltg=ltg)
                for _ in range(CASES_PER_TRACK // 2):
                    inst = pool[rng.integers(len(pool))]
                    c = inst.lo - 40.0 + rng.random(3) * (inst.hi - inst.lo + 80.0)
                    big = rng.uniform(40, 140)
                    if query == "prop":
                        motion = int(rng.choice([1, 2, 3, 5]))
                        mask = ic.MOTION_MASK[motion]
                        shape = random_shape(rng, c, big, n_sub_max, mask)
                        h = world.prop_query(shape)
                        rows.append(["prop", t, int(alt), *vec(c), f(big), "0", "0", "-1", 0, mask, shape_txt(shape) + "|",
                                     *hit_cols(h)])
                        continue
                    mask = int(rng.choice([-1, 0x3, 0x5, 0xFC3, 0]))
                    shape = random_shape(rng, c, big, n_sub_max, mask)
                    fine = bool(rng.integers(2))
                    fd = None
                    if rng.random() < 0.4:
                        fd = rng.normal(size=3)
                        fd /= np.linalg.norm(fd)
                    h = world.sphere_query(shape, fine=fine, filter_dir=fd, surfaced=(query == "wall"))
                    fd_txt = " ".join(f(x) for x in fd) if fd is not None else ""
                    rows.append([query, t, int(alt), *vec(c), f(big), "0", "0", "0", int(fine), mask, f"{shape_txt(shape)}|{fd_txt}",
                                 *hit_cols(h)])

    sphere_cases("sphere", SPHERE_TRACKS, lambda i: i.surface == -1, 4)
    sphere_cases("wall", SURF_SPHERE_TRACKS, lambda i: i.surface != -1, 4)
    sphere_cases("prop", PROP_TRACKS, lambda i: i.surface == -1, 18)
    sphere_cases("prop", ["merquer", "elysium", "snow", "gari"], lambda i: i.mtype == ic.TYPE_SPHERE_TREE, 18)

    for t in PROP_TRACKS:
        insts, (hdr, cells) = loaded[t]
        coll = {i.index for i in insts}
        for alt in (False, True):
            for _ in range(ORDER_PER_TRACK // 2):
                inst = insts[rng.integers(len(insts))]
                c = (inst.lo + inst.hi) / 2 + rng.normal(size=3) * 300.0
                r = rng.uniform(50, 2500)
                seq = [k for k in ic.ltg_order(hdr, cells, c - r, c + r, alt) if k in coll]
                rows.append(["order", t, int(alt), *vec(c), f(r), "0", "0", "0", 0, 0, " ".join(map(str, seq)),
                             len(seq), "0", "0", "0", "0", "0", "0", "0"])
    with open(SIMDATA / "instcol_cases.csv", "w", newline="") as fh:
        w = csv.writer(fh)
        w.writerow(HEADER)
        w.writerows(rows)
    for q in ("line", "sphere", "wall", "prop"):
        print(f"{q}: {sum(r[0] == q for r in rows)} cases, {sum(r[13] for r in rows if r[0] == q)} hits")

    resp = []
    for _ in range(2000):
        mass = float(rng.choice([0.0, 1e30, 5.0, 1000.0]))
        e = float(rng.choice([0.5, 0.6, 0.2, 0.03]))
        flags = (int(rng.choice([0x21, 0xA1, 0x1020])) << 16) | 0x2
        inst = ic.Instance(0, flags, -1, 1, False, np.zeros(3), np.zeros(3), mass, e)
        n = rng.normal(size=3)
        n /= np.linalg.norm(n)
        g = rng.normal(size=3) * 0.3 + np.array([0, 0, 1.0])
        g /= np.linalg.norm(g)
        v = rng.normal(size=3) * rng.uniform(100, 3000)
        depth = rng.uniform(0.1, 60)
        motion = int(rng.choice([1, 2, 3, 5, 6]))
        control = int(rng.choice([3, 0xE, 0x16, 1, 0x20]))
        movable = bool(rng.random() < 0.2)
        mr = float(rng.uniform(50, 900))
        hit = col.Hit(1, np.zeros(3), n, -1, depth)
        out = ic.prop_response(inst, hit, v, motion, control, g, rider_mass=mr, movable_controller=movable)
        resp.append([mass, e, flags, *vec(n), *vec(g), *vec(v), f(depth), motion, control, int(movable), f(mr),
                     out.reaction, *vec(out.normal), f(out.approach), *vec(out.dp), *vec(out.v), f(out.impulse), f(out.rumble)])
    with open(SIMDATA / "instcol_response.csv", "w", newline="") as fh:
        w = csv.writer(fh)
        w.writerow(["mass", "e", "flags", "nx", "ny", "nz", "gx", "gy", "gz", "vx", "vy", "vz", "depth", "motion", "control",
                    "movable", "rider_mass", "reaction", "onx", "ony", "onz", "approach", "dpx", "dpy", "dpz", "ovx", "ovy",
                    "ovz", "impulse", "rumble"])
        w.writerows(resp)
    print(f"response: {len(resp)} cases")

    if not (LIVE_SESSION / "frames.bin").is_file():
        print("no live trace; instcol_live.csv not written")
        return
    import instance_live_check as lc  # noqa: E402
    meta = json.loads((LIVE_SESSION / "meta.json").read_text())
    riders = meta["riders"]
    fr = 8 + len(riders) * lc.REC
    raw = np.memmap(LIVE_SESSION / "frames.bin", dtype=np.uint8, mode="r")
    nframes = len(raw) // fr
    insts, ltg = loaded["gari"]
    world = ic.InstanceWorld(insts, alternate_mode=False, ltg=ltg)
    live, per = [], {"none": 0, "bump": 0, "bounce": 0}
    for fi in range(nframes - 1):
        b = bytes(raw[fi * fr:(fi + 1) * fr])
        for k in range(len(riders)):
            o = 8 + k * lc.REC
            motion, control = struct.unpack_from("<2i", b, o + 0x424)
            if motion not in ic.MOTION_MASK:
                continue
            sh = lc.shape_at(b, o, ic.MOTION_MASK[motion])
            if not (sh.radius > 0):
                continue
            hit = world.prop_query(sh)
            if hit is None:
                continue
            inst = world.by_index[hit.cell[0]]
            kname = lc.klass(inst)
            if per[kname] >= LIVE_PER_CLASS:
                continue
            per[kname] += 1
            nb = bytes(raw[(fi + 1) * fr:(fi + 2) * fr])
            p0 = np.array(struct.unpack_from("<3f", b, o + 0x140))
            v0 = np.array(struct.unpack_from("<3f", b, o + 0x150))
            p1 = np.array(struct.unpack_from("<3f", nb, o + 0x140))
            v1 = np.array(struct.unpack_from("<3f", nb, o + 0x150))
            g = np.array(struct.unpack_from("<3f", b, o + 0x2A0))
            out = ic.prop_response(inst, hit, v0, motion, control, g)
            push_seen = float((p1 - p0 - v0 / 60.0) @ out.normal)
            live.append([fi, k, motion, control, ic.MOTION_MASK[motion], *vec(sh.center), f(sh.radius), shape_txt(sh),
                         *vec(v0), *vec(g), *hit_cols(hit), kname, *vec(out.dp), *vec(out.v), f(push_seen),
                         f(float(v1 @ out.normal))])
        if all(c >= LIVE_PER_CLASS for c in per.values()):
            break
    with open(SIMDATA / "instcol_live.csv", "w", newline="") as fh:
        w = csv.writer(fh)
        w.writerow(["frame", "rider", "motion", "control", "mask", "x0", "y0", "z0", "r", "subs", "vx", "vy", "vz", "gx", "gy", "gz",
                    "hit", "t", "nx", "ny", "nz", "surface", "instance", "kind", "class", "dpx", "dpy", "dpz", "ovx", "ovy", "ovz",
                    "push_seen", "vn_next"])
        w.writerows(live)
    print(f"live: {len(live)} rider-frames {per} (frames scanned {fi + 1} of {nframes})")


if __name__ == "__main__":
    main()
