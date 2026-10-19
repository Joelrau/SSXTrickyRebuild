"""Reference cases for the SSX.World automation tests, from the Python models in tools/lib/models.
Usage: python tools/make_world_reference.py
"""
import libpath  # noqa: F401,E402  (tools/lib on sys.path)
import csv
import json
import random
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
import prop_light as pl  # noqa: E402
import rider_light as rl  # noqa: E402

sys.path.insert(0, str(ROOT / "tools"))
import pbd  # noqa: E402
import pbdmodels  # noqa: E402

STEM_ID = {"gari": 0, "snow": 1, "elysium": 2, "mesa": 3, "merquer": 4, "aloha": 5, "pipe": 6,
           "untrack": 7, "megaple": 8, "trick": 10, "alaska": 11, "ssxfe": 11}   # world_track_table record (game track index)
PER_TRACK = 24
PROP_TRACKS = (("snow", "Snow"), ("gari", "Gari"), ("alaska", "Alaska"))
PROP_INSTANCES = 10
PROP_VERTS = 12


def samples(lights, grid, rnd):
    pts = []
    for l in rnd.sample(lights, min(len(lights), PER_TRACK // 2)):
        if l.type == rl.SPOT:
            pts.append(tuple(l.position[k] + l.direction[k] * 400.0 for k in range(3)))
        elif l.type == rl.POINT:
            pts.append(tuple(l.position[k] + (150.0, -80.0, -60.0)[k] for k in range(3)))
    while len(pts) < PER_TRACK:
        x = grid.min_x + rnd.random() * grid.nx * grid.cell
        y = grid.min_y + rnd.random() * grid.ny * grid.cell
        pts.append((x, y, rnd.uniform(-5000.0, 8000.0)))
    return pts


def vec(v):
    return [f"{x:.6g}" for x in v]


def write_probe(table, sim):
    rnd = random.Random(1234)
    rows = 0
    with (sim / "world_probe_reference.csv").open("w", newline="") as f:
        w = csv.writer(f)
        w.writerow(["stem", "px", "py", "pz", "ar", "ag", "ab"]
                   + [f"{k}{i}{c}" for i in range(3) for k, cs in (("c", "rgb"), ("d", "xyz")) for c in cs]
                   + ["mr", "mg", "mb", "tr", "tg", "tb"])
        for p in sorted((ROOT / "unpacked" / "MODELS").rglob("*.pbd")):
            stem = p.stem
            if stem not in STEM_ID:
                continue
            lights, grid = rl.load_track(p.parent, stem)
            adj = rl.adjust_from_table(table[STEM_ID[stem]])
            for q in samples(lights, grid, rnd):
                r = rl.probe(lights, grid.sublist(q), q, adj)
                cols, dirs = r.game_slots()
                row = [stem, *(f"{v:.4f}" for v in q), *vec(r.ambient)]
                for i in range(3):
                    row += vec(cols[i]) + vec(dirs[i])
                w.writerow(row + vec(r.modulator) + vec(r.total))
                rows += 1
    print(f"{rows} probe cases")


def write_slots(sim, cases=400):
    """Random candidate lists through rider_light.insert_slot. Colours from a coarse grid so equal scores
    (ties keep the earlier light) and negative sums happen often."""
    rnd = random.Random(99)
    with (sim / "world_slot_reference.csv").open("w", newline="") as f:
        w = csv.writer(f)
        w.writerow(["n", "slot0", "slot1", "slot2", "colours r g b ..."])
        for _ in range(cases):
            n = rnd.randint(0, 7)
            cols = [tuple(rnd.choice((-1.0, -0.5, 0.0, 0.25, 0.5, 1.0, 1.5, 2.0)) for _ in range(3)) for _ in range(n)]
            slots = [[0.0, (0.0, 0.0, 0.0), -1] for _ in range(rl.LIGHT_SLOTS)]
            for k, c in enumerate(cols):
                rl.insert_slot(slots, c, k)
            w.writerow([n, *(s[2] for s in slots), *(f"{v:g}" for c in cols for v in c)])
    print(f"{cases} slot cases")


def write_clear(table, sim):
    with (sim / "world_clear_reference.csv").open("w", newline="") as f:
        w = csv.writer(f)
        w.writerow(["id", "track", "r", "g", "b", "a"])
        for rec in table:
            w.writerow([rec["id"], rec["track"], *pl.clear_colour(rec["colour_rgb"])])
    print(f"{len(table)} clear colours")


def write_props(sim):
    rnd = random.Random(4321)
    rows = 0
    with (sim / "prop_light_reference.csv").open("w", newline="") as f:
        w = csv.writer(f)
        w.writerow(["map", "stem", "mesh", "px", "py", "pz", "nx", "ny", "nz", "r", "g", "b", "a"])
        for stem, map_name in PROP_TRACKS:
            path = next((ROOT / "unpacked" / "MODELS").rglob(f"{stem}.pbd"))
            level = pbd.load(path)
            models = pbdmodels.load_models(level)
            picked = 0
            for i in rnd.sample(range(len(level.instances)), len(level.instances)):
                inst = level.instances[i]
                if picked >= PROP_INSTANCES or not 0 <= inst.model_id < len(models):
                    continue
                o = level.offsets["instance"] + i * pbdmodels.INSTANCE_SIZE
                block = pl.PropLightBlock.from_instance(level.raw[o:o + pbdmodels.INSTANCE_SIZE])
                objs = [(k, ob) for k, ob in enumerate(models[inst.model_id].objects) if ob.meshes]
                if not objs:
                    continue
                k, ob = objs[0]
                part = ob.meshes[0]
                for v in rnd.sample(range(len(part.positions)), min(PROP_VERTS, len(part.positions))):
                    w.writerow([map_name, stem, f"{stem}_inst{i:04d}_obj{k}", *(f"{x:.4f}" for x in part.positions[v]),
                                *(f"{x:.6f}" for x in part.normals[v]), *pl.vertex_colour_u8(block, part.normals[v])])
                    rows += 1
                picked += 1
    print(f"{rows} prop vertex colours")


TWO_SIDED_MAX = 150


def write_two_sided(sim):
    rnd = random.Random(99)
    rows = 0
    with (sim / "prop_two_sided_reference.csv").open("w", newline="") as f:
        w = csv.writer(f)
        w.writerow(["map", "stem", "mesh", "two_sided"])
        for stem, map_name in PROP_TRACKS:
            path = next((ROOT / "unpacked" / "MODELS").rglob(f"{stem}.pbd"))
            level = pbd.load(path)
            models = pbdmodels.load_models(level)
            two, one = [], []
            for i, inst in enumerate(level.instances):
                if not 0 <= inst.model_id < len(models):
                    continue
                for k, ob in enumerate(models[inst.model_id].objects):
                    if ob.meshes:
                        (two if ob.two_sided else one).append(f"{stem}_inst{i:04d}_obj{k}")
            w.writerow([map_name, stem, "total", f"{len(two)}/{len(two) + len(one)}"])
            pick2 = rnd.sample(two, min(TWO_SIDED_MAX, len(two)))
            pick1 = rnd.sample(one, min(len(pick2) or 20, len(one)))
            for name in pick2:
                w.writerow([map_name, stem, name, 1])
            for name in pick1:
                w.writerow([map_name, stem, name, 0])
            rows += len(pick2) + len(pick1)
            print(f"{stem}: {len(two)} of {len(two) + len(one)} instance object meshes two-sided")
    print(f"{rows} two-sided reference rows")


TERRAIN_UV_PATCHES = (("gari", "Gari", (180, 181, 188, 190, 191, 192, 193, 197, 198, 203)),
                      ("alaska", "Alaska", ()))
BAND = 8
ICE_BLUE = 20.0



def write_terrain_uv(sim):
    """terrain_uv_reference.csv: per listed patch the corner (S, T) as the VU1 program sends them (image 0x30F960,
    0x518 / 0x628: bilinear, corner 1 = u, corner 2 = v, T not negated), the lightmap tile origin the mesh carries in
    TEXCOORD_2 (to find the patch's vertices), and for its v = 0 / v = 1 edges the (B - R) of the texture band the GS
    samples there and of the neighbour patch across that edge. A 'total' row per track counts, over every transition
    patch (top and bottom bands differ in class), edges whose GS band matches the neighbour's class, and the same with T
    negated."""
    import numpy as np
    import pbd2gltf
    import sshtex
    strip = [(a, b) for a in np.linspace(0.02, 0.98, 16) for b in np.linspace(0.005, 1.0 / BAND - 0.005, 4)]
    edge_uv = {"v0": lambda a, b: (a, b), "v1": lambda a, b: (a, 1 - b),
               "u0": lambda a, b: (b, a), "u1": lambda a, b: (1 - b, a)}
    rows = 0
    with (sim / "terrain_uv_reference.csv").open("w", newline="") as f:
        w = csv.writer(f)
        w.writerow(["map", "stem", "patch", "texture", "lm_u", "lm_v", "s0", "t0", "s1", "t1", "s2", "t2", "s3", "t3",
                    "band_v0", "nb_v0", "band_v1", "nb_v1"])
        for stem, map_name, listed in TERRAIN_UV_PATCHES:
            base = next((ROOT / "unpacked" / "MODELS").rglob(f"{stem}.pbd")).parent
            level = pbd.load(base / f"{stem}.pbd")
            ims = sshtex.read_images(base / f"{stem}.ssh")
            pats = [(i, p) for i, p in enumerate(level.patches) if p.texture >= 0 and p.flag_word >= 0]
            rgb = {}

            def edge_band(p, e, sign):
                """Mean (B - R) of the texels the GS samples in the 1/BAND strip of patch parameter space along edge e:
                (S, T) = bilinear(corners) (T x sign; -1 = the pre-fix port), REPEAT."""
                if p.texture not in rgb:
                    rgb[p.texture] = np.asarray(ims[p.texture][0].convert("RGB")).astype(np.float64)
                a = rgb[p.texture]
                h, wd = a.shape[:2]
                c = [(s, sign * t) for s, t in p.uvs]
                acc = []
                for x, y in strip:
                    u, v = edge_uv[e](x, y)
                    s = (1 - u) * (1 - v) * c[0][0] + u * (1 - v) * c[1][0] + (1 - u) * v * c[2][0] + u * v * c[3][0]
                    t = (1 - u) * (1 - v) * c[0][1] + u * (1 - v) * c[1][1] + (1 - u) * v * c[2][1] + u * v * c[3][1]
                    px = a[int((t % 1.0) * h) % h, int((s % 1.0) * wd) % wd]
                    acc.append(px[2] - px[0])
                return float(np.mean(acc))

            def key(pt):
                return tuple(int(round(c / 5.0)) for c in pt)
            mids = {"v0": (0.5, 0.0), "v1": (0.5, 1.0), "u0": (0.0, 0.5), "u1": (1.0, 0.5)}
            edges = {}
            for i, p in pats:
                for e, uv in mids.items():
                    edges.setdefault(key(p.evaluate(*uv)), []).append((i, e))

            def neighbour(i, p, e):
                """(patch, its edge) across edge e of patch i, or None."""
                return next(((j, f) for j, f in edges.get(key(p.evaluate(*mids[e])), []) if j != i), None)

            match = {1: 0, -1: 0}
            total = {1: 0, -1: 0}
            for i, p in pats:
                if (edge_band(p, "v0", 1) > ICE_BLUE) == (edge_band(p, "v1", 1) > ICE_BLUE):
                    continue
                for e in ("v0", "v1"):
                    nb = neighbour(i, p, e)
                    if nb is None:
                        continue
                    for sign in (1, -1):
                        total[sign] += 1
                        match[sign] += (edge_band(p, e, sign) > ICE_BLUE) == \
                            (edge_band(level.patches[nb[0]], nb[1], sign) > ICE_BLUE)
            w.writerow([map_name, stem, "total", match[1], total[1], match[-1], total[-1]])
            for i in listed:
                p = level.patches[i]
                out = []
                for e in ("v0", "v1"):
                    nb = neighbour(i, p, e)
                    out += [f"{edge_band(p, e, 1):.2f}",
                            f"{edge_band(level.patches[nb[0]], nb[1], 1):.2f}" if nb else "0"]
                w.writerow([map_name, stem, i, p.texture, *(f"{x:.6f}" for x in pbd2gltf.lightmap_origin(p)),
                            *(f"{x:g}" for c in p.uvs for x in c), *out])
                rows += 1
            print(f"{stem}: transition-patch edges whose GS band matches the neighbour's touching band: GS T "
                  f"{match[1]}/{total[1]}, T negated {match[-1]}/{total[-1]}")
    print(f"{rows} terrain uv reference patches")


def main():
    import export_world_lighting
    table = export_world_lighting.track_table_records()      # 0x257DC0, from the exe
    sim = ROOT / "Unreal" / "SSXTrickyRebuild" / "SimData"
    write_probe(table, sim)
    write_slots(sim)
    write_clear(table, sim)
    write_props(sim)
    write_two_sided(sim)
    write_terrain_uv(sim)


if __name__ == "__main__":
    main()
