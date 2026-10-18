"""Convert SSX Tricky rider models (.mpf body + head) into skinned glTF, plus their boards.
Usage: python convert_characters.py [--only brodi,zoe] [--outfit 1] [--verify-only]
"""
# FUN_00201138 / 0x341998
import libpath  # noqa: F401,E402  (tools/lib on sys.path)
import argparse
import json
import shutil
import sys
from pathlib import Path

import numpy as np
from PIL import Image

import mpf

ROOT = Path(__file__).resolve().parent.parent
MDL_DIR = ROOT / "unpacked/CHAR/MDLPS2/data/char"
BOARD_MPF = ROOT / "unpacked/CHAR/BRDPS2/data/char/board.mpf"
TEX_DIR = ROOT / "converted/textures/CHAR/TEXPS2/data/char"
OUT_DIR = ROOT / "converted/characters"

UNIT_SCALE = 0.01
BASIS = np.array([[1, 0, 0], [0, 0, 1], [0, -1, 0]], dtype=np.float64)   # Z-up -> Y-up

CHARACTER_INDEX = {"eddie": 0, "kaori": 1, "luther": 2, "mac": 3, "moby": 4, "zoe": 5, "jp": 6,
                   "elise": 7, "psymon": 8, "seeiah": 9, "brodi": 10, "marisol": 11, "zz_mmm": 12}
TEXTURE_PREFIX = {"zz_mmm": "mmm"}
OUTFIT_SLOTS = {"suit", "boot", "bord"}   # numbered per outfit: <prefix><n>_<slot>
BOARD_MODELS = {"fr": "Fr", "bx": "Bx", "al": "Al", "fr_goofy": "FrGoofy", "bx_goofy": "BxGoofy", "al_goofy": "AlGoofy"}
LOD_TAGS = ("3000", "1500", "750")
MAX_INFLUENCES = 4


def to_gltf_vec(v):
    return np.asarray(v, dtype=np.float64) @ BASIS.T


def to_gltf_mat(m_game: np.ndarray) -> np.ndarray:
    """4x4 column-vector game matrix (cm) -> 4x4 column-vector glTF matrix (m)."""
    out = np.eye(4)
    out[:3, :3] = BASIS @ m_game[:3, :3] @ BASIS.T
    out[:3, 3] = BASIS @ m_game[:3, 3] * UNIT_SCALE
    return out


def mat_to_quat(r: np.ndarray):
    """3x3 rotation -> glTF quaternion [x, y, z, w]."""
    t = np.trace(r)
    if t > 0:
        s = np.sqrt(t + 1.0) * 2
        w, x, y, z = 0.25 * s, (r[2, 1] - r[1, 2]) / s, (r[0, 2] - r[2, 0]) / s, (r[1, 0] - r[0, 1]) / s
    elif r[0, 0] > r[1, 1] and r[0, 0] > r[2, 2]:
        s = np.sqrt(1.0 + r[0, 0] - r[1, 1] - r[2, 2]) * 2
        w, x, y, z = (r[2, 1] - r[1, 2]) / s, 0.25 * s, (r[0, 1] + r[1, 0]) / s, (r[0, 2] + r[2, 0]) / s
    elif r[1, 1] > r[2, 2]:
        s = np.sqrt(1.0 + r[1, 1] - r[0, 0] - r[2, 2]) * 2
        w, x, y, z = (r[0, 2] - r[2, 0]) / s, (r[0, 1] + r[1, 0]) / s, 0.25 * s, (r[1, 2] + r[2, 1]) / s
    else:
        s = np.sqrt(1.0 + r[2, 2] - r[0, 0] - r[1, 1]) * 2
        w, x, y, z = (r[1, 0] - r[0, 1]) / s, (r[0, 2] + r[2, 0]) / s, (r[1, 2] + r[2, 1]) / s, 0.25 * s
    q = np.array([x, y, z, w])
    return (q / np.linalg.norm(q)).tolist()


