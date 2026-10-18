"""Track world data decoders (rails, collision flags, grid, lights, lightmaps).

  python scripts/world_data.py <track dir or stem path> [--rails] [--inst] [--grid] [--lights]

Files (all in DATA/MODELS/<TRACK>.BIG -> data/models/):
  .pbd  geometry. Spline record 40 B, spline segment record **128 B** (not 124: the rebuild's
        tools/pbd.py SPLINE_SEGMENT_SIZE is wrong, only segment 0 of each file decodes there).
  .ltg  world grid (= the in-memory world collision object, loaded in place, offsets fixed up).
  .ssf  track logic. Header pointers used here: +0x38 instance records (24 B), +0x3C instance
        count, +0x40 per-instance record index (i32), +0x44 spline count, +0x48 spline table
        (8 B: u32 flags, u32 surface id). Effect main type 25 = "set rail enabled".
Offsets are file offsets.
"""
import struct
import sys
from collections import Counter
from dataclasses import dataclass, field
from pathlib import Path

import numpy as np

REBUILD = Path(__file__).resolve().parents[3]
SEG_SIZE = 128
SPLINE_SIZE = 40


def track_files(arg):
    p = Path(arg)
    if p.is_dir():
        d = p / "data" / "models" if (p / "data").exists() else p
        stem = next(d.glob("*.pbd")).stem
        stem = [s.stem for s in d.glob("*.pbd") if not s.stem.endswith("_sky")][0]
        return d, stem
    return p.parent, p.stem


def pbd_header(d):
    counts = struct.unpack_from("<15i", d, 4)
    offs = struct.unpack_from("<18i", d, 64)
    cn = ("player_starts patches instances particle_instances materials material_blocks lights "
          "splines spline_segments texture_flipbooks models particle_models textures cameras "
          "lightmap_size").split()
    on = ("player_start patch instance particle_instance material material_block light spline "
          "spline_segment texture_flipbook model_pointer models particle_model_pointer "
          "particle_models camera_pointer cameras hash mesh_data").split()
    return dict(zip(cn, counts)), dict(zip(on, offs))


@dataclass
class Segment:
    coef: np.ndarray        # (4, 3): rows multiply t^3, t^2, t, 1
    prev: int               # segment index or -1
    next: int
    spline: int
    bmin: np.ndarray
    bmax: np.ndarray
    length: float           # cm
    start_dist: float       # cm from spline start
    dist_poly: tuple        # 4 floats (param from distance)


def read_splines(pbd_bytes):
    counts, offs = pbd_header(pbd_bytes)
    splines = []
    for k in range(counts["splines"]):
        o = offs["spline"] + k * SPLINE_SIZE
        bmin = np.array(struct.unpack_from("<3f", pbd_bytes, o))
        bmax = np.array(struct.unpack_from("<3f", pbd_bytes, o + 12))
        _flags, n, first, _surf = struct.unpack_from("<4i", pbd_bytes, o + 24)
        splines.append(dict(bmin=bmin, bmax=bmax, count=n, first=first))
    segs = []
    for s in range(counts["spline_segments"]):
        o = offs["spline_segment"] + s * SEG_SIZE
        rows = np.array(struct.unpack_from("<16f", pbd_bytes, o)).reshape(4, 4)
        poly = struct.unpack_from("<4f", pbd_bytes, o + 0x40)
        prev, nxt, sp = struct.unpack_from("<3i", pbd_bytes, o + 0x50)
        bmin = np.array(struct.unpack_from("<3f", pbd_bytes, o + 0x5C))
        bmax = np.array(struct.unpack_from("<3f", pbd_bytes, o + 0x68))
        length, start = struct.unpack_from("<2f", pbd_bytes, o + 0x74)
        segs.append(Segment(rows[:, :3].copy(), prev, nxt, sp, bmin, bmax, length, start, poly))
    return splines, segs


