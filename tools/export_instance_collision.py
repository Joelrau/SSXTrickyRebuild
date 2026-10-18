"""Export the prop-instance collision data the rider simulation reads at run time (format v2).
Usage:
    python tools/export_instance_collision.py               # every track under unpacked/MODELS
    python tools/export_instance_collision.py gari mesa     # selected tracks
    python tools/export_instance_collision.py --check gari  # print the survey only, write nothing
"""
import libpath  # noqa: F401,E402  (tools/lib on sys.path)
import argparse
import struct
import sys
from collections import Counter
from pathlib import Path

import numpy as np

ROOT = Path(__file__).resolve().parent.parent
OUT_DIR = ROOT / "Unreal" / "SSXTrickyRebuild" / "SimData"

MAGIC = b"SSXC"
VERSION = 2
FLAG_GAME_SPACE = 1
COLLIDABLE = 0x20
TYPE_MESH, TYPE_BOX, TYPE_SPHERE_TREE = 1, 2, 3
SET_NORMAL, SET_ALTERNATE = 1, 2
ENTRY_SIZE, NODE_SIZE, MESH_SIZE, TRI_SIZE, TREE_SIZE = 120, 136, 8, 48, 8
SUB_SIZE = 44
CELL_SIZE = 24 + 16 * SUB_SIZE


def ssf_header(s: bytes) -> dict:
    h = struct.unpack_from("<19I", s, 0)
    return dict(n_ev=h[3], p_ev=h[4], n_tree=h[5], p_tree=h[6], n_cm=h[7], p_cm=h[8],
                n_irec=h[13], p_irec=h[14], n_inst=h[15], p_iidx=h[16])


def decode_mesh_list(s: bytes, off: int, size: int, count: int):
    """One type-1 collision model: per geometry node (tris (T,3), verts (V,3), normals (T,3))."""
    meshes, p = [], off
    for _ in range(count):
        nt, nv, pad = struct.unpack_from("<3I", s, p)
        tris = np.frombuffer(s, "<i4", nt * 3, p + 12).reshape(-1, 3)
        pv = p + 12 + nt * 12 + pad
        v4 = np.frombuffer(s, "<f4", nv * 4, pv).reshape(-1, 4)
        n4 = np.frombuffer(s, "<f4", nt * 4, pv + nv * 16).reshape(-1, 4)
        if nt and (tris.min() < 0 or tris.max() >= nv):
            raise ValueError(f"collision mesh at 0x{p:x}: vertex index out of range")
        meshes.append((tris, v4[:, :3].copy(), n4[:, :3].copy()))
        p = pv + nv * 16 + nt * 16
    if p - off != size:
        raise ValueError(f"collision model at 0x{off:x}: decoded {p - off} bytes, entry says {size}")
    return meshes


def tree_blocks(s: bytes, off: int, size: int, count: int):
    """Raw sphere-tree blocks of one type-3 model."""
    out, q = [], off
    for _ in range(count):
        packed, extra, _flag, depth = struct.unpack_from("<4i", s, q)
        n = 0x70 + 12 * (depth + 1) + packed + extra
        out.append(s[q:q + n])
        q += n
    if q - off != size:
        raise ValueError(f"sphere-tree model at 0x{off:x}: decoded {q - off} bytes, entry says {size}")
    return out


def read_ltg(b: bytes):
    gmin = struct.unpack_from("<3f", b, 4)
    cell = struct.unpack_from("<f", b, 0x28)[0]
    nx, ny, ncell = struct.unpack_from("<3i", b, 0x2C)
    sub_n, table = struct.unpack_from("<2i", b, 0x48)
    if sub_n != 16 or ncell != nx * ny:
        raise ValueError("unexpected .ltg grid")
    cells = []
    for co in struct.unpack_from(f"<{ncell}I", b, table):
        if not co:
            cells.append(None)
            continue
        box = struct.unpack_from("<6f", b, co)
        sub0 = co + struct.unpack_from("<I", b, co + 0x38)[0]
        subs = []
        for k in range(sub_n):
            so = sub0 + 76 * k
            sbox = struct.unpack_from("<6f", b, so)
            counts = struct.unpack_from("<6H", b, so + 0x24)
            p_patch, p_inst = struct.unpack_from("<2I", b, so + 0x34)
            if counts[2] < counts[1]:
                raise ValueError("ltg sub-cell with alternate count < normal count")
            patches = list(struct.unpack_from(f"<{counts[0]}i", b, co + p_patch)) if counts[0] else []
            n_i = max(counts[1], counts[2])
            insts = list(struct.unpack_from(f"<{n_i}i", b, co + p_inst)) if n_i else []
            subs.append((sbox, counts, patches, insts))
        cells.append((box, subs))
    return dict(min=gmin, cell=cell, nx=nx, ny=ny), cells