def quat_to_mat(q) -> np.ndarray:
    x, y, z, w = q
    return np.array([[1 - 2 * (y * y + z * z), 2 * (x * y - z * w), 2 * (x * z + y * w)],
                     [2 * (x * y + z * w), 1 - 2 * (x * x + z * z), 2 * (y * z - x * w)],
                     [2 * (x * z - y * w), 2 * (y * z + x * w), 1 - 2 * (x * x + y * y)]])


class Gltf:
    def __init__(self, out_dir: Path, name: str):
        self.out_dir, self.name = out_dir, name
        self.blob = bytearray()
        self.doc = {k: [] for k in ("nodes", "meshes", "materials", "textures", "images", "accessors",
                                    "bufferViews", "skins")}
        self.material_ids = {}

    def accessor(self, arr: np.ndarray, gl_type: str, component: int, target=None, minmax=False) -> int:
        while len(self.blob) % 16:
            self.blob.append(0)
        view = {"buffer": 0, "byteOffset": len(self.blob), "byteLength": arr.nbytes}
        if target:
            view["target"] = target
        self.doc["bufferViews"].append(view)
        self.blob.extend(arr.tobytes())
        acc = {"bufferView": len(self.doc["bufferViews"]) - 1, "componentType": component,
               "count": int(arr.shape[0]), "type": gl_type}
        if minmax:
            acc["min"] = arr.min(axis=0).tolist()
            acc["max"] = arr.max(axis=0).tolist()
        self.doc["accessors"].append(acc)
        return len(self.doc["accessors"]) - 1

    def material(self, name: str, png) -> int:
        if name in self.material_ids:
            return self.material_ids[name]
        mat = {"name": name, "pbrMetallicRoughness": {"metallicFactor": 0.0, "roughnessFactor": 0.85}}
        if png is not None:
            dest = self.out_dir / "textures" / f"{name}.png"
            dest.parent.mkdir(parents=True, exist_ok=True)
            shutil.copyfile(png, dest)
            self.doc["images"].append({"uri": f"textures/{dest.name}"})
            self.doc["textures"].append({"source": len(self.doc["images"]) - 1, "sampler": 0})
            mat["pbrMetallicRoughness"]["baseColorTexture"] = {"index": len(self.doc["textures"]) - 1}
            img = Image.open(png)
            if img.mode == "RGBA" and img.getextrema()[3][0] < 128:
                mat["alphaMode"] = "MASK"
                mat["doubleSided"] = True
        self.doc["materials"].append(mat)
        self.material_ids[name] = len(self.doc["materials"]) - 1
        return self.material_ids[name]

    def node(self, **fields) -> int:
        self.doc["nodes"].append(fields)
        return len(self.doc["nodes"]) - 1

    def write(self, roots: list, extra=None) -> Path:
        (self.out_dir / f"{self.name}.bin").write_bytes(self.blob)
        doc = {"asset": {"version": "2.0", "generator": "SSXTrickyRebuild convert_characters"},
               "scene": 0, "scenes": [{"nodes": roots}],
               **{k: v for k, v in self.doc.items() if v},
               "samplers": [{"magFilter": 9729, "minFilter": 9987, "wrapS": 10497, "wrapT": 10497}],
               "buffers": [{"uri": f"{self.name}.bin", "byteLength": len(self.blob)}]}
        if extra:
            doc.update(extra)
        path = self.out_dir / f"{self.name}.gltf"
        path.write_text(json.dumps(doc))
        return path


def texture_png(prefix: str, slot: str, outfit: int):
    tex = f"{prefix}{outfit}_{slot}" if slot in OUTFIT_SLOTS else f"{prefix}_{slot}"
    hits = sorted((TEX_DIR / tex).glob(f"*_{slot}.png")) or sorted((TEX_DIR / tex).glob("*.png"))
    return hits[0] if hits else None


