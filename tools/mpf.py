"""Parse SSX Tricky PS2 character / board model containers (.mpf).
Usage: python mpf.py <file.mpf> [--verbose]      (prints a summary of every model in the file)
"""
import argparse
import struct
from dataclasses import dataclass, field
from pathlib import Path

import numpy as np

MODEL_ENTRY_SIZE = 80
MATERIAL_SIZE = 32
BONE_SIZE = 84
IK_SIZE = 16
GROUP_SIZE = 20

GROUP_SKINNED, GROUP_SHADOW, GROUP_MORPH = 1, 17, 256

VIF_V4_32, VIF_V4_16, VIF_V3_16, VIF_V3_32 = 0x6C, 0x6D, 0x79, 0x78
WEIGHT_SLOT_BASE, WEIGHT_SLOT_STRIDE = 14, 4   # VU1 memory address -> palette slot


@dataclass
class Material:
    textures: list   # five 4-char slots ('' when empty); [0] is the main texture
    floats: tuple

    @property
    def main(self) -> str:
        return self.textures[0]

    @property
    def gloss(self) -> int:
        """Signed gloss of the material = word 2 of its runtime draw record (FUN_00205670 0x205670, per mesh group:
        float0 x 127 truncated (EE cvt.w.s) when slot 3 (+0x0C) is non-empty, -float0 x 127 when only slot 4 (+0x10)
        is, else 0). cPS2GraphicsMan::vfunc_92 draws the context-2 pass of every chunk with the second kick (env_pass)
        with FIX = abs(gloss) & 0xFF and the track's specmap `<trk>s` (gloss >= 0) or `<trk>e` (gloss < 0), additive
. The slot texture names themselves are not used by that pass."""
        if self.textures[3]:
            f = self.floats[0]
        elif self.textures[4]:
            f = -self.floats[0]
        else:
            return 0
        return int(np.float32(f) * np.float32(127.0))

    @property
    def kind(self) -> str:
        """'base', 'gloss' or 'envr' - the PS2 renders gloss/env passes as extra layers."""
        if self.textures[3].endswith("_g"):
            return "gloss"
        if self.textures[4] == "envr":
            return "envr"
        return "base"


@dataclass
class Bone:
    name: str
    parent_file: int   # file id of the parent bone's model (-1 = root)
    parent_index: int
    unknown: int
    bone_id: int
    translation: np.ndarray   # local, relative to parent, game units
    rotation: np.ndarray      # local Euler angles (radians), see local_matrix()
    extra: tuple


@dataclass
class Chunk:
    """One VIF chunk: up to ~70 vertices as triangle strips."""
    strips: list
    positions: np.ndarray   # (N, 3) float64, game units, bind pose
    normals: np.ndarray
    uvs: np.ndarray
    weight_slots: np.ndarray   # (N,) palette slot per vertex
    mscal: list = field(default_factory=list)   # 0x318080
    env_pass: bool = False


@dataclass
class MeshBlock:
    palette: int
    chunks: list
    morph_offset: int
    morph_size: int
    morph_deltas: object = None   # (keys, vertices, 3) int8 for morph meshes (face), else None


@dataclass
class MeshGroup:
    type: int
    material: int
    unknown: int
    blocks: list = field(default_factory=list)


@dataclass
class Model:
    name: str
    file_id: int
    morph_key_count: int
    materials: list
    bones: list
    ik_points: np.ndarray
    weight_sets: list   # list of [(bone_index, file_id, weight_fraction), ...]
    weight_lists: list
    groups: list

    @property
    def is_shadow(self) -> bool:
        return "shdw" in self.name.lower()


def _cstr(b: bytes) -> str:
    return b.split(b"\0", 1)[0].decode("ascii", "replace")


def _unpack_size(cmd: int, num: int) -> int:
    vn, vl = (cmd >> 2) & 3, cmd & 3
    return num * (vn + 1) * (4 >> vl)


def _find_unpack(d: bytes, pos: int, end: int, want_cmd: int, count=None):
    """Next UNPACK with command want_cmd (ignoring the flag bit 0x10) at or after pos."""
    pos = (pos + 3) & ~3
    while pos + 4 <= end:
        cmd, num = d[pos + 3], d[pos + 2]
        if cmd & 0x6F == want_cmd & 0x6F and (count is None or num == (count & 0xFF)):
            return pos, num
        pos += 4
    return None