def ltg_sets(cells, n_inst):
    normal, alt = set(), set()
    for c in cells:
        if c:
            for _b, counts, _p, insts in c[1]:
                normal.update(insts[:counts[1]])
                alt.update(insts[counts[1]:counts[2]])
    sets = [SET_NORMAL if i in normal else (SET_ALTERNATE if i in alt else 0) for i in range(n_inst)]
    return sets, len(normal & alt)


def model_nodes(d: bytes, p_mptr: int, p_models: int, model: int):
    """[(node -> model matrix 4x4 float64, geometry?, lod header offset, animated?)] in node order."""
    m = p_models + struct.unpack_from("<i", d, p_mptr + 4 * model)[0]
    _t, n_obj, obj_off, _mb = struct.unpack_from("<4i", d, m)
    objs = []
    for k in range(n_obj):
        ob = m + obj_off + 24 * k
        f = struct.unpack_from("<6i", d, ob)
        mat = np.array(struct.unpack_from("<16f", d, ob + f[5]), float).reshape(4, 4) if f[5] != -1 else np.eye(4)
        objs.append((f, ob, mat))
    chain, out = [], []
    for k, (f, ob, mat) in enumerate(objs):
        chain.append(mat @ chain[f[0]] if 0 <= f[0] < k else mat)
        out.append((chain[k], f[1] != 0, ob + f[1], f[4] != 0))
    return out


def build(stem: Path):
    s = stem.with_suffix(".ssf").read_bytes()
    d = stem.with_suffix(".pbd").read_bytes()
    counts = struct.unpack_from("<15i", d, 4)
    offs = struct.unpack_from("<18i", d, 64)
    n_inst, p_inst, p_mptr, p_models = counts[2], offs[2], offs[10], offs[11]
    h = ssf_header(s)
    recs = [struct.unpack_from("<ffIi4h", s, h["p_irec"] + 24 * k) for k in range(h["n_irec"])]
    idx = struct.unpack_from(f"<{h['n_inst']}i", s, h["p_iidx"])
    if len(idx) != n_inst:
        raise ValueError(f"ssf instance count {len(idx)} != pbd {n_inst}")
    events = [struct.unpack_from("<7i", s, h["p_ev"] + 28 * k) for k in range(h["n_ev"])]
    cms = [decode_mesh_list(s, *struct.unpack_from("<3i", s, h["p_cm"] + 12 * k)) for k in range(h["n_cm"])]
    trees = [tree_blocks(s, *struct.unpack_from("<3i", s, h["p_tree"] + 12 * k)) for k in range(h["n_tree"])]
    ltg_hdr, cells = read_ltg(stem.with_suffix(".ltg").read_bytes())
    sets, mixed = ltg_sets(cells, n_inst)

    mesh_rows, tris, mesh_id = [], [], {}
    tree_rows, blob, tree_id = [], bytearray(), {}
    entries, nodes, draw = [], [], []
    st = Counter()
    for i in range(n_inst):
        r = recs[idx[i]]
        mass, rest, flags, surface, mtype, mindex, ev, _x = r
        draw.append((flags, sets[i]))
        if not (flags >> 16) & COLLIDABLE:
            continue
        st["collidable"] += 1
        st[f"type{mtype}"] += 1
        st["surfaced" if surface != -1 else "unsurfaced"] += 1
        st["alt_only"] += sets[i] == SET_ALTERNATE
        o = p_inst + 256 * i
        M = np.array(struct.unpack_from("<16f", d, o), float).reshape(4, 4)
        lo, hi = struct.unpack_from("<3f", d, o + 204), struct.unpack_from("<3f", d, o + 216)
        touch = events[ev][1] if 0 <= ev < len(events) else -1
        first = len(nodes)
        if mtype in (TYPE_MESH, TYPE_BOX, TYPE_SPHERE_TREE):
            geo = 0
            for chain, has_geo, lod, animated in model_nodes(d, p_mptr, p_models, struct.unpack_from("<i", d, o + 192)[0]):
                if not has_geo:
                    continue
                bits = 2 if animated else 0
                mesh = tree = -1
                blo = bhi = (0.0, 0.0, 0.0)
                if mtype == TYPE_MESH:
                    bits |= struct.unpack_from("<I", d, lod + 0x1C)[0] & 1
                    key = (mindex, geo)
                    if key not in mesh_id:
                        t, v, n = cms[mindex][geo]
                        mesh_id[key] = len(mesh_rows)
                        mesh_rows.append((len(tris), len(t)))
                        tris.extend((v[t[k]], n[k]) for k in range(len(t)))
                    mesh = mesh_id[key]
                    st["two_sided"] += bits & 1
                elif mtype == TYPE_BOX:
                    blo, bhi = struct.unpack_from("<3f", d, lod + 4), struct.unpack_from("<3f", d, lod + 16)
                else:
                    key = (mindex, geo)
                    if key not in tree_id:
                        tree_id[key] = len(tree_rows)
                        tree_rows.append((len(blob), len(trees[mindex][geo])))
                        blob += trees[mindex][geo]
                    tree = tree_id[key]
                nodes.append((mtype, bits, chain, mesh, tree, blo, bhi))
                st["nodes"] += 1
                geo += 1
            expected = len(cms[mindex]) if mtype == TYPE_MESH else (len(trees[mindex]) if mtype == TYPE_SPHERE_TREE else geo)
            if geo != expected:
                raise ValueError(f"instance {i}: {geo} geometry nodes, collision model has {expected} entries")
        entries.append((i, flags, surface, mtype, mindex, sets[i], lo, hi, M, mass, rest, touch, ev, first, len(nodes) - first))
        st["touch_effect"] += touch != -1
    st["mixed_set"] = mixed
    st["meshes"], st["tris"], st["trees"] = len(mesh_rows), len(tris), len(tree_rows)
    return dict(n_inst=n_inst, entries=entries, nodes=nodes, meshes=mesh_rows, tris=tris, trees=tree_rows,
                blob=bytes(blob), draw=draw, ltg=(ltg_hdr, cells), stats=st)