def pick_parts(models: list) -> list:
    """Highest LOD present in the file, shadow meshes excluded."""
    for tag in LOD_TAGS:
        parts = [m for m in models if tag in m.name and not m.is_shadow]
        if parts:
            return parts
    return [m for m in models if not m.is_shadow]


class Skeleton:
    """Merged joint list: joint 0 = added root, then the game bones keyed by (file id, index)."""

    def __init__(self, parts: list, lift: float):
        self.names, self.parents, self.local = ["root"], [-1], [np.eye(4)]
        self.key = {}
        pending = []
        for m in parts:
            for i, b in enumerate(m.bones):
                if (m.file_id, i) in self.key:
                    continue
                self.key[(m.file_id, i)] = len(self.names)
                name = b.name or f"bone_{m.file_id}_{i}"
                while name in self.names:
                    name += "_"
                self.names.append(name)
                self.parents.append(None)
                self.local.append(mpf.local_matrix(b))
                pending.append((len(self.names) - 1, b))
        for j, b in pending:
            if b.parent_index < 0:
                self.parents[j] = 0
                self.local[j] = self.local[j].copy()
                self.local[j][2, 3] += lift   # root bones are relative to the soles
            else:
                p = self.key.get((b.parent_file, b.parent_index))
                if p is None:
                    raise ValueError(f"bone {b.name}: parent ({b.parent_file},{b.parent_index}) missing")
                self.parents[j] = p
        self.world = [None] * len(self.names)
        for j in self._order():
            p = self.parents[j]
            self.world[j] = self.local[j] if p < 0 else self.world[p] @ self.local[j]

    def _order(self):
        done, order = set(), []
        while len(order) < len(self.names):
            for j, p in enumerate(self.parents):
                if j not in done and (p < 0 or p in done):
                    done.add(j)
                    order.append(j)
        return order

    def depth(self) -> int:
        def d(j):
            return 0 if self.parents[j] < 0 else 1 + d(self.parents[j])
        return max(d(j) for j in range(len(self.names)))


def material_key(slot: str, env_pass: bool, gloss: int = 0) -> str:
    """Material suffix: env-pass chunks (mpf.env_pass_flags) get "env_<slot>", face morph chunks "<slot>".
    Env chunks with a gloss (mpf.Material.gloss != 0) get "env_s<FIX>_<slot>" (gloss > 0, `<trk>s` map) or
    "env_e<FIX>_<slot>" (gloss < 0, `<trk>e`), FIX = |gloss| & 0xFF. All end in _<slot>, which SSXCharacterAppearance
    swaps the outfit textures by."""
    if not env_pass:
        return slot
    fix = abs(gloss) & 0xFF
    if fix == 0:
        return f"{ENV_TAG}{slot}"
    return f"{ENV_TAG}{'s' if gloss > 0 else 'e'}{fix}_{slot}"


def key_slot(key: str) -> str:
    return key.rsplit("_", 1)[-1]


ENV_TAG = "env_"
# 0x1DF478 / 0x33EDF0
ENV_TEXTURE = ROOT / "converted/textures/TEXTURES/PARTICLE/036_envr.png"
# FUN_0025EDF0 / 0x25EDF0 / 0x346F60
SPECMAP_DIR = ROOT / "converted/textures/TEXTURES/SPECMAP"
SPECMAP_STEMS = ("gari", "snow", "elys", "mesa", "merq", "aloh", "pipe", "untr", "toky", "tric", "alas", "fend")
SPEC_FALLBACK = ROOT / "converted/textures/TEXTURES/PARTICLE/035_spec.png"


