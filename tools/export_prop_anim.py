"""Export the prop animation data the run-time prop nodes read (World/PropNodes.h).
Usage:
    python tools/export_prop_anim.py              # every .pbd under unpacked/MODELS (12 files incl. the front end)
    python tools/export_prop_anim.py gari ssxfe   # selected files
"""
# 0x1E2948 / 0x1E6E28
import libpath  # noqa: F401,E402  (tools/lib on sys.path)
import argparse
import re
import struct
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
sys.path.insert(0, str(Path(__file__).resolve().parent))
import effects as fx  # noqa: E402
import pbd  # noqa: E402
import pbdmodels  # noqa: E402
import prop_anim as pa  # noqa: E402

OUT_DIR = ROOT / "Unreal" / "SSXTrickyRebuild" / "SimData"
OBJECT_HEADER_SIZE = 24
MATERIAL_SIZE = 72
MATERIAL_FLIPBOOK = 0x44   # flipbook index, -1 = none
IDENTITY = (1.0, 0.0, 0.0, 0.0, 0.0, 1.0, 0.0, 0.0, 0.0, 0.0, 1.0, 0.0, 0.0, 0.0, 0.0, 1.0)
STRIP_TYPES = (12, 13)   # Fence, Flag
PACKET_BIAS = 0x10   # RAM packet pointer (mesh entry +0x0C) = file offset + 0x10


def strip_instances(path: Path, n_inst: int) -> set:
    """Instances a Flag / Fence record can reach: an effect of slots 0..4, or a scene-function op 7 (inst, effect)."""
    ssf = path.with_suffix(".ssf")
    if not ssf.exists():
        return set()
    s = fx.parse_ssf(ssf)
    def has(e):
        return e is not None and 0 <= e < len(s.effects) and any(r.type == 0 and r.i(0) in STRIP_TYPES for r in s.effects[e])
    out = {i for i in range(min(len(s.inst_index), n_inst)) if any(has(s.effect_for(i, sl)) for sl in range(5))}
    for _name, recs in s.functions:
        out |= {r.i(0) for r in recs if r.type == 7 and has(r.i(1)) and 0 <= r.i(0) < n_inst}
    return out


def strip_record(L, inst: int):
    """One version-3 strip record (see the module doc), or None when object 0 has no high-LOD mesh with 4 vertices."""
    d, off = L.raw, L.offsets
    mi = L.instances[inst].model_id
    m = off["models"] + L.model_pointers[mi]
    _total, oc, oo, _mb = struct.unpack_from("<4i", d, m)
    scale = struct.unpack_from("<3f", d, m + 0x18)
    o = m + oo
    lod = struct.unpack_from("<6i", d, o)[1]
    if oc < 1 or lod in (0, -1):
        return None
    h = o + lod
    mc = struct.unpack_from("<I", d, h + 32)[0]
    if mc == 0:
        return None
    _elen, _mat, dlen, start, *_ = struct.unpack_from("<7I", d, h + 44 + 4 * mc)
    pk = off["mesh_data"] + start
    q = pk + PACKET_BIAS
    pos = [v / 32767.0 for v in struct.unpack_from("<12h", d, q + 0x100)]
    nrm = [v / 32767.0 for v in struct.unpack_from("<12h", d, q + 0xD0)]
    uv = [v / 4096.0 for v in struct.unpack_from("<8h", d, q + 0x90)]
    p3, _n3, _uv, _strips, _ = pbdmodels.decode_chunk(d, pk, pk + dlen, (1.0, 1.0, 1.0))
    if len(p3) < 4 or any(abs(pos[3 * v + j] * 32767.0 / 32768.0 - p3[v][j]) > 1e-6 for v in range(4) for j in range(3)):
        print(f"  instance {inst} (model {mi}): first packet is not the 4-vertex layout vfunc_86 assumes; no strip")
        return None
    blk = pbdmodels.instance_light_block(L, inst)
    io = off["instance"] + inst * pbdmodels.INSTANCE_SIZE
    c4, c8 = struct.unpack_from("<2i", d, io + 0xC4)
    flags = struct.unpack_from("<I", d, io + 0xE8)[0]
    return struct.pack("<i3f12f12f8f9f12f4fiiI", inst, *scale, *pos, *nrm, *uv, *blk.dirs.reshape(-1),
                       *blk.colours.reshape(-1), *blk.ambient, c4, c8, flags)