def map_names(map_path, section_prefix):
    """Object names from the .map listing: {index: name} for lines starting with the prefix."""
    out = {}
    if not Path(map_path).exists():
        return out
    for line in Path(map_path).read_text(errors="replace").splitlines():
        if line.startswith(section_prefix):
            parts = line.split()
            if len(parts) >= 3 and parts[1].isdigit():
                out[int(parts[1])] = parts[0]
    return out


def ssf_header(s):
    h = struct.unpack_from("<19I", s, 0)
    return dict(n_a=h[3], p_a=h[4], n_spheretree=h[5], p_spheretree=h[6], n_coll=h[7], p_coll=h[8],
                n_c=h[9], p_c=h[10], n_d=h[11], p_d=h[12], n_irec=h[13], p_irec=h[14],
                n_inst=h[15], p_iidx=h[16], n_spl=h[17], p_spl=h[18])


def ssf_spline_table(s):
    h = ssf_header(s)
    return [struct.unpack_from("<II", s, h["p_spl"] + 8 * k) for k in range(h["n_spl"])]


def ssf_rail_effects(s):
    """Effect records {25, 16, spline, enable} (main type 25 = set rail enabled)."""
    n = len(s) // 4
    w = np.frombuffer(s[:n * 4], "<u4")
    idx = np.nonzero((w[:-4] == 25) & (w[1:-3] == 16) & (w[3:-1] <= 1))[0]
    return [(int(i) * 4, int(w[i + 2]), int(w[i + 3])) for i in idx]


def ssf_instance_records(s):
    h = ssf_header(s)
    recs = [struct.unpack_from("<ffIi4h", s, h["p_irec"] + 24 * k) for k in range(h["n_irec"])]
    idx = struct.unpack_from(f"<{h['n_inst']}i", s, h["p_iidx"])
    return recs, idx


def rail_enabled(s, apply_effects=True):
    """Initial enable state per spline: high half of the ssf flag word, bit 0; then (race mode,
    observed on Garibaldi) every effect-25 record applied in file order."""
    en = [((f >> 16) & 1) for f, _ in ssf_spline_table(s)]
    surf = [sid for _, sid in ssf_spline_table(s)]
    if apply_effects:
        for _off, sp, e in ssf_rail_effects(s):
            if 0 <= sp < len(en):
                en[sp] = e & 1
    return en, surf


def read_ltg(b):
    """Grid header and per-cell sub-cell records with their index lists (file offsets are cell-
    relative inside each cell block)."""
    minv = struct.unpack_from("<3f", b, 4)
    maxv = struct.unpack_from("<3f", b, 16)
    cell = struct.unpack_from("<f", b, 0x28)[0]
    nx, ny, ncell = struct.unpack_from("<3i", b, 0x2C)
    sub_size = struct.unpack_from("<f", b, 0x40)[0]
    sub_side, sub_n, table = struct.unpack_from("<3i", b, 0x44)
    offs = struct.unpack_from(f"<{ncell}I", b, table)
    cells = [None] * ncell
    for ci, co in enumerate(offs):
        if not co:
            continue
        cb = struct.unpack_from("<6f", b, co)
        sub0 = co + struct.unpack_from("<I", b, co + 0x38)[0]
        subs = []
        for k in range(sub_n):
            so = sub0 + k * 76
            bb = struct.unpack_from("<6f", b, so)
            n_patch, n_inst, n_inst_alt, n_seg, n_x, n_y = struct.unpack_from("<6H", b, so + 0x24)
            p_patch, p_inst, p_seg, p_x, p_y = struct.unpack_from("<5I", b, so + 0x34)

            def lst(p, n):
                return list(struct.unpack_from(f"<{n}i", b, co + p)) if (p and n) else []
            subs.append(dict(bbox=bb, patches=lst(p_patch, n_patch), inst=lst(p_inst, n_inst),
                             inst_alt=lst(p_inst, n_inst_alt), segs=lst(p_seg, n_seg),
                             n_x=n_x, n_y=n_y))
        cells[ci] = dict(bbox=cb, subs=subs)
    return dict(min=minv, max=maxv, cell=cell, nx=nx, ny=ny, sub_size=sub_size, sub_side=sub_side,
                cells=cells)