def export_specmaps() -> dict:
    """Copy the 24 specmap images and the 'spec' / 'envr' fallbacks to converted/characters/specmap/<name>.png."""
    out = OUT_DIR / "specmap"
    out.mkdir(parents=True, exist_ok=True)
    found = {}
    for k, stem in enumerate(SPECMAP_STEMS):
        for j, suffix in enumerate("se"):
            name = stem + suffix
            src = SPECMAP_DIR / f"{2 * k + j:03d}_{name}.png"
            if src.exists():
                shutil.copyfile(src, out / f"{name}.png")
                found[name] = True
            else:
                found[name] = False
    for name, src in (("spec", SPEC_FALLBACK), ("envr", ENV_TEXTURE)):
        if src.exists():
            shutil.copyfile(src, out / f"{name}.png")
        found[name] = src.exists()
    return found


FACE_MORPH_CM = 2560.0 / 32768.0   # cm per s8 delta step at weight 1


def gather_geometry(parts: list, lift: float):
    """Flatten every chunk of the chosen parts. Returns dict of per-vertex arrays + per-material tris, and the face
    morph keys as (start vertex, int8 deltas[keys, n, 3]) runs."""
    pos, nrm, uv, inf = [], [], [], []
    tris_by_mat = {}
    morphs = []
    base = 0
    for m in parts:
        for g in m.groups:
            if g.type == mpf.GROUP_SHADOW:
                continue
            valid = 0 <= g.material < len(m.materials)
            slot = m.materials[g.material].main if valid else "suit"
            gloss = m.materials[g.material].gloss if valid else 0
            for blk in g.blocks:
                palette = m.weight_lists[blk.palette]
                if blk.morph_deltas is not None:
                    morphs.append((base, blk.morph_deltas))
                for c in blk.chunks:
                    tris = mpf.strip_triangles(c.strips, c.positions, c.normals)
                    tris_by_mat.setdefault(material_key(slot, c.env_pass, gloss), []).append(tris + base)
                    pos.append(c.positions + np.array([0.0, 0.0, lift]))
                    nrm.append(c.normals)
                    uv.append(c.uvs)
                    for s in c.weight_slots:
                        inf.append(m.weight_sets[palette[s]])
                    base += len(c.positions)
    return (np.concatenate(pos), np.concatenate(nrm), np.concatenate(uv), inf,
            {k: np.concatenate(v) for k, v in tris_by_mat.items()}, morphs)


def morph_targets(morphs: list, n_verts: int) -> np.ndarray:
    """Face morph keys as per-vertex game-space offsets (cm) at weight 1, shape (keys, n_verts, 3). The game adds
    delta / 32768 x (weight x rider scale x 2560) before skinning, so the Unreal weight is weight x rider scale."""
    if not morphs:
        return np.zeros((0, n_verts, 3))
    keys = max(d.shape[0] for _, d in morphs)
    out = np.zeros((keys, n_verts, 3))
    for start, d in morphs:
        out[:d.shape[0], start:start + d.shape[1]] = d.astype(np.float64) * FACE_MORPH_CM
    return out


def build_weights(influences, skel: Skeleton):
    n = len(influences)
    joints = np.zeros((n, MAX_INFLUENCES), dtype=np.uint16)
    weights = np.zeros((n, MAX_INFLUENCES), dtype=np.float32)
    unresolved = 0
    for v, entries in enumerate(influences):
        entries = sorted(entries, key=lambda e: -e[2])[:MAX_INFLUENCES]
        for k, (bone, fid, w) in enumerate(entries):
            j = skel.key.get((fid, bone))
            if j is None:
                unresolved += 1
                j = 0
            joints[v, k] = j
            weights[v, k] = w
    sums = weights.sum(axis=1, keepdims=True)
    raw_sums = sums[:, 0].copy()
    weights = weights / np.where(sums > 0, sums, 1.0)
    return joints, weights, raw_sums, unresolved


