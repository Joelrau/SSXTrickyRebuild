"""Decode prop / skybox models from SSX Tricky .pbd files."""
import struct
from dataclasses import dataclass, field

import numpy as np

import pbd

MATERIAL_SIZE = 72
INSTANCE_SIZE = 256
LIGHT_BLOCK = 64   # .pbd instance bytes 64..191: baked prop lighting
MODEL_HEADER_SCALE = 24   # offset of the (x, y, z) scale vector in the model header
OBJECT_HEADER_SIZE = 24
VIF_UNPACK_MASK = 0x60


@dataclass
class MeshPart:
    material: int   # index into the level's material table (-1 if unknown)
    positions: np.ndarray   # (N, 3) float, model space, game units
    normals: np.ndarray
    uvs: np.ndarray
    triangles: np.ndarray


@dataclass
class ModelObject:
    parent: int
    matrix: list   # 16 floats (row-vector convention) or None
    meshes: list = field(default_factory=list)
    two_sided: bool = False   # bit 0 of the u32 at +0x1C of the LOD header


@dataclass
class Model:
    index: int
    scale: tuple
    objects: list = field(default_factory=list)


def load_materials(level: pbd.PBD):
    d, off = level.raw, level.offsets
    textures = [struct.unpack_from("<h", d, off["material"] + k * MATERIAL_SIZE)[0]
                for k in range(level.counts["materials"])]
    blocks, pos = [], off["material_block"]
    for _ in range(level.counts["material_blocks"]):
        n = struct.unpack_from("<i", d, pos)[0]
        blocks.append(list(struct.unpack_from(f"<{n}i", d, pos + 4)))
        pos += 4 + 4 * n
    return textures, blocks


def load_material_flags(level: pbd.PBD) -> list:
    """Material record word +0x40 (u32) per material (
    cPS2GraphicsMan::vfunc_85 0x1E1F70): 0x40000 = translucent (renderer slot 61 sort mode 2: drawn after the opaque
    batches, far to near by the instance origin depth), 0x20000 = sort mode 1 (right after the opaque batches),
    else mode 0 (material order). Retail words 0x15008 / 0x55008 / 0x35008 / 0x15000 / 0x55108 / 0x15108."""
    d, off = level.raw, level.offsets
    return [struct.unpack_from("<I", d, off["material"] + k * MATERIAL_SIZE + 0x40)[0]
            for k in range(level.counts["materials"])]


def _find_unpack(d: bytes, pos: int, end: int, count=None):
    """Return (code_pos, cmd, num) of the next VIF UNPACK at or after pos (4-byte aligned)."""
    pos = (pos + 3) & ~3
    while pos + 4 <= end:
        cmd, num = d[pos + 3], d[pos + 2]
        if cmd & VIF_UNPACK_MASK == VIF_UNPACK_MASK and (count is None or num == (count & 0xFF)):
            return pos, cmd, num
        pos += 4
    raise ValueError(f"VIF UNPACK (count={count}) not found before 0x{end:x}")


def _unpack_size(cmd: int, num: int) -> int:
    vn, vl = (cmd >> 2) & 3, cmd & 3
    return num * (vn + 1) * (4 >> vl)   # vl: 0 = 32-bit, 1 = 16-bit, 2 = 8-bit