def _read_chunk(d: bytes, pos: int, end: int, group_type: int):
    """Decode the chunk starting near pos. Returns (Chunk, next_pos) or None at the end tag."""
    hit = _find_unpack(d, pos, min(end, pos + 160), VIF_V4_32)
    if hit is None:
        return None
    p, num = hit
    strip_count, _unk1, flags, vcount = struct.unpack_from("<4I", d, p + 4 + 16)
    if not 0 < vcount < 256 or not 0 < strip_count <= vcount or num != strip_count + 2:
        return None
    strips = [struct.unpack_from("<I", d, p + 4 + 16 * (k + 2))[0] for k in range(strip_count)]
    p += 4 + _unpack_size(VIF_V4_32, num)
    if sum(strips) != vcount:
        raise ValueError(f"strip lengths {strips} don't sum to vertex count {vcount} at 0x{p:x}")

    if group_type == GROUP_SHADOW:
        hit = _find_unpack(d, p, end, VIF_V4_32, vcount)
        if hit is None:
            raise ValueError(f"shadow positions missing at 0x{p:x}")
        p, _ = hit
        raw = np.frombuffer(d, "<f4", vcount * 4, p + 4).reshape(-1, 4)
        positions = raw[:, :3].astype(np.float64)
        slots = np.frombuffer(d, "<u4", vcount * 4, p + 4).reshape(-1, 4)[:, 3].astype(np.int64)
        p += 4 + vcount * 16
        empty = np.zeros((0, 3))
        return Chunk(strips, positions, empty, np.zeros((0, 2)),
                     (slots - WEIGHT_SLOT_BASE) // WEIGHT_SLOT_STRIDE), p

    if not flags:
        raise ValueError(f"skinned chunk without UV/normal streams at 0x{p:x}")
    hit = _find_unpack(d, p, end, VIF_V4_16, vcount)
    p, _ = hit
    uvw = np.frombuffer(d, "<i2", vcount * 4, p + 4).reshape(-1, 4).astype(np.int64)
    p += 4 + vcount * 8
    hit = _find_unpack(d, p, end, VIF_V3_16, vcount)
    p, _ = hit
    nrm = np.frombuffer(d, "<i2", vcount * 3, p + 4).reshape(-1, 3) / 32768.0
    p += 4 + vcount * 6
    hit = _find_unpack(d, p, end, VIF_V3_32, vcount)
    p, _ = hit
    positions = np.frombuffer(d, "<f4", vcount * 3, p + 4).reshape(-1, 3).astype(np.float64)
    p += 4 + vcount * 12
    uvs = uvw[:, :2] / 4096.0
    slots = (uvw[:, 2] - WEIGHT_SLOT_BASE) // WEIGHT_SLOT_STRIDE
    return Chunk(strips, positions, nrm, uvs, slots, _kick_mscal(d, p, end)), p


def _kick_mscal(d: bytes, p: int, end: int) -> list:
    """MSCAL immediates of the kick packet after a chunk: DMA cnt-1 tag (+ its two VIF slots), then one qword of
    VIF codes (STCYCL + up to three MSCALs). Rider image 0x318080 entries: 0 skinned draw, 0xA rigid draw, 6 rewrite the
    ST of the drawn buffer from the normals and kick it again (environment pass), 8 kick the constant GIF packet at
    VU 914 (keeps the double-buffer parity), 4 shadow. Empty for the face morph chunks (vfunc_92 sends their MSCAL)."""
    q = (p + 3) & ~3
    while q + 32 <= end and struct.unpack_from("<I", d, q)[0] == 0:
        q += 4
    if q + 32 > end:
        return []
    tag = struct.unpack_from("<I", d, q)[0]
    if tag >> 28 != 1 or tag & 0xFFFF != 1:
        return []
    codes = struct.unpack_from("<6I", d, q + 8)
    return [c & 0xFFFF for c in codes if (c >> 24) & 0x7F == 0x14]


def env_pass_flags(model) -> int:
    """Mark Chunk.env_pass (code: cPS2GraphicsMan::vfunc_92 0x1DF478, VU1 image 0x318080 entry 6 = micro 0x1288)."""
    buf, par, hits, chunks = [None, None], 0, {}, []
    for g in model.groups:
        for blk in g.blocks:
            for c in blk.chunks:
                buf[par] = len(chunks)
                chunks.append(c)
                for code in c.mscal:
                    if code == 6 and buf[par] is not None:
                        hits[buf[par]] = hits.get(buf[par], 0) + 1
                    par ^= 1
    for k, c in enumerate(chunks):
        c.env_pass = k in hits
    return sum(1 for n in hits.values() if n > 1)


def _read_block_chunks(d: bytes, start: int, end: int, group_type: int) -> list:
    chunks, pos = [], start
    while True:
        res = _read_chunk(d, pos, end, group_type)
        if res is None:
            break
        chunk, pos = res
        chunks.append(chunk)
    return chunks


def _read_morph_keys(blob: bytes, start: int, size: int, count: int, n_verts: int):
    """Morph keys of one block: `count` DMA packets of `size` bytes, each a 16-byte
    tag, STCYCL 4/4, NOP and an UNPACK V3-8 (signed, masked) of one s8 xyz delta per block vertex to VU address 0xB6
    (TOPS-relative), the rider VU program adding delta / 32768 x weight x scale x 2560 to the positions."""
    keys = []
    for k in range(count):
        p = start + k * size + 16
        end = start + (k + 1) * size
        while p < end:
            c = struct.unpack_from("<I", blob, p)[0]
            if (c >> 24) & 0x60 == 0x60:
                break
            p += 4
        else:
            raise ValueError("morph key without UNPACK")
        num = (c >> 16) & 0xFF
        if (c >> 24) & 0x0F != 0x0A or num != n_verts:
            raise ValueError(f"morph key {k}: unexpected UNPACK {c:08x} for {n_verts} vertices")
        keys.append(np.frombuffer(blob, np.int8, 3 * num, p + 4).reshape(num, 3))
    return np.stack(keys) if keys else None


def _parse_model(name: str, blob: bytes, e: dict) -> Model:
    materials = []
    for k in range(e["material_count"]):
        o = e["material_offset"] + k * MATERIAL_SIZE
        slots = []
        for s in range(5):
            raw = blob[o + 4 * s:o + 4 * s + 4]
            slots.append("" if raw[0] == 0 else _cstr(raw).strip())
        materials.append(Material(slots, struct.unpack_from("<3f", blob, o + 20)))   # +0x14..; float 0 = gloss

    bones = []
    for k in range(e["bone_count"]):
        o = e["bone_offset"] + k * BONE_SIZE
        pf, pi, unk, bid = struct.unpack_from("<4h", blob, o + 16)
        f = struct.unpack_from("<15f", blob, o + 24)
        bones.append(Bone(_cstr(blob[o:o + 16]), pf, pi, unk, bid,
                          np.array(f[0:3], dtype=np.float64), np.array(f[3:6], dtype=np.float64),
                          f[6:]))

    ik = np.array([struct.unpack_from("<3f", blob, e["ik_offset"] + k * IK_SIZE)
                   for k in range(e["ik_count"])], dtype=np.float64).reshape(-1, 3)

    weight_sets = []
    for k in range(e["weight_count"]):
        n, off, _unk = struct.unpack_from("<3I", blob, e["weight_offset"] + 12 * k)
        entries = []
        for j in range(n):
            w, bone, fid = struct.unpack_from("<hBB", blob, off + 4 * j)
            entries.append((bone, fid, w / 100.0))
        weight_sets.append(entries)

    weight_lists = []
    for k in range(e["list_count"]):
        n, off = struct.unpack_from("<2I", blob, e["list_offset"] + 8 * k)
        weight_lists.append(list(struct.unpack_from(f"<{n}I", blob, off)))

    table, palette = [], 0
    for g in range(e["group_count"]):
        gtype, mat, unk, sub_count, sub_off = struct.unpack_from("<5I", blob, e["group_offset"] + g * GROUP_SIZE)
        blocks = []
        for s in range(sub_count):
            blk_off, blk_count = struct.unpack_from("<2I", blob, sub_off + 8 * s)
            for b in range(blk_count):
                blocks.append((palette, *struct.unpack_from("<3i", blob, blk_off + 12 * b)))
            palette += 1
        table.append((MeshGroup(gtype, mat, unk), blocks))
    starts = sorted({b[1] for _, blocks in table for b in blocks} | {len(blob)})

    groups = []
    for group, blocks in table:
        for pal, model_off, morph_off, morph_size in blocks:
            end = starts[starts.index(model_off) + 1]
            if morph_off > 0:
                end = min(end, model_off + morph_off)
            chunks = _read_block_chunks(blob, model_off, end, group.type)
            deltas = _read_morph_keys(blob, model_off + morph_off, morph_size, e["morph_count"],
                                      sum(len(c.positions) for c in chunks)) if morph_off > 0 else None
            group.blocks.append(MeshBlock(pal, chunks, morph_off, morph_size, deltas))
        groups.append(group)

    model = Model(name, e["file_id"], e["morph_count"], materials, bones, ik, weight_sets,
                  weight_lists, groups)
    env_pass_flags(model)
    return model


def load(path) -> list:
    """Return the list of Models in an .mpf file."""
    d = Path(path).read_bytes()
    _version, count, table, data_start = struct.unpack_from("<IHHI", d, 0)
    models = []
    for i in range(count):
        o = table + i * MODEL_ENTRY_SIZE
        name = _cstr(d[o:o + 16])
        vals = struct.unpack_from("<9I", d, o + 16)
        counts = struct.unpack_from("<8h", d, o + 60)
        e = dict(zip(("data_offset", "blob_size", "bone_offset", "ik_offset", "group_offset",
                      "mesh_offset", "material_offset", "list_offset", "weight_offset"), vals))
        e.update(zip(("weight_count", "list_count", "group_count", "bone_count", "material_count",
                      "ik_count", "morph_count", "file_id"), counts))
        start = data_start + e["data_offset"]
        models.append(_parse_model(name, d[start:start + e["blob_size"]], e))
    return models


def euler_matrix(rx: float, ry: float, rz: float) -> np.ndarray:
    """3x3 rotation for the bone's stored Euler angles (column-vector convention)."""
    def rx_(a):
        c, s = np.cos(a), np.sin(a)
        return np.array([[1, 0, 0], [0, c, -s], [0, s, c]])

    def ry_(a):
        c, s = np.cos(a), np.sin(a)
        return np.array([[c, 0, s], [0, 1, 0], [-s, 0, c]])

    def rz_(a):
        c, s = np.cos(a), np.sin(a)
        return np.array([[c, -s, 0], [s, c, 0], [0, 0, 1]])
    return rz_(-rz) @ ry_(-ry) @ rx_(-rx)


def local_matrix(bone: Bone) -> np.ndarray:
    m = np.eye(4)
    m[:3, :3] = euler_matrix(*bone.rotation)
    m[:3, 3] = bone.translation
    return m


def strip_triangles(strips, positions, normals=None) -> np.ndarray:
    """Expand strips into triangles (alternating winding, degenerates dropped)."""
    tris, base = [], 0
    for n in strips:
        for i in range(2, n):
            a, b, c = base + i - 2, base + i - 1, base + i
            if i % 2:
                b, c = c, b
            pa, pb, pc = positions[a], positions[b], positions[c]
            face = np.cross(pb - pa, pc - pa)
            if np.dot(face, face) < 1e-12:
                continue
            if normals is not None and len(normals):
                if np.dot(face, normals[a] + normals[b] + normals[c]) < 0:
                    b, c = c, b
            tris.append((a, b, c))
        base += n
    return np.array(tris, dtype=np.int64).reshape(-1, 3)


def summary(models, verbose=False) -> str:
    out = []
    for m in models:
        nv = sum(len(c.positions) for g in m.groups for b in g.blocks for c in b.chunks)
        out.append(f"{m.name:16s} file_id={m.file_id} bones={len(m.bones)} mats={len(m.materials)} "
                   f"groups={len(m.groups)} verts={nv} weight_sets={len(m.weight_sets)} "
                   f"palettes={len(m.weight_lists)} ik={len(m.ik_points)} morphs={m.morph_key_count}")
        if verbose:
            for k, mat in enumerate(m.materials):
                out.append(f"   mat {k}: {mat.textures} {mat.kind}")
            for k, b in enumerate(m.bones):
                out.append(f"   bone {k:2d} {b.name:16s} parent=({b.parent_file},{b.parent_index}) "
                           f"t={np.round(b.translation, 2)} r={np.round(b.rotation, 3)}")
            for g in m.groups:
                out.append(f"   group type={g.type} mat={g.material} blocks={len(g.blocks)} "
                           f"chunks={sum(len(b.chunks) for b in g.blocks)}")
    return "\n".join(out)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("mpf", type=Path)
    ap.add_argument("--verbose", "-v", action="store_true")
    args = ap.parse_args()
    print(summary(load(args.mpf), args.verbose))


if __name__ == "__main__":
    main()