def convert_character(name: str, outfit: int, board_models: dict) -> dict:
    prefix = TEXTURE_PREFIX.get(name, name)
    parts = pick_parts(mpf.load(MDL_DIR / f"{name}_body.mpf")) + pick_parts(mpf.load(MDL_DIR / f"{name}_head.mpf"))
    out = OUT_DIR / name
    if out.exists():
        shutil.rmtree(out)
    out.mkdir(parents=True)

    raw_min = min(c.positions[:, 2].min() for m in parts for g in m.groups for b in g.blocks for c in b.chunks)
    lift = -float(raw_min)
    skel = Skeleton(parts, lift)
    pos, nrm, uv, influences, tris_by_mat, morphs = gather_geometry(parts, lift)
    targets = morph_targets(morphs, len(pos))
    joints, weights, raw_sums, unresolved = build_weights(influences, skel)

    gl = Gltf(out, name)
    attrs = {
        "POSITION": gl.accessor((to_gltf_vec(pos) * UNIT_SCALE).astype(np.float32), "VEC3", 5126, 34962, True),
        "NORMAL": gl.accessor(to_gltf_vec(nrm).astype(np.float32), "VEC3", 5126, 34962),
        "TEXCOORD_0": gl.accessor(uv.astype(np.float32), "VEC2", 5126, 34962),
        "JOINTS_0": gl.accessor(joints, "VEC4", 5123, 34962),
        "WEIGHTS_0": gl.accessor(weights, "VEC4", 5126, 34962),
    }
    target_acc = [{"POSITION": gl.accessor((to_gltf_vec(t) * UNIT_SCALE).astype(np.float32), "VEC3", 5126, 34962, True)}
                  for t in targets]
    tex_report = {}
    prims = []
    for key, tris in sorted(tris_by_mat.items()):
        png = texture_png(prefix, key_slot(key), outfit)
        tex_report[key] = str(png.relative_to(ROOT)) if png else None
        prim = {"attributes": attrs, "material": gl.material(f"{name}_{key}", png),
                "indices": gl.accessor(tris.reshape(-1).astype(np.uint32), "SCALAR", 5125, 34963)}
        if target_acc:
            prim["targets"] = target_acc
        prims.append(prim)
    mesh = {"name": name, "primitives": prims}
    if target_acc:
        mesh["extras"] = {"targetNames": [f"ssx_face_{k:02d}" for k in range(len(target_acc))]}
    gl.doc["meshes"].append(mesh)

    joint_nodes = []
    for j in range(len(skel.names)):
        lm = to_gltf_mat(skel.local[j])
        fields = {"name": skel.names[j], "translation": lm[:3, 3].tolist(), "rotation": mat_to_quat(lm[:3, :3])}
        joint_nodes.append(gl.node(**fields))
    for j, p in enumerate(skel.parents):
        if p >= 0:
            gl.doc["nodes"][joint_nodes[p]].setdefault("children", []).append(joint_nodes[j])
    ibm = np.stack([np.linalg.inv(to_gltf_mat(w)).T for w in skel.world]).astype(np.float32)   # column-major
    gl.doc["skins"].append({"name": f"{name}_skin", "skeleton": joint_nodes[0], "joints": joint_nodes,
                            "inverseBindMatrices": gl.accessor(ibm.reshape(-1, 16), "MAT4", 5126)})
    mesh_node = gl.node(name=f"{name}_mesh", mesh=0, skin=0)
    path = gl.write([mesh_node, joint_nodes[0]])

    boards = {}
    for cls, model in board_models.items():
        boards[cls] = convert_board(name, prefix, outfit, cls, model, out)

    env_parts = {m.name: [sum(c.env_pass for g in m.groups if g.type != mpf.GROUP_SHADOW for b in g.blocks for c in b.chunks),
                          sum(1 for g in m.groups if g.type != mpf.GROUP_SHADOW for b in g.blocks for c in b.chunks)]
                 for m in parts}
    env_png = None
    if ENV_TEXTURE.exists():
        env_png = OUT_DIR / "envr.png"
        shutil.copyfile(ENV_TEXTURE, env_png)
    # FUN_00205670
    gloss_groups = {}
    for m in parts:
        rows = []
        for g in m.groups:
            if g.type == mpf.GROUP_SHADOW:
                continue
            mat = m.materials[g.material]
            chunks = [c for b in g.blocks for c in b.chunks]
            env = sum(c.env_pass for c in chunks)
            rows.append([g.material, float(mat.floats[0]), bool(mat.textures[3]), bool(mat.textures[4]), mat.gloss, env,
                         len(chunks), f"{name}_{material_key(mat.main, env > 0, mat.gloss)}"])
        gloss_groups[m.name] = rows
    info = {"name": name, "character_index": CHARACTER_INDEX.get(name, -1), "outfit": outfit,
            "parts": [m.name for m in parts], "lift_cm": lift, "gltf": str(path.relative_to(ROOT)),
            "bones": skel.names, "textures": tex_report, "boards": boards,
            "env_pass_materials": sorted(f"{name}_{k}" for k in tris_by_mat if k.startswith(ENV_TAG)),
            "env_pass_chunks": env_parts,
            "env_texture": str(env_png.relative_to(ROOT)) if env_png else None,
            "gloss_groups": gloss_groups,
            "face_morph_keys": int(len(targets)),
            "face_morph_max_cm": float(np.abs(targets).max()) if len(targets) else 0.0,
            "unresolved_weights": unresolved}
    (out / f"{name}.json").write_text(json.dumps(info, indent=1))
    (out / f"{name}_bones.txt").write_text("\n".join(skel.names) + "\n")
    return info


