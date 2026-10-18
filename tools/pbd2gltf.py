"""Convert an SSX Tricky level (.pbd) into a glTF 2.0 scene: terrain patches + instanced props.
Usage: python pbd2gltf.py <level.pbd> <textures_dir> <out_dir> [--tess N] [--lod high|medium|low]
"""
import argparse
import json
import shutil
from pathlib import Path

import numpy as np
from PIL import Image

import pbd
import pbdmodels
import sshtex

LM_PAGE = 128   # _L.ssh page size, texels
DEFAULT_TESS = 7   # quads per patch edge: the original's near VU level (8 vertices at k/7)
LM_ATLAS_COLS, LM_ATLAS_ROWS = 8, 4

UNIT_SCALE = 0.01  # game units (cm) -> metres
# Basis change Z-up -> Y-up, (x, y, z) -> (x, z, -y). A rotation, so winding is preserved.
BASIS = np.array([[1, 0, 0], [0, 0, 1], [0, -1, 0]], dtype=np.float64)


# FUN_00200390
ALPHA_OPAQUE, ALPHA_CUTOUT, ALPHA_SOFT = "opaque", "cutout", "soft"
GS_AREF = 1


def texel_alpha(png_alpha: np.ndarray) -> np.ndarray:
    """PNG alpha byte -> GS texel alpha At (0..128): the inverse of sshtex.ps2_alpha (SSXGs::TexelAlphaFromPng)."""
    return np.minimum((png_alpha.astype(np.int32) + 1) // 2, 128)


def alpha_class(png_path: Path) -> str:
    """opaque: every At = 128; cutout: At only 0..AREF or 128 (alpha test + full replace, order-independent);
    soft: some 1 < At < 128 texels (they blend)."""
    img = Image.open(png_path)
    if img.mode != "RGBA":
        img = img.convert("RGBA")
    at = texel_alpha(np.asarray(img.getchannel("A")))
    if (at >= 128).all():
        return ALPHA_OPAQUE
    if ((at <= GS_AREF) | (at >= 128)).all():
        return ALPHA_CUTOUT
    return ALPHA_SOFT


def to_gltf_space(v: np.ndarray) -> np.ndarray:
    return v @ BASIS.T


def to_gltf_matrix(game_matrix) -> list:
    """Row-vector game matrix (p' = p @ M) -> column-major glTF node matrix in Y-up metres."""
    m = np.array(game_matrix, dtype=np.float64).reshape(4, 4).T   # column-vector form
    out = np.eye(4)
    out[:3, :3] = BASIS @ m[:3, :3] @ BASIS.T
    out[:3, 3] = BASIS @ m[:3, 3] * UNIT_SCALE
    return out.T.reshape(-1).tolist()   # glTF stores column-major


class GltfBuilder:
    def __init__(self, tex_dir: Path, out_dir: Path):
        self.tex_dir, self.out_dir = tex_dir, out_dir
        self.blob = bytearray()
        self.doc = {k: [] for k in ("nodes", "meshes", "materials", "textures", "images",
                                    "accessors", "bufferViews")}
        self.material_for_texture = {}
        self.texture_info = {}

    def _view(self, arr: np.ndarray, target: int) -> int:
        while len(self.blob) % 4:
            self.blob.append(0)
        self.doc["bufferViews"].append({"buffer": 0, "byteOffset": len(self.blob),
                                        "byteLength": arr.nbytes, "target": target})
        self.blob.extend(arr.tobytes())
        return len(self.doc["bufferViews"]) - 1

    def _accessor(self, arr: np.ndarray, gl_type: str, component: int, target: int, minmax=False) -> int:
        acc = {"bufferView": self._view(arr, target), "componentType": component,
               "count": int(arr.shape[0]), "type": gl_type}
        if minmax:
            acc["min"] = arr.min(axis=0).tolist()
            acc["max"] = arr.max(axis=0).tolist()
        self.doc["accessors"].append(acc)
        return len(self.doc["accessors"]) - 1

    def _texture(self, tex_index: int):
        """Copy the texture PNG next to the glTF once; returns (glTF texture index, GS alpha class) or (None, 'opaque')."""
        if tex_index in self.texture_info:
            return self.texture_info[tex_index]
        info = (None, ALPHA_OPAQUE)
        hits = sorted(self.tex_dir.glob(f"{tex_index:03d}_*.png")) if tex_index >= 0 else []
        if hits:
            src = hits[0]
            dest = self.out_dir / "textures" / src.name
            dest.parent.mkdir(parents=True, exist_ok=True)
            shutil.copyfile(src, dest)
            self.doc["images"].append({"uri": f"textures/{src.name}"})
            self.doc["textures"].append({"source": len(self.doc["images"]) - 1, "sampler": 0})
            info = (len(self.doc["textures"]) - 1, alpha_class(src))
        self.texture_info[tex_index] = info
        return info

    def material(self, tex_index: int, two_sided: bool = False, flags=None, part: str = "") -> int:
        """glTF material per (texture, two-sided, .pbd material flags, part)."""
        key = (tex_index, bool(two_sided), flags, part)
        if key in self.material_for_texture:
            return self.material_for_texture[key]
        tex, cls = self._texture(tex_index)
        name = f"tex_{tex_index:03d}" + ("_2s" if two_sided else "")
        mat = {"name": name, "doubleSided": bool(two_sided),
               "pbrMetallicRoughness": {"metallicFactor": 0.0, "roughnessFactor": 0.9}}
        if flags is not None:
            mat["name"] += f"_f{flags:05x}" + {ALPHA_OPAQUE: "", ALPHA_CUTOUT: "_cut", ALPHA_SOFT: "_soft"}[cls] \
                + (f"_{part}" if part else "")
            mat["extras"] = {"ssx_flags": int(flags), "ssx_alpha": cls}
            if part:
                mat["extras"]["ssx_part"] = part
        if tex is not None:
            mat["pbrMetallicRoughness"]["baseColorTexture"] = {"index": tex}
            if cls != ALPHA_OPAQUE:
                mat["alphaMode"] = "MASK"
        self.doc["materials"].append(mat)
        self.material_for_texture[key] = len(self.doc["materials"]) - 1
        return self.material_for_texture[key]

    def primitive(self, positions, normals, uvs, triangles, tex_index, extra_uvs=(), two_sided=False,
                  flags=None, part: str = "") -> dict:
        """positions in game space (cm, Z-up); converted here. extra_uvs -> TEXCOORD_1, _2, ..."""
        prim = {
            "attributes": {
                "POSITION": self._accessor((to_gltf_space(positions) * UNIT_SCALE).astype(np.float32),
                                           "VEC3", 5126, 34962, minmax=True),
                "NORMAL": self._accessor(to_gltf_space(normals).astype(np.float32), "VEC3", 5126, 34962),
                "TEXCOORD_0": self._accessor(uvs.astype(np.float32), "VEC2", 5126, 34962),
            },
            "indices": self._accessor(triangles.reshape(-1).astype(np.uint32), "SCALAR", 5125, 34963),
            "material": self.material(tex_index, two_sided, flags, part),
        }
        for n, extra in enumerate(extra_uvs, start=1):
            prim["attributes"][f"TEXCOORD_{n}"] = self._accessor(extra.astype(np.float32), "VEC2", 5126, 34962)
        return prim

    def alpha_class_of(self, tex_index: int) -> str:
        return self._texture(tex_index)[1]

    def with_material(self, prim: dict, tex_index: int, two_sided: bool, flags, part: str) -> dict:
        """Copy of a primitive (sharing every accessor) with another material."""
        out = dict(prim)
        out["material"] = self.material(tex_index, two_sided, flags, part)
        return out

    def with_colours(self, prim: dict, colours_u8: np.ndarray) -> dict:
        """Copy of a primitive (sharing its geometry accessors) with its own COLOR_0: (N, 4) u8,
        normalised, so the stored byte b arrives in the material as b / 255."""
        acc = self._accessor(np.ascontiguousarray(colours_u8, dtype=np.uint8), "VEC4", 5121, 34962)
        self.doc["accessors"][acc]["normalized"] = True
        out = dict(prim)
        out["attributes"] = dict(prim["attributes"], COLOR_0=acc)
        return out

    def mesh(self, name: str, primitives: list) -> int:
        self.doc["meshes"].append({"name": name, "primitives": primitives})
        return len(self.doc["meshes"]) - 1

    def node(self, **fields) -> int:
        self.doc["nodes"].append(fields)
        return len(self.doc["nodes"]) - 1

    def write(self, name: str, roots: list):
        (self.out_dir / f"{name}.bin").write_bytes(self.blob)
        doc = {"asset": {"version": "2.0", "generator": "SSXTrickyRebuild pbd2gltf"},
               "scene": 0, "scenes": [{"nodes": roots}],
               **{k: v for k, v in self.doc.items() if v},
               "samplers": [{"magFilter": 9729, "minFilter": 9987, "wrapS": 10497, "wrapT": 10497}],
               "buffers": [{"uri": f"{name}.bin", "byteLength": len(self.blob)}]}
        (self.out_dir / f"{name}.gltf").write_text(json.dumps(doc))


def tessellate(patches, n: int):
    """Return per-patch grids: positions, normals (P, n+1, n+1, 3) and uvs (P, n+1, n+1, 2)."""
    coeffs = np.array([p.coeffs for p in patches], dtype=np.float64).reshape(-1, 4, 4, 3)
    t = np.linspace(0.0, 1.0, n + 1)
    powers = np.stack([np.ones_like(t), t, t * t, t ** 3], axis=1)
    dpowers = np.stack([np.zeros_like(t), np.ones_like(t), 2 * t, 3 * t * t], axis=1)

    pos = np.einsum("ai,pijk,bj->pabk", powers, coeffs, powers)
    du = np.einsum("ai,pijk,bj->pabk", dpowers, coeffs, powers)
    dv = np.einsum("ai,pijk,bj->pabk", powers, coeffs, dpowers)
    nrm = np.cross(du, dv)
    length = np.linalg.norm(nrm, axis=-1, keepdims=True)
    nrm = np.where(length > 1e-9, nrm / np.maximum(length, 1e-9), np.array([0.0, 0.0, 1.0]))

    corner_uv = np.array([p.uvs for p in patches], dtype=np.float64)
    u, v = t[:, None, None], t[None, :, None]
    # the GS bilinear (S, T): S along corner 1 (u), T along corner 2 (v), REPEAT, t = 0 on the first texel row as in
    # glTF / Unreal: T is NOT negated (retail corners (0,0) (1,0) (0,-1) (1,-1) put the texture's bottom row on v = 0)
    uv = ((1 - u) * (1 - v) * corner_uv[:, None, None, 0] + u * (1 - v) * corner_uv[:, None, None, 1]
          + (1 - u) * v * corner_uv[:, None, None, 2] + u * v * corner_uv[:, None, None, 3])
    # 0x30F960
    local = np.broadcast_to(np.stack(np.broadcast_arrays(u[..., 0], v[..., 0]), axis=-1),
                            (len(patches), n + 1, n + 1, 2))
    return pos, nrm, uv, local


def lightmap_origin(p) -> tuple:
    """Atlas-space origin of a patch's lightmap tile (page laid out at col = id % 8, row = id // 8)."""
    page = max(p.lightmap_id, 0)
    col, row = page % LM_ATLAS_COLS, page // LM_ATLAS_COLS
    return (col + p.lightmap_rect[0]) / LM_ATLAS_COLS, (row + p.lightmap_rect[1]) / LM_ATLAS_ROWS


def write_lightmap_atlas(ssh_path: Path, out_png: Path) -> int:
    """Pack the _L.ssh pages into one RGBA atlas with the raw PS2 alpha (0x80 = 1.0). Returns pages."""
    pages = sshtex.read_images(ssh_path, raw_alpha=True)
    if len(pages) > LM_ATLAS_COLS * LM_ATLAS_ROWS:
        raise ValueError(f"{ssh_path.name}: {len(pages)} lightmap pages exceed the atlas")
    atlas = Image.new("RGBA", (LM_ATLAS_COLS * LM_PAGE, LM_ATLAS_ROWS * LM_PAGE), (0, 0, 0, 128))
    for k, (img, _name) in enumerate(pages):
        if img.size != (LM_PAGE, LM_PAGE):
            raise ValueError(f"{ssh_path.name}: page {k} is {img.size}, expected 128x128")
        atlas.paste(img, ((k % LM_ATLAS_COLS) * LM_PAGE, (k // LM_ATLAS_COLS) * LM_PAGE))
    atlas.save(out_png)
    return len(pages)


def grid_indices(n: int) -> np.ndarray:
    """Triangle indices for one (n+1)x(n+1) grid, wound CCW about +du x dv. Each quad splits along (u1, v0)-(u0, v1),
    the diagonal of the original's strips."""
    a = np.arange(n + 1)
    idx = a[:, None] * (n + 1) + a[None, :]
    q00, q10, q01, q11 = idx[:-1, :-1], idx[1:, :-1], idx[:-1, 1:], idx[1:, 1:]
    tris = np.stack([np.stack([q00, q10, q01], -1), np.stack([q10, q11, q01], -1)], axis=-2)
    return tris.reshape(-1, 3)


def add_terrain(gb: GltfBuilder, level: pbd.PBD, name: str, tess: int, showoff: bool = False):
    """One terrain mesh: the normal patches (flag word +0x1AC >= 0) as <name>_terrain, or with showoff the
    ShowOff patches as
    <name>_showoff_terrain, so the game can hide them by mode (Source/SSXTrickyRebuild/SSXShowOffDraw.h)."""
    patches = [p for p in level.patches if p.texture >= 0 and (p.flag_word < 0) == showoff]
    if not patches:
        return None, 0, 0
    pos, nrm, uv, lm_local = tessellate(patches, tess)
    vpp = (tess + 1) ** 2
    pos, nrm, uv = pos.reshape(-1, vpp, 3), nrm.reshape(-1, vpp, 3), uv.reshape(-1, vpp, 2)
    lm_local = lm_local.reshape(-1, vpp, 2)
    lm_origin = np.repeat(np.array([lightmap_origin(p) for p in patches])[:, None, :], vpp, axis=1)
    local = grid_indices(tess)
    by_tex = {}
    for k, p in enumerate(patches):
        by_tex.setdefault(p.texture, []).append(k)
    prims = []
    for tex, ids in sorted(by_tex.items()):
        tris = (local[None] + (np.arange(len(ids)) * vpp)[:, None, None]).reshape(-1, 3)
        prims.append(gb.primitive(pos[ids].reshape(-1, 3), nrm[ids].reshape(-1, 3),
                                  uv[ids].reshape(-1, 2), tris, tex,
                                  (lm_local[ids].reshape(-1, 2), lm_origin[ids].reshape(-1, 2))))
    mesh_name = f"{name}_showoff_terrain" if showoff else f"{name}_terrain"
    node = gb.node(name=mesh_name, mesh=gb.mesh(mesh_name, prims))
    return node, len(patches) * len(local), len(patches)


def model_flags(level: pbd.PBD, model_index: int) -> int:
    """Model header +0x10 (u32): bit 0 = crowd model."""
    import struct
    return struct.unpack_from("<I", level.raw, level.offsets["models"] + level.model_pointers[model_index] + 0x10)[0]


def add_props(gb: GltfBuilder, level: pbd.PBD, name: str, lod: str):
    """Props: one node per instance. Every instance carries its own baked lighting block
    (3 lights + ambient in model space, .pbd instance bytes 64..191), so each instance gets its own glTF
    mesh per model object whose COLOR_0 is the per-vertex colour of pbdmodels.bake_vertex_colours; the
    geometry accessors are shared between the instances of a model. The material shows
    texture x COLOR_0 / 128 (Scripts/make_world_materials.py M_SSXProp)."""
    models = pbdmodels.load_models(level, lod)
    textures, _blocks = pbdmodels.load_materials(level)
    flags_of = pbdmodels.load_material_flags(level)

    object_prims = {}
    tri_count = 0
    two_sided_meshes = meshes = 0
    for m in models:
        crowd = model_flags(level, m.index) & 1
        for k, obj in enumerate(m.objects):
            prims = []
            for j, part in enumerate(obj.meshes):
                valid = 0 <= part.material < len(textures)
                tex = textures[part.material] if valid else -1
                flags = flags_of[part.material] if valid else 0
                extra = (np.tile(np.array([[float(j), 0.0]]), (len(part.positions), 1)),) if crowd else ()
                soft = not crowd and len(level.instances) > 0 and gb.alpha_class_of(tex) == ALPHA_SOFT
                prim = gb.primitive(part.positions, part.normals, part.uvs.copy(), part.triangles, tex, extra,
                                    two_sided=obj.two_sided, flags=flags, part="core" if soft else "")
                prims.append((prim, part.normals))
                if soft:
                    prims.append((gb.with_material(prim, tex, obj.two_sided, flags, "blend"), part.normals))
                tri_count += len(part.triangles)
                two_sided_meshes += bool(obj.two_sided)
                meshes += 1
            if prims:
                object_prims[(m.index, k)] = prims
    plain_mesh = {}

    def object_nodes(m, instance_tag, block=None):
        """Build the object hierarchy of one model instance; returns root node ids."""
        ids = {}
        for k, obj in enumerate(m.objects):
            fields = {"name": f"{instance_tag}_obj{k}"}
            prims = object_prims.get((m.index, k))
            if prims and block is not None:
                fields["mesh"] = gb.mesh(f"{instance_tag}_obj{k}",
                                         [gb.with_colours(p, pbdmodels.bake_vertex_colours(block, n)) for p, n in prims])
            elif prims:
                if (m.index, k) not in plain_mesh:
                    plain_mesh[(m.index, k)] = gb.mesh(f"{name}_model{m.index:03d}_obj{k}", [p for p, _ in prims])
                fields["mesh"] = plain_mesh[(m.index, k)]
            if obj.matrix is not None:
                fields["matrix"] = to_gltf_matrix(obj.matrix)
            ids[k] = gb.node(**fields)
        roots = []
        for k, obj in enumerate(m.objects):
            parent = obj.parent
            if 0 <= parent < len(m.objects) and parent != k:
                gb.doc["nodes"][ids[parent]].setdefault("children", []).append(ids[k])
            else:
                roots.append(ids[k])
        return roots

    instance_nodes = []
    for i, inst in enumerate(level.instances):
        if not 0 <= inst.model_id < len(models):
            continue
        m = models[inst.model_id]
        tag = f"{name}_inst{i:04d}"
        children = object_nodes(m, tag, pbdmodels.instance_light_block(level, i))
        instance_nodes.append(gb.node(name=f"inst{i:04d}_model{m.index:03d}",
                                      matrix=to_gltf_matrix(inst.matrix), children=children))

    if not instance_nodes:
        for m in models:
            instance_nodes += object_nodes(m, f"model{m.index:03d}")
    if not instance_nodes:
        return None, 0, 0
    root = gb.node(name=f"{name}_props", children=instance_nodes)
    print(f"  {name}: {two_sided_meshes} of {meshes} model meshes two-sided (LOD header +0x1C bit 0)")
    return root, len(instance_nodes), tri_count


RIDEABLE_SURFACES = {0, 1, 3, 4, 5}   # upward-facing snow / ice; 10 = walls, 9 / 17 = ceilings


def find_start(level: pbd.PBD):
    """Guess the race start: the highest gently-sloped rideable patch, facing downhill."""
    best = None
    for p in level.patches:
        if p.surface_type not in RIDEABLE_SURFACES:
            continue
        c = np.array(p.evaluate(0.5, 0.5))
        du = np.array(p.evaluate(0.6, 0.5)) - np.array(p.evaluate(0.4, 0.5))
        dv = np.array(p.evaluate(0.5, 0.6)) - np.array(p.evaluate(0.5, 0.4))
        n = np.cross(du, dv)
        n /= max(np.linalg.norm(n), 1e-9)
        if n[2] < 0.75 or (best is not None and c[2] <= best[0][2]):
            continue
        best = (c, n)
    if best is None:
        return None
    c, n = best
    downhill = -(np.array([0.0, 0.0, 1.0]) - n[2] * n)
    if np.linalg.norm(downhill) < 1e-3:
        downhill = np.array([1.0, 0.0, 0.0])
    return c + n * 150.0, downhill / np.linalg.norm(downhill)


def add_start_markers(gb: GltfBuilder, level: pbd.PBD, name: str):
    """Empty nodes that Unreal transforms with the track: start point and a point 10 m downhill."""
    start = find_start(level)
    if start is None:
        return []
    pos, fwd = start
    to_node = lambda p: (to_gltf_space(np.asarray(p)) * UNIT_SCALE).tolist()
    return [gb.node(name=f"{name}_PlayerStart", translation=to_node(pos)),
            gb.node(name=f"{name}_PlayerStartForward", translation=to_node(pos + fwd * 1000.0))]


def build(level_path: Path, tex_dir: Path, out_dir: Path, tess: int = None, lod: str = "high"):
    tess = DEFAULT_TESS if tess is None else tess
    level = pbd.load(level_path)
    name = level_path.stem
    out_dir.mkdir(parents=True, exist_ok=True)
    gb = GltfBuilder(tex_dir, out_dir)
    terrain, terrain_tris, n_patches = add_terrain(gb, level, name, tess)
    showoff, showoff_tris, n_showoff = add_terrain(gb, level, name, tess, showoff=True)
    props, n_inst, prop_tris = add_props(gb, level, name, lod)
    roots = [n for n in (terrain, showoff, props) if n is not None]
    if terrain is not None:
        roots += add_start_markers(gb, level, name)
    gb.write(name, roots)
    lm_ssh = next((c for c in (level_path.with_name(f"{name}_L.ssh"), level_path.with_name(f"{name[:6]}_L.ssh"))
                   if c.is_file()), None)
    if terrain is not None and lm_ssh is not None:
        print(f"{name}: lightmap atlas {write_lightmap_atlas(lm_ssh, out_dir / f'{name}_lightmap.png')} pages")
    print(f"{name}: terrain {n_patches} patches {terrain_tris:,} tris | ShowOff {n_showoff} patches "
          f"{showoff_tris:,} tris | props {n_inst} instances, "
          f"{prop_tris:,} unique tris | {len(gb.doc['materials'])} materials, {len(gb.blob) / 1e6:.1f} MB")


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("pbd", type=Path)
    ap.add_argument("textures", type=Path)
    ap.add_argument("out", type=Path)
    ap.add_argument("--tess", type=int, default=DEFAULT_TESS)
    ap.add_argument("--lod", choices=("high", "medium", "low"), default="high")
    args = ap.parse_args()
    build(args.pbd, args.textures, args.out, args.tess, args.lod)


if __name__ == "__main__":
    main()