def export(path: Path, out_dir: Path) -> bool:
    L = pbd.load(path)
    d, off = L.raw, L.offsets
    models, objects, anims, segs = [], [], [], []
    blocks, pos = [], off["material_block"]
    for _ in range(L.counts["material_blocks"]):
        n = struct.unpack_from("<i", d, pos)[0]
        blocks.append(list(struct.unpack_from(f"<{n}i", d, pos + 4)))
        pos += 4 + 4 * n
    n_flip = L.counts["texture_flipbooks"]
    flipbooks, pos = [], off["texture_flipbook"]
    for _ in range(n_flip):
        n = struct.unpack_from("<i", d, pos)[0]
        flipbooks.append([struct.unpack_from("<i", d, pos + 4 + 4 * k)[0] & 0xFFFF for k in range(n)])
        pos += 4 + 4 * n
    mats = []
    for k in range(L.counts["materials"]):
        o = off["material"] + MATERIAL_SIZE * k
        tex, flip = struct.unpack_from("<h", d, o)[0], struct.unpack_from("<i", d, o + MATERIAL_FLIPBOOK)[0]
        if not -1 <= flip < n_flip:
            print(f"{path.stem}: material {k} flipbook {flip} out of range ({n_flip}); not written")
            return False
        mats.append((tex, flip))
    model_mats = []
    for mi, ptr in enumerate(L.model_pointers):
        m = off["models"] + ptr
        _total, oc, oo, _mb = struct.unpack_from("<4i", d, m)
        block = blocks[_mb] if 0 <= _mb < len(blocks) else []
        model_mats.append([mats[b] if 0 <= b < len(mats) else (-1, -1) for b in block])
        flags = struct.unpack_from("<I", d, m + 0x10)[0]
        end = pa.model_anim_end_frame(L, mi)
        node_anims = pa.model_node_anims(L, mi)
        models.append((end, flags, len(objects), oc))
        for k in range(oc):
            o = m + oo + OBJECT_HEADER_SIZE * k
            f = struct.unpack_from("<6i", d, o)
            parent, lod_off, mat_off = f[0], f[1], f[5]
            meshes = struct.unpack_from("<I", d, o + lod_off + 32)[0] if lod_off not in (0, -1) else 0
            mat = struct.unpack_from("<16f", d, o + mat_off) if mat_off != -1 else IDENTITY
            ai = -1
            if k in node_anims:
                na = node_anims[k]
                first, count = [], []
                for c in range(6):
                    tr = na.tracks.get(c, [])
                    first.append(len(segs))
                    count.append(len(tr))
                    segs.extend((s.a, s.b, s.c, s.d, s.t0, s.t1) for s in tr)
                ai = len(anims)
                anims.append((na.base, na.mask, first, count))
            objects.append((parent, ai, meshes, 1 if mat_off != -1 else 0, mat))
    out = bytearray(b"SSXP")
    out += struct.pack("<7I", 3, len(models), len(objects), len(anims), len(segs), len(L.instances), 0)
    for end, flags, first, n in models:
        out += struct.pack("<fIii", end, flags, first, n)
    for parent, ai, meshes, has, mat in objects:
        out += struct.pack("<iiiI16f", parent, ai, meshes, has, *mat)
    for base, mask, first, count in anims:
        out += struct.pack("<6fI6i6i", *base, mask, *first, *count)
    for s in segs:
        out += struct.pack("<6f", *s)
    for inst in L.instances:
        out += struct.pack("<i", inst.model_id)
    out += struct.pack("<I", len(flipbooks))
    for fb in flipbooks:
        out += struct.pack(f"<I{len(fb)}i", len(fb), *fb)
    for mm in model_mats:
        out += struct.pack("<I", len(mm))
        for tex, flip in mm:
            out += struct.pack("<ii", tex, flip)
    strips = [r for r in (strip_record(L, i) for i in sorted(strip_instances(path, len(L.instances)))) if r]
    out += struct.pack("<I", len(strips))
    for r in strips:
        out += r
    out_dir.mkdir(parents=True, exist_ok=True)
    stem = path.stem.lower()
    (out_dir / f"{stem}.propanim.bin").write_bytes(out)
    write_surface_look(path, L, out_dir / f"{stem}.surfacelook.txt")
    crowd = sum(1 for m in models if m[1] & 1)
    flip_models = {mi for mi, mm in enumerate(model_mats) if any(f >= 0 for _t, f in mm)}
    flip_inst = sum(1 for inst in L.instances if inst.model_id in flip_models)
    ambiguous = [mi for mi in flip_models
                 if len({(t, f) for t, f in model_mats[mi]}) != len({t for t, _f in model_mats[mi]})]
    print(f"{stem}.propanim.bin: {len(models)} models ({crowd} crowd), {len(objects)} objects, {len(anims)} animated, "
          f"{len(segs)} segments, {len(L.instances)} instances; {len(flipbooks)} flipbooks, {len(flip_models)} flipbook "
          f"models, {flip_inst} flipbook instances; {len(strips)} flag / fence strips" + (f", AMBIGUOUS slot match models {ambiguous}" if ambiguous else ""))
    return True