def write(path: Path, x):
    ltg_hdr, cells = x["ltg"]
    cell_index, patch_refs, inst_refs, cell_recs = [], [], [], bytearray()
    for c in cells:
        if c is None:
            cell_index.append(-1)
            continue
        cell_index.append(len(cell_index) - cell_index.count(-1))
        box, subs = c
        cell_recs += struct.pack("<6f", *box)
        for sbox, counts, patches, insts in subs:
            cell_recs += struct.pack("<6f6H2i", *sbox, *counts, len(patch_refs), len(inst_refs))
            patch_refs.extend(patches)
            inst_refs.extend(insts)
    n_cells = sum(1 for c in cells if c is not None)
    out = bytearray(MAGIC + struct.pack("<15I", VERSION, x["n_inst"], len(x["entries"]), len(x["nodes"]), len(x["meshes"]),
                                        len(x["tris"]), len(x["trees"]), len(x["blob"]), FLAG_GAME_SPACE, n_cells,
                                        len(patch_refs), len(inst_refs), 0, 0, 0))
    for i, fl, surf, mt, mi, sets, lo, hi, M, mass, rest, touch, ev, first, count in x["entries"]:
        out += struct.pack("<iIiiiI6f12f2f4i", i, fl, surf, mt, mi, sets, *lo, *hi, *M[:, :3].reshape(-1),
                           mass, rest, touch, ev, first, count)
    for kind, bits, chain, mesh, tree, blo, bhi in x["nodes"]:
        out += struct.pack("<iI12d2i6f", kind, bits, *chain[:, :3].reshape(-1), mesh, tree, *blo, *bhi)
    for row in x["meshes"]:
        out += struct.pack("<2i", *row)
    for v, n in x["tris"]:
        out += struct.pack("<12f", *v.reshape(-1), *n)
    for row in x["trees"]:
        out += struct.pack("<2i", *row)
    out += x["blob"]
    for fl, sets in x["draw"]:
        out += struct.pack("<2I", fl, sets)
    out += struct.pack("<4f4i", *ltg_hdr["min"], ltg_hdr["cell"], ltg_hdr["nx"], ltg_hdr["ny"], 16, 0)
    out += struct.pack(f"<{len(cell_index)}i", *cell_index)
    out += cell_recs
    out += struct.pack(f"<{len(patch_refs)}i", *patch_refs)
    out += struct.pack(f"<{len(inst_refs)}i", *inst_refs)
    assert len(cell_recs) == n_cells * CELL_SIZE
    path.write_bytes(out)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("tracks", nargs="*", help="track names (default: all non-sky .pbd files)")
    ap.add_argument("--out", type=Path, default=OUT_DIR)
    ap.add_argument("--check", action="store_true", help="survey only, write nothing")
    args = ap.parse_args()
    wanted = {t.lower() for t in args.tracks}
    if not args.check:
        args.out.mkdir(parents=True, exist_ok=True)
    for level in sorted((ROOT / "unpacked" / "MODELS").rglob("*.pbd")):
        stem = level.stem.lower()
        if stem.endswith("_sky") or (wanted and stem not in wanted):
            continue
        if not level.with_suffix(".ssf").is_file() or not level.with_suffix(".ltg").is_file():
            print(f"{stem}: no .ssf/.ltg, skipped", file=sys.stderr)
            continue
        x = build(level.with_suffix(""))
        st = x["stats"]
        print(f"{stem}: {x['n_inst']} instances, {st['collidable']} collidable ({st['surfaced']} surfaced, "
              f"{st['unsurfaced']} unsurfaced; mesh {st['type1']}, box {st['type2']}, sphere tree {st['type3']}, "
              f"none {st['type0']}), {st['nodes']} geometry nodes ({st['two_sided']} two-sided mesh nodes), "
              f"{st['alt_only']} modes-3/5 only, {st['mixed_set']} mixed-set, {st['touch_effect']} with a touch effect; "
              f"{st['meshes']} meshes / {st['tris']} tris, {st['trees']} trees")
        if not args.check:
            write(args.out / f"{stem}.instcol.bin", x)


if __name__ == "__main__":
    main()