def convert_board(name, prefix, outfit, cls, model, out: Path) -> dict:
    pos, nrm, uv, tris, base = [], [], [], [], 0
    env, gloss = set(), set()
    for g in model.groups:
        gloss.add(model.materials[g.material].gloss)
        for blk in g.blocks:
            for c in blk.chunks:
                env.add(c.env_pass)
                tris.append(mpf.strip_triangles(c.strips, c.positions, c.normals) + base)
                pos.append(c.positions)
                nrm.append(c.normals)
                uv.append(c.uvs)
                base += len(c.positions)
    pos, nrm, uv, tris = map(np.concatenate, (pos, nrm, uv, tris))
    pos = pos - np.array([0.0, 0.0, pos[:, 2].min()])   # bottom at z = 0
    gl = Gltf(out, f"{name}_board_{cls}")
    png = texture_png(prefix, "bord", outfit)
    prim = {"attributes": {
        "POSITION": gl.accessor((to_gltf_vec(pos) * UNIT_SCALE).astype(np.float32), "VEC3", 5126, 34962, True),
        "NORMAL": gl.accessor(to_gltf_vec(nrm).astype(np.float32), "VEC3", 5126, 34962),
        "TEXCOORD_0": gl.accessor(uv.astype(np.float32), "VEC2", 5126, 34962)},
        "indices": gl.accessor(tris.reshape(-1).astype(np.uint32), "SCALAR", 5125, 34963),
        "material": gl.material(f"{name}_{material_key('board', True in env, min(gloss))}", png)}
    if len(env) != 1:
        raise ValueError(f"board {model.name}: chunks with and without the environment pass")
    if len(gloss) != 1:
        raise ValueError(f"board {model.name}: groups with different gloss values {sorted(gloss)}")
    gl.doc["meshes"].append({"name": f"{name}_board_{cls}", "primitives": [prim]})
    gl.write([gl.node(name=f"{name}_board_{cls}", mesh=0)])
    # FUN_00207C30
    return {"model": model.name, "triangles": int(len(tris)), "top_cm": float(pos[:, 2].max()), "env_pass": True in env,
            "gloss": min(gloss), "gloss_float": float(model.materials[model.groups[0].material].floats[0]),
            "material": f"{name}_{material_key('board', True in env, min(gloss))}",
            "length_cm": float(np.ptp(pos[:, 0])), "texture": str(png.relative_to(ROOT)) if png else None}