def read_lights(pbd_bytes):
    counts, offs = pbd_header(pbd_bytes)
    n = counts["lights"]
    size = (offs["spline"] - offs["light"]) // max(n, 1)
    out = []
    for k in range(n):
        o = offs["light"] + k * size
        out.append(pbd_bytes[o:o + size])
    return size, out


def main():
    d, stem = track_files(sys.argv[1])
    pbd_b = (d / f"{stem}.pbd").read_bytes()
    ssf_b = (d / f"{stem}.ssf").read_bytes()
    ltg_b = (d / f"{stem}.ltg").read_bytes()
    args = set(sys.argv[2:]) or {"--rails", "--inst"}
    if "--rails" in args:
        splines, segs = read_splines(pbd_b)
        names = map_names(d / f"{stem}.map", "Spline_")
        en, surf = rail_enabled(ssf_b)
        c = Counter()
        for k in range(len(splines)):
            base = names.get(k, "?").rsplit("_", 1)[0]
            c[(base, en[k], surf[k])] += 1
        print(f"{stem}: {len(splines)} splines, {len(segs)} segments, rail effects "
              f"{len(ssf_rail_effects(ssf_b))}")
        for k, v in sorted(c.items()):
            print("  ", k, v)
    if "--inst" in args:
        recs, idx = ssf_instance_records(ssf_b)
        hi = [recs[i][2] >> 16 for i in idx]
        print(f"instances {len(idx)}: flag 0x20 (collide) {sum(1 for h in hi if h & 0x20)}, "
              f"0x80 (reaction) {sum(1 for h in hi if h & 0x80)}, 0x1000 {sum(1 for h in hi if h & 0x1000)}, "
              f"0x2000 {sum(1 for h in hi if h & 0x2000)}")
    if "--grid" in args:
        g = read_ltg(ltg_b)
        nsub = sum(1 for c in g["cells"] if c for s in c["subs"] if s["segs"])
        print(f"grid {g['nx']}x{g['ny']} cell {g['cell']} sub-cells with rail segments {nsub}")
    if "--lights" in args:
        size, ls = read_lights(pbd_b)
        print("light record size", size, "count", len(ls))


if __name__ == "__main__":
    main()


def build_rail_world(track_dir, enabled=None):
    """RailWorld (models/rail.py) from the track files. enabled: optional per-spline override."""
    sys.path.insert(0, str(Path(__file__).resolve().parent.parent / "models"))
    import rail
    d, stem = track_files(track_dir)
    pbd_b = (d / f"{stem}.pbd").read_bytes()
    ssf_b = (d / f"{stem}.ssf").read_bytes()
    g = read_ltg((d / f"{stem}.ltg").read_bytes())
    splines, segs = read_splines(pbd_b)
    en, surf = rail_enabled(ssf_b)
    if enabled is not None:
        en = enabled
    cells = []
    for c in g["cells"]:
        if c is None:
            cells.append(None)
            continue
        subs = []
        for s in c["subs"]:
            empty = not (s["patches"] or s["inst"] or s["inst_alt"] or s["segs"] or s["n_x"] or s["n_y"])
            subs.append(rail.SubCell(np.array(s["bbox"][:3]), np.array(s["bbox"][3:]), empty, s["segs"]))
        cells.append(rail.Cell(np.array(c["bbox"][:3]), np.array(c["bbox"][3:]), subs))
    return rail.RailWorld(
        coef=np.array([s.coef for s in segs]), seg_min=np.array([s.bmin for s in segs]),
        seg_max=np.array([s.bmax for s in segs]), seg_spline=np.array([s.spline for s in segs]),
        spline_enabled=np.array([bool(e) for e in en]), spline_surface=np.array(surf),
        origin=(g["min"][0], g["min"][1]), cell=g["cell"], nx=g["nx"], ny=g["ny"], cells=cells)