def decode_chunk(d: bytes, pos: int, end: int, scale):
    """Decode one VIF chunk. Returns (positions, normals, uvs, strip_lengths, next_pos)."""
    p, cmd, num = _find_unpack(d, pos, end)
    if cmd & 0x6F != 0x6C:
        raise ValueError(f"expected V4-32 header unpack at 0x{p:x}, got 0x{cmd:02x}")
    strip_count, _, vcount, _ = struct.unpack_from("<4I", d, p + 4 + 16)
    if not 0 < vcount < 256 or not 0 < strip_count <= vcount:
        raise ValueError(f"implausible chunk header: {strip_count} strips, {vcount} verts")
    p += 4 + _unpack_size(cmd, num)

    p, cmd, num = _find_unpack(d, p, end)
    strips = [v // 3 for v in struct.unpack_from(f"<{strip_count}h", d, p + 4)]
    p += 4 + _unpack_size(cmd, num)

    p, cmd, num = _find_unpack(d, p, end, vcount)
    uvs = np.frombuffer(d, "<i2", vcount * 2, p + 4).reshape(-1, 2) / 4096.0
    p += 4 + _unpack_size(cmd, num)

    p, cmd, num = _find_unpack(d, p, end, vcount)
    nrm = np.frombuffer(d, "<i2", vcount * 3, p + 4).reshape(-1, 3) / 32768.0
    p += 4 + _unpack_size(cmd, num)

    p, cmd, num = _find_unpack(d, p, end, vcount)
    pos3 = np.frombuffer(d, "<i2", vcount * 3, p + 4).reshape(-1, 3) / 32768.0 * np.asarray(scale)
    p += 4 + _unpack_size(cmd, num)

    if sum(strips) != vcount:
        raise ValueError(f"strip lengths {strips} don't sum to vertex count {vcount}")
    return pos3, nrm, uvs, strips, p


def strips_to_triangles(strips, positions, normals) -> np.ndarray:
    """Expand triangle strips; orient each triangle to agree with its stored vertex normals."""
    tris, base = [], 0
    for n in strips:
        for i in range(2, n):
            a, b, c = base + i - 2, base + i - 1, base + i
            pa, pb, pc = positions[a], positions[b], positions[c]
            face = np.cross(pb - pa, pc - pa)
            if not face.any():
                continue
            if np.dot(face, normals[a] + normals[b] + normals[c]) < 0:
                b, c = c, b
            tris.append((a, b, c))
        base += n
    return np.array(tris, dtype=np.uint32).reshape(-1, 3)


def decode_mesh(d: bytes, start: int, length: int, scale):
    """Decode all chunks of one mesh into merged arrays."""
    end = start + length
    pos, parts = start, []
    while pos < end - 32:
        try:
            pos3, nrm, uvs, strips, pos = decode_chunk(d, pos, end, scale)
        except ValueError:
            if not parts:
                raise
            break
        parts.append((pos3, nrm, uvs, strips))

    positions, normals, uvs_all, tris, offset = [], [], [], [], 0
    for pos3, nrm, uvs, strips in parts:
        tris.append(strips_to_triangles(strips, pos3, nrm) + offset)
        positions.append(pos3)
        normals.append(nrm)
        uvs_all.append(uvs)
        offset += len(pos3)
    return (np.concatenate(positions), np.concatenate(normals),
            np.concatenate(uvs_all), np.concatenate(tris))


def load_models(level: pbd.PBD, lod: str = "high"):
    d, off = level.raw, level.offsets
    _textures, blocks = load_materials(level)
    lod_slot = {"high": 1, "medium": 2, "low": 3}[lod]
    models = []
    for index, ptr in enumerate(level.model_pointers):
        m = off["models"] + ptr
        _total, obj_count, obj_offset, mat_block = struct.unpack_from("<4i", d, m)
        scale = tuple(s if s else 1.0 for s in struct.unpack_from("<3f", d, m + MODEL_HEADER_SCALE))
        model = Model(index, scale)
        for k in range(obj_count):
            o = m + obj_offset + k * OBJECT_HEADER_SIZE
            fields = struct.unpack_from("<6i", d, o)
            parent, lod_off, matrix_off = fields[0], fields[lod_slot], fields[5]
            matrix = list(struct.unpack_from("<16f", d, o + matrix_off)) if matrix_off != -1 else None
            obj = ModelObject(parent, matrix)
            if lod_off not in (0, -1):
                h = o + lod_off
                obj.two_sided = bool(struct.unpack_from("<I", d, h + 0x1C)[0] & 1)
                mesh_count = struct.unpack_from("<I", d, h + 32)[0]
                entries = h + 44 + 4 * mesh_count
                for b in range(mesh_count):
                    _elen, mat_pos, data_len, start, *_ = struct.unpack_from("<7I", d, entries + 28 * b)
                    block = blocks[mat_block] if 0 <= mat_block < len(blocks) else []
                    material = block[mat_pos] if mat_pos < len(block) else -1
                    try:
                        arrays = decode_mesh(d, off["mesh_data"] + start, data_len, scale)
                    except ValueError as e:
                        print(f"  model {index} object {k} mesh {b}: {e}")
                        continue
                    obj.meshes.append(MeshPart(material, *arrays))
            model.objects.append(obj)
        models.append(model)
    return models


@dataclass
class PropLightBlock:
    """3 directional lights + ambient, already in the instance's MODEL space, PS2 colour scale (128 = 1.0)."""
    dirs: np.ndarray   # (3, 3) unit vectors toward the lights (row i = direction i)
    colours: np.ndarray   # (3, 4) RGBA, w = 0
    ambient: np.ndarray   # (4,) RGBA, w = 128


def instance_light_block(level: pbd.PBD, index: int) -> PropLightBlock:
    """Instance record +0x40/+0x50/+0x60: rows x, y, z of the three directions (direction i = column i);
    +0x70 (0, 0, 0, 1) unused; +0x80/+0x90/+0xA0 light colours; +0xB0 ambient."""
    o = level.offsets["instance"] + index * INSTANCE_SIZE
    rows = np.array(struct.unpack_from("<12f", level.raw, o + LIGHT_BLOCK), dtype=np.float64).reshape(3, 4)
    cols = np.array(struct.unpack_from("<12f", level.raw, o + 0x80), dtype=np.float64).reshape(3, 4)
    amb = np.array(struct.unpack_from("<4f", level.raw, o + 0xB0), dtype=np.float64)
    return PropLightBlock(rows[:, :3].T.copy(), cols, amb)


def bake_vertex_colours(block: PropLightBlock, normals: np.ndarray) -> np.ndarray:
    """Per-vertex colour the VU1 lighting loop stores, from the STORED vertex normal n
    (the block is uploaded unchanged next to the instance matrix, so n is not transformed):
        c = min(ambient + sum_i colour_i * max(0, n . dir_i), 255)   all 4 channels, FTOI0 -> u8
    Returns (N, 4) uint8. The VU has no lower clamp; no negative colours exist in the data, the 0 floor
    here only keeps the u8 store defined."""
    d = np.maximum(normals.astype(np.float64) @ block.dirs.T, 0.0)
    c = np.minimum(block.ambient[None, :] + d @ block.colours, 255.0)
    return np.clip(np.trunc(c), 0.0, 255.0).astype(np.uint8)