def read_accessor(doc, blob, idx):
    acc = doc["accessors"][idx]
    view = doc["bufferViews"][acc["bufferView"]]
    dtype = {5126: np.float32, 5125: np.uint32, 5123: np.uint16, 5121: np.uint8}[acc["componentType"]]
    width = {"SCALAR": 1, "VEC2": 2, "VEC3": 3, "VEC4": 4, "MAT4": 16}[acc["type"]]
    arr = np.frombuffer(blob, dtype, acc["count"] * width, view["byteOffset"] + acc.get("byteOffset", 0))
    return arr.reshape(acc["count"], width) if width > 1 else arr


def node_local(n) -> np.ndarray:
    m = np.eye(4)
    if "matrix" in n:
        return np.array(n["matrix"]).reshape(4, 4).T
    if "rotation" in n:
        m[:3, :3] = quat_to_mat(n["rotation"])
    if "translation" in n:
        m[:3, 3] = n["translation"]
    return m


def verify(name: str) -> dict:
    out = OUT_DIR / name
    doc = json.loads((out / f"{name}.gltf").read_text())
    blob = (out / f"{name}.bin").read_bytes()
    info = json.loads((out / f"{name}.json").read_text())
    nodes = doc["nodes"]
    skin = doc["skins"][0]
    joints = skin["joints"]
    parent = {}
    for i, n in enumerate(nodes):
        for c in n.get("children", []):
            parent[c] = i
    world = {}

    def w(i):
        if i not in world:
            world[i] = (w(parent[i]) if i in parent else np.eye(4)) @ node_local(nodes[i])
        return world[i]

    def depth(i):
        return 0 if i not in parent else 1 + depth(parent[i])

    prim = doc["meshes"][0]["primitives"][0]
    a = prim["attributes"]
    P = read_accessor(doc, blob, a["POSITION"]).astype(np.float64)
    N = read_accessor(doc, blob, a["NORMAL"]).astype(np.float64)
    J = read_accessor(doc, blob, a["JOINTS_0"]).astype(np.int64)
    W = read_accessor(doc, blob, a["WEIGHTS_0"]).astype(np.float64)
    IBM = read_accessor(doc, blob, skin["inverseBindMatrices"]).reshape(-1, 4, 4).transpose(0, 2, 1)
    tris = sum(doc["accessors"][p["indices"]]["count"] // 3 for p in doc["meshes"][0]["primitives"])
    max_index = max(int(read_accessor(doc, blob, p["indices"]).max()) for p in doc["meshes"][0]["primitives"])

    skin_m = np.stack([w(j) @ IBM[k] for k, j in enumerate(joints)])
    Ph = np.c_[P, np.ones(len(P))]
    skinned = np.zeros_like(P)
    for k in range(J.shape[1]):
        skinned += W[:, k:k + 1] * np.einsum("nij,nj->ni", skin_m[J[:, k]], Ph)[:, :3]
    skin_err_cm = float(np.abs(skinned - P).max() / UNIT_SCALE)

    jpos = np.array([w(j)[:3, 3] for j in joints])
    children = {k: [c for c, j in enumerate(joints) if parent.get(j) == joints[k]] for k in range(len(joints))}
    dom = J[np.arange(len(J)), W.argmax(axis=1)]
    seg = []
    for k in range(1, len(joints)):
        v = P[dom == k]
        if len(v) < 8:
            continue
        ends = [jpos[c] for c in children[k]] or [jpos[k]]
        dists = []
        for e in ends:
            d = e - jpos[k]
            t = np.clip(((v - jpos[k]) @ d) / max(d @ d, 1e-12), 0, 1) if d @ d > 1e-12 else np.zeros(len(v))
            dists.append(np.linalg.norm(v - (jpos[k] + t[:, None] * d), axis=1))
        seg.append(np.median(np.min(dists, axis=0)))

    game = P @ BASIS / UNIT_SCALE   # back to game space (cm, Z-up)
    tex_ok = all(info["textures"].values()) and all(b["texture"] for b in info["boards"].values())
    return {
        "name": name, "bones": len(joints), "depth": max(depth(j) for j in joints) - depth(joints[0]),
        "joints_valid": bool(J.min() >= 0 and J.max() < len(joints) and info["unresolved_weights"] == 0),
        "weight_err": float(np.abs(W.sum(axis=1) - 1).max()),
        "verts": len(P), "tris": int(tris), "index_ok": max_index < len(P),
        "height_cm": float(np.ptp(game[:, 2])), "width_cm": float(np.ptp(game[:, 0])),
        "nan": bool(np.isnan(P).any() or np.isnan(N).any() or np.isnan(W).any() or np.isnan(IBM).any()),
        "skin_err_cm": skin_err_cm, "bone_seg_cm": float(np.median(seg)) / UNIT_SCALE if seg else float("nan"),
        "textures": "ok" if tex_ok else "MISSING",
        "board_tris": "/".join(str(b["triangles"]) for b in info["boards"].values()),
    }


def print_table(rows):
    cols = [("name", "{:8s}"), ("bones", "{:5d}"), ("depth", "{:5d}"), ("joints_valid", "{!s:6}"),
            ("weight_err", "{:8.1e}"), ("verts", "{:6d}"), ("tris", "{:6d}"), ("height_cm", "{:6.1f}"),
            ("width_cm", "{:6.1f}"), ("nan", "{!s:5}"), ("skin_err_cm", "{:8.1e}"), ("bone_seg_cm", "{:6.2f}"),
            ("textures", "{:7s}"), ("board_tris", "{:11s}")]
    heads = ["name", "bones", "depth", "jnt_ok", "w_err", "verts", "tris", "height", "width", "NaN",
             "skin_err", "seg_cm", "tex", "board_tris"]
    widths = [max(len(h), len(f.format(rows[0][c]))) if rows else len(h) for h, (c, f) in zip(heads, cols)]
    print("  ".join(h.rjust(wd) for h, wd in zip(heads, widths)))
    for r in rows:
        print("  ".join(f.format(r[c]).rjust(wd) for (c, f), wd in zip(cols, widths)))


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--only", default="")
    ap.add_argument("--outfit", type=int, default=1)
    ap.add_argument("--verify-only", action="store_true")
    args = ap.parse_args()
    names = sorted(p.name[:-9] for p in MDL_DIR.glob("*_body.mpf"))
    if args.only:
        names = [n for n in names if n in args.only.split(",")]
    board_file = {m.name: m for m in mpf.load(BOARD_MPF)}
    board_models = {cls: board_file[n] for cls, n in BOARD_MODELS.items()}
    rows, failed = [], []
    if not args.verify_only:
        specmaps = export_specmaps()
        missing = [k for k, ok in specmaps.items() if not ok]
        print(f"specmaps: {len(specmaps) - len(missing)} copied to {OUT_DIR / 'specmap'}"
              + (f", MISSING {missing} (run sshtex.py on SPECMAP.SSH / PARTICLE.SSH)" if missing else ""))
    for name in names:
        try:
            if not args.verify_only:
                convert_character(name, args.outfit, board_models)
            rows.append(verify(name))
        except Exception as e:
            failed.append((name, repr(e)))
    print_table(rows)
    bad = [r["name"] for r in rows if not (r["joints_valid"] and r["weight_err"] < 1e-3 and not r["nan"]
                                           and r["index_ok"] and r["skin_err_cm"] < 0.01
                                           and 150 <= r["height_cm"] <= 210 and r["textures"] == "ok")]
    for name, err in failed:
        print(f"FAILED {name}: {err}")
    print(f"{len(rows)} converted, {len(bad)} with check failures {bad}, {len(failed)} errors")
    sys.exit(1 if bad or failed else 0)


if __name__ == "__main__":
    main()