# Glass, and the clear props that read as glass: Tokyo's outer railing panes, Aloha's shortcut tubes, ice walls, icicles.
GLASS = re.compile(r"glass|window|dome|railing_outer|shortcuttube|icewall|icefacade|iceflow|icechunk|icicle|iceshard", re.I)
WATER = re.compile(r"water|river|wave|ripple", re.I)
NOT_SURFACE = re.compile(r"litwindow|riverbed|bed|frame", re.I)


def map_model_names(path: Path) -> dict:
    """Model index -> name from the MODELS section of the track .map (UID = .pbd model index)."""
    out, sec = {}, None
    m = path.with_suffix(".map")
    if not m.exists():
        return out
    for line in m.read_text(errors="replace").splitlines():
        if line.startswith("###"):
            if "BEGIN" in line or "END" in line:
                sec = line.strip("# ").strip()
            continue
        parts = line.split()
        if sec == "MODELS BEGIN" and len(parts) >= 2 and parts[1].isdigit():
            out[int(parts[1])] = parts[0]
    return out


def write_surface_look(path: Path, L, out_path: Path):
    """Remastered look: the instances whose model is glass (1) or water (2), by model name (World/SSXRemasterWorld.cpp)."""
    names = map_model_names(path)
    rows = []
    for i, inst in enumerate(L.instances):
        n = names.get(inst.model_id, "")
        if not n or NOT_SURFACE.search(n):
            continue
        k = 2 if WATER.search(n) else (1 if GLASS.search(n) else 0)
        if k:
            rows.append(f"{i} {k}")
    out_path.write_text("\n".join(["# instance class (1 glass, 2 water), from the .map model names"] + rows) + "\n")
    return len(rows)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("tracks", nargs="*")
    ap.add_argument("--out", type=Path, default=OUT_DIR)
    args = ap.parse_args()
    wanted = {t.lower() for t in args.tracks}
    ok = True
    for p in sorted((ROOT / "unpacked" / "MODELS").glob("*/data/models/*.pbd")):
        if p.stem.lower().endswith("_sky") or (wanted and p.stem.lower() not in wanted):
            continue
        ok &= export(p, args.out)
    sys.exit(0 if ok else 1)


if __name__ == "__main__":
    main()
