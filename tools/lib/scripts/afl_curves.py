"""SSX Tricky `.afl` per-bone animation curves: decoder, pose sampler, clip exporter.

Decoded from the EE code:
  0x157ED8 track evaluator (piecewise dispatcher), 0x158030 segment evaluator,
  0x159910 track binder (bone order -> track index), 0x202B00 pose sampler (layers, mirror, yaw, blend).

Usage:
    python scripts/afl_curves.py dump <file.afl> <record>          tracks of one animation
    python scripts/afl_curves.py export <out_dir>                  JSON clips per rider
"""
from __future__ import annotations

import json
import math
import os
import struct
import sys

import numpy as np

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import afl  # noqa: E402

REBUILD = afl.REPO
MDL_DIR = os.path.join(REBUILD, "unpacked", "CHAR", "MDLPS2", "data", "char")



def f24(b: bytes, o: int) -> float:
    """24-bit float: the 3 stored bytes are bits 8..31 of an IEEE float; the game sets the low byte to 0x80."""
    return struct.unpack("<f", bytes((0x80, b[o], b[o + 1], b[o + 2])))[0]


def _seg_size(typ: int, n: int) -> int:
    return {0: 5, 1: 8, 2: 11, 3: 14, 4: 4 * n + 2, 6: n + 8, 7: 2 * n + 8}[typ]


def parse_track(d: bytes, o: int) -> list:
    """Decode a track at byte offset o into a list of segments (type, n_frames, payload).

    Header u16: bits 0-3 type, bits 4-15 frame count n.  Type 5 = piecewise: n = number of segments,
    each with its own u16 header; consecutive segments share their boundary frame
    (segment k covers local t in [0, n_k - 1])."""
    h = d[o] | d[o + 1] << 8
    typ, n = h & 15, h >> 4
    if typ != 5:
        return [_parse_seg(d, o)]
    segs, p = [], o + 2
    for _ in range(n):
        s = _parse_seg(d, p)
        segs.append(s)
        p += _seg_size(s[0], s[1])
    return segs


def _parse_seg(d: bytes, o: int):
    h = d[o] | d[o + 1] << 8
    typ, n = h & 15, h >> 4
    p = o + 2
    if typ <= 3:                                   # polynomial, highest power first, t in local frames
        return (typ, n, [f24(d, p + 3 * k) for k in range(typ + 1)])
    if typ == 4:                                   # raw float32 per frame, linear interpolation
        return (typ, n, list(struct.unpack_from("<%df" % n, d, p)))
    if typ in (6, 7):                              # base + scale * u8/u16 sample, linear interpolation
        base, scale = f24(d, p), f24(d, p + 3)
        if typ == 6:
            q = list(d[p + 6:p + 6 + n])
        else:
            q = list(struct.unpack_from("<%dH" % n, d, p + 6))
        return (typ, n, (base, scale, q))
    raise ValueError(f"unknown segment type {typ} at {o:#x}")


def _eval_seg(seg, t: float) -> float:
    typ, n, c = seg
    if typ <= 3:
        v = 0.0
        for k in c:
            v = v * t + k
        return v
    i = int(t)                                     # cvt.w.s truncates
    fr = t - i
    if typ == 4:
        return c[i] if fr == 0.0 else c[i] + fr * (c[i + 1] - c[i])
    base, scale, q = c
    a = float(q[i])
    if fr != 0.0:
        a = a + fr * (q[i + 1] - a)
    return base + scale * a


def eval_track(segs: list, t: float) -> float:
    """0x157ED8: piecewise lookup; t beyond the last segment returns 0.0 (game behaviour)."""
    if len(segs) == 1:
        return _eval_seg(segs[0], t)
    for s in segs:
        if t <= s[1] - 1:
            return _eval_seg(s, t)
        t -= s[1] - 1
    return 0.0




class AflFile:
    def __init__(self, path: str):
        self.path = path
        self.name = os.path.basename(path)
        d = self.d = open(path, "rb").read()
        magic, n, self.off_tracks, self.off_data = struct.unpack_from("<HHII", d, 0)
        assert magic == afl.MAGIC
        self.n_records = n
        self.anims = {}                            # record index -> Animation
        i = 0
        while i < n:
            o = 0x0C + i * 36
            h, _z, _ff, n_sub, nf = struct.unpack_from("<IIBBH", d, o)
            mk = struct.unpack_from("<4H", d, o + 12)
            blocks = []
            for k in range(n_sub):
                if i + 1 + k >= n:
                    break
                so = 0x0C + (i + 1 + k) * 36
                _0, toff, chan, snf = struct.unpack_from("<IIHH", d, so)
                if _0 != 0:
                    # a named record inside the count: the next animation (some scene banks, e.g. stg_com_4.afl
                    # record 0 counts 7 blocks but has 6); it is bound by its own name hash, not a block
                    break
                blocks.append(CurveBlock(self, i + 1 + k, toff, chan, snf))
            a = Animation(self, i, h, nf, blocks)
            a.markers = tuple(None if m == 0xFFFF else m for m in mk)
            self.anims[i] = a
            i += 1 + len(blocks)

    def track_offset(self, idx: int) -> int:
        return self.off_data + struct.unpack_from("<I", self.d, self.off_tracks + 4 * idx)[0]


class CurveBlock:
    """One curve record = the tracks of one model part (channel 0 = body skeleton, 1 = board)."""

    def __init__(self, f: AflFile, rec: int, toff: int, chan: int, nf: int):
        self.f, self.rec, self.first, self.chan, self.n_frames = f, rec, toff, chan, nf
        self._cache = {}

    def track(self, k: int) -> list:
        if k not in self._cache:
            self._cache[k] = parse_track(self.f.d, self.f.track_offset(self.first + k))
        return self._cache[k]


class Animation:
    def __init__(self, f, rec, name_hash, n_frames, blocks):
        self.file, self.index, self.name_hash, self.n_frames, self.blocks = f, rec, name_hash, n_frames, blocks
        self.name = "?"
        self.markers = ()


_NAMES = None


def anim_names() -> dict:
    """name hash -> name from the exe name table (.data 0x3242A0)."""
    global _NAMES
    if _NAMES is None:
        b = afl.Bank.__new__(afl.Bank)
        b.exe = open(afl.EXE, "rb").read()
        _NAMES = {afl.elf23(n): n for n in b._name_table()}
    return _NAMES


def load_afl(name: str) -> AflFile:
    f = AflFile(os.path.join(afl.AFL_DIR, name))
    names = anim_names()
    for a in f.anims.values():
        a.name = names.get(a.name_hash, f"hash_{a.name_hash:08x}")
    return f




def load_skeleton(rider: str, part: str = "body"):
    """Bones of <rider>_body.mpf (19) or board.mpf (1) via tools/mpf.py."""
    sys.path.insert(0, os.path.join(REBUILD, "tools"))
    import mpf
    path = os.path.join(MDL_DIR, "board.mpf" if part == "board" else f"{rider}_{part}.mpf")
    return mpf.load(path)[0].bones


def bone_track_index(bones) -> list:
    """0x159910: tracks are laid out in skeleton order; flags (bone +0x14) bit 0 = 3 translation
    tracks, bit 1 = 3 rotation tracks (x, y, z). Returns [(first_t, first_r)] (None when absent)."""
    out, k = [], 0
    for b in bones:
        t = r = None
        if b.unknown & 1:
            t, k = k, k + 3
        if b.unknown & 2:
            r, k = k, k + 3
        out.append((t, r))
    return out


def sample_block(block: CurveBlock, bones, frame: float, mirror: bool = False, yaw: float = 0.0,
                 trans_scale: float = 1.0):
    """0x202B00 for one layer at weight 1. Returns (euler[n,3], trans[n,3]): the local transform
    inputs per bone (radians / game units = cm), what the game stores at pose+0x000 / pose+0x108.

    Non-mirrored: euler = tracks; translation = tracks * trans_scale (animated bones) or the bind
    translation. Root bone (parent -1): translation rotated by `yaw` about z, euler z += yaw.
    mirror (pose+0x330): read the tracks of the partner bone (bone id, +0x16), then
    euler = extra[0:3] + extra[3:6] * euler and translation *= extra[6:9] (animated bones only)."""
    idx = bone_track_index(bones)
    n = len(bones)
    eul = np.zeros((n, 3))
    tr = np.zeros((n, 3))
    for i, b in enumerate(bones):
        src = b.bone_id if mirror else i
        ti, ri = idx[src]
        ex = b.extra
        if b.unknown & 1:
            tr[i] = [eval_track(block.track(ti + a), frame) for a in range(3)]
            tr[i] *= trans_scale
            if b.parent_file == -1:
                # the root turned by the layer yaw: x' = c x + s y, y' = -s x + c y (0x202B00; the sense shows only at
                # +-pi/2, the rail clips: REP0001 f1031)
                c, s = math.cos(yaw), math.sin(yaw)
                x, y = tr[i, 0], tr[i, 1]
                tr[i, 0], tr[i, 1] = c * x + s * y, -s * x + c * y
            if mirror:
                tr[i] *= ex[6:9]
        else:
            tr[i] = b.translation
        if b.unknown & 2:
            eul[i] = [eval_track(block.track(ri + a), frame) for a in range(3)]
            if b.parent_file == -1:
                eul[i, 2] += yaw
            if mirror:
                eul[i] = np.array(ex[0:3]) + np.array(ex[3:6]) * eul[i]
        else:
            eul[i] = b.rotation
    return eul, tr


def euler_matrix(e) -> np.ndarray:
    """Local rotation = Rz(-z) Ry(-y) Rx(-x) (column vectors), as tools/mpf.py."""
    x, y, z = -e[0], -e[1], -e[2]
    cx, sx, cy, sy, cz, sz = math.cos(x), math.sin(x), math.cos(y), math.sin(y), math.cos(z), math.sin(z)
    rx = np.array([[1, 0, 0], [0, cx, -sx], [0, sx, cx]])
    ry = np.array([[cy, 0, sy], [0, 1, 0], [-sy, 0, cy]])
    rz = np.array([[cz, -sz, 0], [sz, cz, 0], [0, 0, 1]])
    return rz @ ry @ rx


def world_matrices(bones, eul, tr) -> list:
    out = []
    for i, b in enumerate(bones):
        m = np.eye(4)
        m[:3, :3] = euler_matrix(eul[i])
        m[:3, 3] = tr[i]
        out.append(m if b.parent_file == -1 else out[b.parent_index] @ m)
    return out


def sample_pose(anim: Animation, frame: float, bones, block: int = 0, **kw) -> dict:
    """Per-bone local transforms {bone name: (euler xyz rad, translation cm)} at a fractional frame."""
    eul, tr = sample_block(anim.blocks[block], bones, frame, **kw)
    return {b.name: (eul[i].tolist(), tr[i].tolist()) for i, b in enumerate(bones)}




def euler_blend(a, b, w):
    """Euler lerp used by the game: wrap each delta to [-pi, pi]; also try the equivalent triple
    (x + pi, pi - y, z + pi) and take the smaller |delta| sum; result a + w * delta."""
    two = 2 * math.pi

    def wrap(d):
        return d - two * math.floor(d / two + 0.5)

    a = np.asarray(a, float)
    b = np.asarray(b, float)
    d1 = np.array([wrap(b[0] - a[0]), wrap(b[1] - a[1]), wrap(b[2] - a[2])])
    d2 = np.array([wrap(b[0] + math.pi - a[0]), wrap(math.pi - b[1] - a[1]), wrap(b[2] + math.pi - a[2])])
    return a + w * (d1 if np.abs(d1).sum() < np.abs(d2).sum() else d2)


def blend_layers(layers, bones):
    """layers: [(block, frame, weight, mirror, yaw)], all full-body. Returns (euler, trans) like the
    game's pass-1 accumulation (first layer copied, each next one lerped by w / (W + w))."""
    n = len(bones)
    W = np.zeros(n)
    R = np.zeros((n, 3))
    T = np.zeros((n, 3))
    for blk, fr, w, mir, yaw in layers:
        e, t = sample_block(blk, bones, fr, mirror=mir, yaw=yaw)
        for i in range(n):
            if W[i] == 1.0:
                continue
            if W[i] == 0.0:
                W[i], R[i], T[i] = w, e[i], t[i]
                continue
            tot = W[i] + w
            fo, fn = W[i] / tot, w / tot
            W[i] = tot
            T[i] = T[i] * fo + t[i] * fn
            R[i] = euler_blend(R[i], e[i], fn)
    return R, T


def euler_to_quat(e):
    """Quaternion (x, y, z, w) of euler_matrix(e)."""
    m = euler_matrix(e)
    tr = m[0, 0] + m[1, 1] + m[2, 2]
    if tr > 0:
        s = math.sqrt(tr + 1.0) * 2
        q = ((m[2, 1] - m[1, 2]) / s, (m[0, 2] - m[2, 0]) / s, (m[1, 0] - m[0, 1]) / s, 0.25 * s)
    elif m[0, 0] > m[1, 1] and m[0, 0] > m[2, 2]:
        s = math.sqrt(1.0 + m[0, 0] - m[1, 1] - m[2, 2]) * 2
        q = (0.25 * s, (m[0, 1] + m[1, 0]) / s, (m[0, 2] + m[2, 0]) / s, (m[2, 1] - m[1, 2]) / s)
    elif m[1, 1] > m[2, 2]:
        s = math.sqrt(1.0 + m[1, 1] - m[0, 0] - m[2, 2]) * 2
        q = ((m[0, 1] + m[1, 0]) / s, 0.25 * s, (m[1, 2] + m[2, 1]) / s, (m[0, 2] - m[2, 0]) / s)
    else:
        s = math.sqrt(1.0 + m[2, 2] - m[0, 0] - m[1, 1]) * 2
        q = ((m[0, 2] + m[2, 0]) / s, (m[1, 2] + m[2, 1]) / s, 0.25 * s, (m[1, 0] - m[0, 1]) / s)
    return q



RIDERS = {  # model file prefix -> (afl abbreviation, character-idle bank or None)
    "brodi": ("Bro", "br2anim.afl"), "eddie": ("Edd", "ed2anim.afl"), "elise": ("Eli", None),
    "jp": ("JP", "jp2anim.afl"), "kaori": ("Kao", "kr2anim.afl"), "luther": ("Lut", "lu2anim.afl"),
    "mac": ("Mac", None), "marisol": ("Mar", "mr2anim.afl"), "moby": ("Mob", None),
    "psymon": ("Psy", "ps2anim.afl"), "seeiah": ("See", None), "zoe": ("Zoe", None),
    "zz_mmm": (None, None),
}
SHARED_SETS = ("bxanim.afl", "franim.afl", "exanim.afl", "cmanim.afl")


def clip_json(anim: Animation, body, board, mirror=False, digits=5):
    """One clip, keys at every authored frame (30 fps). Every body bone gets euler xyz (radians,
    local = Rz(-z) Ry(-y) Rx(-x)) and the same rotation as a quaternion (x, y, z, w); translation
    keys only for bones the animation drives (hips, board); 'board' = the board-part block."""
    def r(v):
        return [round(float(x), digits) for x in v]
    out = {"name": anim.name, "file": anim.file.name, "record": anim.index, "n_frames": anim.n_frames,
           "markers": list(anim.markers), "bones": {}}
    for blk in anim.blocks:
        bones = {0: body, 1: board}.get(blk.chan)     # other channels = head parts (cut-scenes)
        if bones is None:
            continue
        frames = [sample_block(blk, bones, float(f), mirror=mirror) for f in range(anim.n_frames)]
        for i, b in enumerate(bones):
            key = b.name if blk.chan == 0 else "board"
            rec = {"euler": [r(e[i]) for e, _ in frames], "quat": [r(euler_to_quat(e[i])) for e, _ in frames]}
            if b.unknown & 1:
                rec["trans"] = [r(t[i]) for _, t in frames]
            out["bones"][key] = rec
    return out


def export_rider(rider: str, out_dir: str, sets=("bx", "fr", "ex"), include_shared=True):
    """Write <out_dir>/<rider>_anims.json: skeleton + every clip of the shared riding sets, the shared
    crash set and the rider's own uber / idle banks, sampled on the rider's .mpf skeleton."""
    body = load_skeleton(rider)
    board = load_skeleton(rider, "board")
    abbr, idle = RIDERS[rider]
    files = []
    if include_shared:
        files += [s + "anim.afl" for s in sets] + ["cmanim.afl"]
    if abbr:
        files += [f"{s}{abbr}Uber.afl" for s in sets]
    if idle:
        files.append(idle)
    doc = {"rider": rider, "fps": 30.0,
           "convention": "local = T(trans) * Rz(-z) Ry(-y) Rx(-x), column vectors, game units (cm, Z-up). "
                         "Bones without 'trans' keep the skeleton bind translation. The game scales the "
                         "whole rider by its profile scale and runs leg IK to the board.",
           "skeleton": [{"name": b.name, "parent": (b.parent_index if b.parent_file != -1 else -1),
                         "bind_trans": [float(x) for x in b.translation],
                         "bind_euler": [float(x) for x in b.rotation]} for b in body],
           "clips": {}}
    for fn in files:
        if not os.path.exists(os.path.join(afl.AFL_DIR, fn)):
            continue
        f = load_afl(fn)
        for a in f.anims.values():
            if a.blocks:
                doc["clips"][a.name] = clip_json(a, body, board)
    os.makedirs(out_dir, exist_ok=True)
    path = os.path.join(out_dir, f"{rider}_anims.json")
    with open(path, "w") as fh:
        json.dump(doc, fh, separators=(",", ":"))
    return path, len(doc["clips"])



BOARD_MODEL = {(0, False): "Bx", (0, True): "BxGoofy", (1, False): "Fr", (1, True): "FrGoofy",
               (2, False): "Al", (2, True): "AlGoofy"}      # (board_class +0x420, mirror +0x46D4)
def foot_rotations(board_class: int, mirror: bool, rider: str = "brodi") -> dict:
    """Foot rotation relative to the board (row-vector 3x3, the game's cBoarder+0x3D0 / +0x3F4):
    foot_world_rot * inverse(board_rot) in the class's RL_BASECYCLEFAST at frame 0.5, mirrored for goofy.
    Source: numeric, float-exact (<= 5e-7) on all 12 RAM riders; the writer was not located (frame 0.5 =
    the pose after the first clock tick of the riding cycle; bx/fr feet are constant over the cycle)."""
    bank = ("bxanim.afl", "franim.afl", "exanim.afl")[board_class]
    f = load_afl(bank)
    a = [x for x in f.anims.values() if x.name.endswith("RL_BASECYCLEFAST")][0]
    body, board = load_skeleton(rider), load_skeleton(rider, "board")
    yaw = -math.pi if mirror else 0.0
    e, t = sample_block(a.blocks[0], body, 0.5, mirror=mirror, yaw=yaw)
    eb, tb = sample_block(a.blocks[1], board, 0.5, mirror=mirror, yaw=yaw)
    rows = [None] * len(body)
    for i, bn in enumerate(body):
        L = local_rows(e[i], t[i]).astype(float)
        rows[i] = L if bn.parent_file == -1 else L @ rows[bn.parent_index]
    Binv = np.linalg.inv(local_rows(eb[0], tb[0]).astype(float)[:3, :3])
    idx = {bn.name: i for i, bn in enumerate(body)}
    return {k: rows[idx[k]][:3, :3] @ Binv for k in ("l_foot", "r_foot")}


def foot_targets(board_world_rows: np.ndarray, board_class: int, mirror: bool, scale: float = 1.0,
                 rider: str = "brodi") -> dict:
    """0x129628 / 0x208398 / 0x2059F8: world foot targets as row-vector 4x4: position = (board.mpf IK
    point 0 (l_foot) / 1 (r_foot) of the board model, times the rider scale) x board matrix (cBoarder+0x4A60);
    rotation rows = foot_rotations(...) x board rotation rows (written into the palette after the IK)."""
    sys.path.insert(0, os.path.join(REBUILD, "tools"))
    import mpf
    name = BOARD_MODEL[(board_class, bool(mirror))]
    ik = {m.name: m.ik_points for m in mpf.load(os.path.join(MDL_DIR, "board.mpf"))}[name]
    rot = foot_rotations(board_class, mirror, rider)
    out = {}
    for k, bn in enumerate(("l_foot", "r_foot")):
        m = np.eye(4)
        m[:3, :3] = rot[bn] @ board_world_rows[:3, :3]
        m[3, :3] = (np.array([*(np.asarray(ik[k]) * scale), 1.0]) @ board_world_rows)[:3]
        out[bn] = m
    return out


F32 = np.float32
_ASIN_C = (F32(0.16770227253437042), F32(0.06765850633382797), F32(0.05200503021478653),
           F32(0.05498472973704338))            # .data 0x3BB900..0x3BB90C


def sincos_ee(a):
    """0x250C10: quadrant reduction + odd polynomial for sin, cos = sqrt(1 - sin^2) (float32)."""
    a = F32(a)
    k = F32(a * F32(0.63661975) - F32(0.5)) if a < 0 else F32(a * F32(0.63661975) + F32(0.5))
    ki = int(k)                                  # truncation toward zero (cvt.w.s)
    q = ki & 3
    x = F32(a - F32(F32(ki) * F32(1.5707964)))
    x2 = F32(x * x)
    s = F32(F32(F32(F32(F32(F32(F32(x2 * F32(2.7557319e-06)) + F32(-0.0001984127)) * x2 + F32(0.008333334))
                         * x2 + F32(-0.16666667)) * x2) + F32(1.0)) * x)
    c = F32(math.sqrt(max(F32(1.0) - s * s, F32(0.0))))
    return [(s, c), (c, -s), (-s, -c), (-c, s)][q]


def asin_ee(x):
    """0x250FD0: polynomial arcsine with the |x| > 1/sqrt(2) branch (float32)."""
    x = F32(x)
    c0, c1, c2, c3 = _ASIN_C
    if x < F32(-0.70710677):
        u = F32(F32(1.0) - x * x)
        return F32(F32(math.sqrt(u)) * F32(u * (c0 + u * (c1 + u * (c2 + u * c3))) + F32(1.0)) - F32(1.5707964))
    u = F32(x * x)
    if x <= F32(0.70710677):
        return F32(F32(u * (c0 + u * (c1 + u * (c2 + u * c3)))) * x + x)
    u = F32(F32(1.0) - u)
    return F32(F32(1.5707964) - F32(math.sqrt(u)) * F32(u * (c0 + u * (c1 + u * (c2 + u * c3))) + F32(1.0)))


def local_rows(e, t):
    """Row-vector local matrix built by 0x205A78 / 0x205E98 / 0x207198 from Euler (x, y, z) and a
    translation (v_world = v * local * parent). Same rotation as euler_matrix(e).T."""
    sx, cx = sincos_ee(e[0])
    sy, cy = sincos_ee(e[1])
    sz, cz = sincos_ee(e[2])
    m = np.zeros((4, 4), dtype=F32)
    m[0] = (cy * cz, -cy * sz, sy, 0)
    m[1] = (sx * sy * cz + cx * sz, cx * cz - sx * sy * sz, -sx * cy, 0)
    m[2] = (sx * sz - cx * sy * cz, cx * sy * sz + sx * cz, cx * cy, 0)
    m[3] = (t[0], t[1], t[2], 1)
    return m


def leg_ik_ee(parent_rows, thigh_euler, bind_t, target):
    """Exact port of 0x205E98 for one leg (row-vector float32 matrices, world space).

    parent_rows: palette matrix of the thigh's parent (hips), 4x4 rows; thigh_euler: the sampled
    thigh Euler (pose +0x000); bind_t: {'thigh','calf','foot'} bind translations of the (scaled)
    skeleton (bone +0x18; the segment lengths are the x components); target: world foot position.
    Returns (thigh_rows, calf_rows, foot_position). The foot's rotation rows are NOT written by
    this routine (0x129628 writes them after the IK, see foot_rotations)."""
    T = local_rows(thigh_euler, bind_t["thigh"]) @ parent_rows.astype(F32)
    hip = T[3, :3].copy()
    d = (np.asarray(target, dtype=F32) - hip).astype(F32)
    dist = F32(math.sqrt(float(np.dot(d, d))))
    inv = F32(F32(1.0) / dist)
    dirv = (d * inv).astype(F32)
    Lt = F32(bind_t["calf"][0])                 # thigh length = calf bone offset x
    Lc = F32(bind_t["foot"][0])                 # calf length = foot bone offset x
    b = F32(Lc * inv)
    a = F32(Lt * inv)
    ang = F32(0.0)
    if a + b > F32(1.0):
        s11 = F32(a * a + b * b)
        s13 = F32(a * a - b * b)
        h = F32(F32(F32(s11 + s11) - s13 * s13) - F32(1.0))
        if h >= 0:
            h = F32(F32(math.sqrt(h)) * F32(0.5))
            x1 = min(max(F32(h / a), F32(-0.99999)), F32(0.99999))
            x2 = min(max(F32(h / b), F32(-0.99999)), F32(0.99999))
            ang = F32(asin_ee(x1) + asin_ee(x2))
    if ang < F32(0.2):
        ang = F32(0.2)
    s, c = sincos_ee(ang)
    p = np.array([Lt + c * Lc, s * Lc, 0, 1], dtype=F32) @ T     # foot with the knee bent about thigh z
    v = (p[:3] - hip).astype(F32)
    v = (v * F32(1.0 / math.sqrt(float(np.dot(v, v))))).astype(F32)
    ax = np.cross(dirv, v).astype(F32)
    sn = F32(math.sqrt(float(np.dot(ax, ax))))
    if sn > F32(0.001):
        ax = (ax * F32(F32(1.0) / sn)).astype(F32)
        a2 = asin_ee(min(sn, F32(0.99999)))
        si, co = sincos_ee(a2)
        x, y, z = ax
        k = F32(F32(1.0) - co)
        R = np.array([[k * x * x + co, k * y * x - si * z, k * z * x + si * y],
                      [k * x * y + si * z, k * y * y + co, k * z * y - si * x],
                      [k * x * z - si * y, k * y * z + si * x, k * z * z + co]], dtype=F32)
        T[:3, :3] = (T[:3, :3] @ R).astype(F32)
    C = local_rows((0.0, 0.0, -ang), bind_t["calf"]) @ T
    foot = (np.array([*bind_t["foot"], 1.0], dtype=F32) @ C)[:3]
    return T, C, foot




def _axis_rows(ax, s, c):
    """Axis-angle rotation in the row layout written by 0x205E98 / 0x12AE18 (v' = v * R)."""
    x, y, z = ax
    k = F32(1.0) - c
    return np.array([[k * x * x + c, k * y * x - s * z, k * z * x + s * y],
                     [k * x * y + s * z, k * y * y + c, k * z * y - s * x],
                     [k * x * z - s * y, k * y * z + s * x, k * z * z + c]], dtype=F32)


def root_frame(quat, pos, steer, motion, board_class, v2c0, s308, v330, scale, lean, spin=0.0, flip=0.0,
               hips_z=0.0, susp=0.0, ik_branch=True):
    """[0x129628] rider root (row-vector 4x4) used by the palette builder. Inputs are cBoarder fields:
    quat (w, x, y, z) +0x170, pos +0x140, steer +0x214, motion +0x424, board_class +0x420,
    v2c0 +0x2C0 (ground normal), s308 +0x308, v330 +0x330, scale = profile(+0x464)+0x60,
    lean +0x244, spin +0x1D0, flip +0x1D4, hips_z = sampled hips translation z (pose+0x110),
    susp = suspension_offset +0x1C0. ik_branch=False gives the frame the board is built from.
    Exact on all 12 RAM riders (rotation 0.0, position <= 0.001 cm after the final hips move)."""
    w, x, y, z = [F32(v) for v in quat]
    M = np.eye(4, dtype=F32)
    M[0, :3] = (1 - 2 * y * y - 2 * z * z, 2 * x * y + 2 * w * z, 2 * x * z - 2 * w * y)
    M[1, :3] = (2 * x * y - 2 * w * z, 1 - 2 * x * x - 2 * z * z, 2 * y * z + 2 * w * x)
    M[2, :3] = (2 * x * z + 2 * w * y, 2 * y * z - 2 * w * x, 1 - 2 * x * x - 2 * y * y)
    if steer != 0 and motion != 3:                       # visual lean about local y
        s, c = sincos_ee(F32(steer) * F32(1.134464 if board_class == 2 else 0.87266463))
        M = np.array([[c, 0, s, 0], [0, 1, 0, 0], [-s, 0, c, 0], [0, 0, 0, 1]], dtype=F32) @ M
    M[3, :3] = (np.asarray(pos, F32) + np.asarray(v2c0, F32) * F32(s308)
                + np.asarray(v330, F32) * F32(F32(scale) * F32(lean)))
    M[3, 3] = 1
    if spin != 0 or flip != 0:                           # trick spin (z) / flip (x) about the hips height
        M[3] = np.array([0, 0, hips_z, 1], dtype=F32) @ M
        s, c = sincos_ee(spin)
        M = np.array([[c, -s, 0, 0], [s, c, 0, 0], [0, 0, 1, 0], [0, 0, 0, 1]], dtype=F32) @ M
        s, c = sincos_ee(flip)
        M = np.array([[1, 0, 0, 0], [0, c, -s, 0], [0, s, c, 0], [0, 0, 0, 1]], dtype=F32) @ M
        M[3] = np.array([0, 0, -hips_z, 1], dtype=F32) @ M
    if ik_branch and motion in (2, 4):                   # suspension drop along the ground normal
        d = np.asarray(v2c0, F32) * F32(susp) * F32(scale)
        M[3] = np.array([*d, 1], dtype=F32) @ M
    return M


def board_frame(root_no_susp, board_euler, board_trans, motion, v2c0, s1cc=0.0, v2a0=(0, 0, 0)):
    """[0x12AE18] board world matrix (cBoarder+0x4A60): FK of the board part (its own pose object,
    cBoarder+0x7C0+0x394) from the root without the suspension offset; on the ground (motion 2) the
    board x axis is rotated to lie in the ground plane (normal v2c0 +0x2C0); then, if +0x1CC > 0, an
    offset +0x2A0 * +0x1CC. Exact on all 12 RAM riders."""
    B = local_rows(board_euler, board_trans) @ root_no_susp
    if motion == 2:
        n = np.asarray(v2c0, F32)
        bx = B[0, :3].copy()
        ax = np.cross(n, bx).astype(F32)
        s = F32(math.sqrt(float(np.dot(ax, ax))))
        if s > F32(1e-8):
            ax = (ax * (F32(1.0) / s)).astype(F32)
            s = min(s, F32(0.99999))
            a = F32(1.5707964) - asin_ee(s)
            sn, cs = sincos_ee(-a if float(np.dot(n, bx)) > 0 else a)
            t = B[3].copy()
            B[:3, :3] = B[:3, :3] @ _axis_rows(ax, sn, cs)
            B[3] = t
    if s1cc > 0:
        B[3] = np.array([*(np.asarray(v2a0, F32) * F32(s1cc)), 1], dtype=F32) @ B
    return B


def build_palette(root, body, euler, trans, board_world, board_class, mirror, scale, rider="brodi"):
    """[0x205A78 + 0x205E98 + 0x129628] world palette (row-vector 4x4 per body bone).

    root: root_frame(...); body: skeleton bones with the game's (scaled) bind translations, i.e. .mpf
    translation * scale; euler/trans: sampled pose (pose+0x000 / +0x108; trans[0] already scaled).
    Bones 0..12 FK; legs 2-bone IK (right leg first) to the board IK points; foot rotations from
    foot_rotations(). Exact against the RAM palettes (see afl_ram_check)."""
    pal = [None] * len(body)
    pal[0] = local_rows(euler[0], trans[0]) @ root
    for i in range(1, 13):
        pal[i] = local_rows(euler[i], body[i].translation) @ pal[body[i].parent_index]
    tg = foot_targets(board_world, board_class, mirror, scale, rider)
    for t, c, f, side in ((16, 17, 18, "r_foot"), (13, 14, 15, "l_foot")):
        T, Cm, foot = leg_ik_ee(pal[body[t].parent_index], euler[t],
                                {"thigh": body[t].translation, "calf": body[c].translation,
                                 "foot": body[f].translation}, tg[side][3, :3])
        pal[t], pal[c] = T, Cm
        F = np.eye(4, dtype=F32)
        F[:3, :3] = tg[side][:3, :3]
        F[3, :3] = foot
        pal[f] = F
    return pal



_TABLES = None


def controller_tables() -> dict:
    """Name tables read from the exe (.data), resolved to animation names.
    riding  [class][10] @0x3791E8  (0x160680)   air_dir [class][5] @0x379190 (0x1601D0)
    air_idle[class]     @0x379180  (0x160140)   switch  [class][2] @0x3791D0 (0x1605A0)
    prewind [class][10 rows][5 grind types] @0x379260 (0x160B20)
    rail    [class][6 rows][5 grind types]  @0x3794B8 (0x161118)"""
    global _TABLES
    if _TABLES is None:
        b = afl.Bank.__new__(afl.Bank)
        b.exe = open(afl.EXE, "rb").read()
        names = b._name_table()
        u = b._x32

        def n(va):
            return names[u(va)]
        _TABLES = {
            "riding": [[n(0x3791E8 + c * 0x28 + 4 * i) for i in range(10)] for c in range(3)],
            "air_dir": [[n(0x379190 + c * 0x14 + 4 * i) for i in range(5)] for c in range(3)],
            "air_idle": [n(0x379180 + 4 * c) for c in range(3)],
            "switch": [[n(0x3791D0 + c * 8 + 4 * i) for i in range(2)] for c in range(3)],
            "prewind": [[[n(0x379260 + c * 0xC8 + r * 0x14 + 4 * g) for g in range(5)] for r in range(10)]
                        for c in range(3)],
            "rail": [[[n(0x3794B8 + c * 0x78 + r * 0x14 + 4 * g) for g in range(5)] for r in range(6)]
                     for c in range(3)],
        }
    return _TABLES


def _atan2_ee(y, x):
    """atan2 as built in 0x1601D0 / 0x160B20 from atan(y/x) (0x2510F8) with quadrant fix; (0,0) never
    reaches it."""
    if x == 0:
        return F32(-1.5707964) if y < 0 else F32(1.5707964)
    a = F32(math.atan(float(F32(y) / F32(x))))
    if x < 0:
        a = F32(a + F32(3.1415927)) if y > 0 else F32(a - F32(3.1415927))
    return a


def procedural_subs(mode: int, st: dict, board_class: int, n_frames) -> dict:
    """Sub-animations of a channel-0 instance for the procedural loop modes (property field 2).

    st: cBoarder fields: steer_follow (+0x268), brake (+0x1FC), crouch (+0x208), goofy (+0x1B4),
    air_turn (+0x27C), air_tilt (+0x280), ramp_1f0 (+0x1F0), steer (+0x214), grind_type (+0x1DC),
    pw_x (+0x220), pw_y (+0x22C), gate (+0x238).  n_frames(name) -> frame count.
    Returns {'subs': [(anim name, frame or None, weight)], 'clock': bool}; frame None = the sub keeps
    the instance clock (0x15A030 advances it); clock True = the clock advances this tick."""
    T = controller_tables()
    c = board_class
    one = F32(1.0)
    cr = F32(st.get("crouch", 0.0))
    if mode == 4:                                         # riding 0x1FF (0x160680)
        t, b = F32(st["steer_follow"]), F32(st["brake"])
        at, ab = abs(t), abs(b)
        if st.get("goofy"):
            t, b = -t, -b
        R = T["riding"][c]
        if t == 0 and ab == 0 and (c != 2 or cr in (F32(0.0), one)):
            return {"subs": [(R[4], None, one - cr), (R[5], None, cr)], "clock": True, "cycle": True}
        if ab == 0:
            if c == 2:
                if cr < F32(0.5):
                    w0, w1 = one - (cr + cr), cr + cr
                    s0, s1 = (R[1], R[9]) if t > 0 else (R[0], R[8])
                else:
                    w0, w1 = F32(2.0) - (cr + cr), (cr + cr) - one
                    s0, s1 = (R[9], R[3]) if t > 0 else (R[8], R[2])
            else:
                w0, w1 = one - cr, cr
                s0, s1 = (R[1], R[3]) if t > 0 else (R[0], R[2])
            return {"subs": [(s0, at * (n_frames(s0) - 1), w0), (s1, at * (n_frames(s1) - 1), w1)], "clock": False}
        s0 = R[1] if t > 0 else R[0]
        s1 = R[6] if b > 0 else R[7]
        return {"subs": [(s0, at * (n_frames(s0) - 1), one - ab), (s1, ab * (n_frames(s1) - 1), ab)], "clock": False}
    if mode == 5:                                         # switch 0x200 (0x1605A0)
        a, b = T["switch"][c]
        return {"subs": [(a, None, one - cr), (b, "sync", cr)], "clock": True,
                "note": "sub1 frame = sub0 progress (frame / (n-1)) * (n1 - 1), 0x159E80"}
    if mode == 6:                                         # take-off poses 0x218-0x220 (0x161300)
        return {"subs": [(None, "(1-crouch)*(n-1)", one)], "clock": False,
                "frame": lambda n: (one - cr) * (n - 1)}
    if mode == 7:                                         # rail cycle 0x254-0x257 (0x161118)
        s = F32(st["steer"])
        g = st["grind_type"]
        R = T["rail"][c]
        if abs(s) == 0 or g in (1, 2):
            return {"subs": [(R[4][g], None, one - cr), (R[5][g], None, cr)], "clock": True, "cycle": True}
        s0, s1 = (R[0][g], R[1][g]) if s > 0 else (R[2][g], R[3][g])
        return {"subs": [(s0, abs(s) * (n_frames(s0) - 1), one - cr), (s1, abs(s) * (n_frames(s1) - 1), cr)],
                "clock": False}
    if mode == 8:                                         # air idle 0x223 (0x160140)
        a = T["air_idle"][c]
        return {"subs": [(a, F32(st["ramp_1f0"]) * (n_frames(a) - 1), one)], "clock": False}
    if mode in (9, 10):                                   # air directional 0x222 / prewind 0x217
        if mode == 9:
            x, y = F32(st["air_turn"]), F32(st["air_tilt"])
            D = T["air_dir"][c]
            if x == 0 and y == 0:
                return {"subs": [(D[4], F32(0.0), one)], "clock": False}
            if st.get("goofy"):
                x, y = -x, -y
            th = _atan2_ee(y, x)
            h = F32(1.5707964)
            if th >= h:
                f, s0, s1 = th - h, D[0], D[2]
            elif th >= 0:
                f, s0, s1 = th, D[2], D[1]
            elif th <= -h:
                f, s0, s1 = th + h, D[0], D[3]
            else:
                f, s0, s1 = th, D[3], D[1]
            mag = min(max(F32(math.sqrt(float(x * x + y * y))), F32(0.0)), one)
            w0 = abs(f) * F32(0.63661975)
            return {"subs": [(s0, mag * (n_frames(s0) - 1), w0), (s1, mag * (n_frames(s1) - 1), one - w0)],
                    "clock": False}
        x, y = F32(st["pw_x"]), F32(st["pw_y"])
        g = st["grind_type"]
        P = [row[g] for row in T["prewind"][c]]          # rows 0..9 for this grind type
        if x == 0 and y == 0:
            return {"subs": [(P[0], F32(0.0), one - cr), (P[1], cr * (n_frames(P[1]) - 1), cr)], "clock": False}
        if st.get("goofy"):
            x, y = -x, -y
        th = _atan2_ee(y, x)
        q = F32(0.7853982)
        sectors = [(3 * q, 2, 3), (2 * q, 3, 4), (q, 4, 5), (F32(0.0), 5, 6)]
        f = s0 = s1 = None
        for lo, a, b in sectors:
            if th >= lo:
                f, s0, s1 = th - lo, P[a], P[b]
                break
        if f is None:
            for hi, a, b in ((-3 * q, 2, 9), (-2 * q, 9, 8), (-q, 8, 7)):
                if th <= hi:
                    f, s0, s1 = th - hi, P[a], P[b]
                    break
        if f is None:
            f, s0, s1 = th, P[7], P[6]
        mag = min(max(F32(math.sqrt(float(x * x + y * y))), F32(0.0)), one)
        wa = abs(f) * F32(1.2732395)
        return {"subs": [(s0, mag * (n_frames(s0) - 1), cr * wa), (s1, mag * (n_frames(s1) - 1), cr * (one - wa)),
                         (P[0], F32(0.0), one - cr)], "clock": False}
    if mode == 11:                                        # gate 0x1FC (0x160AF0)
        return {"subs": [(None, "gate*(n-1)", one)], "clock": False, "frame": lambda n: F32(st["gate"]) * (n - 1)}
    return {"subs": None, "clock": True}                  # modes 0-3: plain clock (0x15A030)


def fade_in_rate(weight: float, speed: float, blend: int) -> float:
    """0x159E38: per-game-frame weight increment of a new instance (weight starts at 0), in float:
    (1 - w) x ((|speed| x 30) / float(B x 60))."""
    return float(F32(F32(1.0) - F32(weight)) * F32(F32(abs(F32(speed)) * F32(30.0)) / F32(blend * 60)))


def fade_out_rate(speed: float, blend: int) -> float:
    """0x159D88: per-game-frame weight increment (negative) of the instances being replaced, from each instance's own
    speed, in float: (|speed| x -30) / float(B x 60)."""
    return float(F32(abs(F32(speed)) * F32(-30.0)) / F32(blend * 60))




def _is_rot_track(chan: int, k: int) -> bool:
    # body: hips = 3 trans + 3 rot, then 3 rot per bone; board: 3 trans + 3 rot
    return k >= 3


def validate():
    """Decode every track of every animation in all .afl files; check segment types, lengths,
    and the largest step between adjacent half frames (rotation tracks, wrapped)."""
    import collections
    import glob
    body = load_skeleton("brodi")
    board = load_skeleton("brodi", "board")
    types = collections.Counter()
    errors = []
    steps = []
    n_anims = n_tracks = 0
    for p in sorted(glob.glob(os.path.join(afl.AFL_DIR, "*.afl"))):
        f = load_afl(os.path.basename(p))
        T = (f.off_data - f.off_tracks) // 4
        for a in f.anims.values():
            n_anims += 1
            for blk in a.blocks:
                bones = body if blk.chan == 0 else (board if blk.chan == 1 else None)
                if bones is None:
                    errors.append((f.name, a.name, f"unknown channel {blk.chan}"))
                    continue
                need = sum(3 * ((b.unknown & 1) + ((b.unknown >> 1) & 1)) for b in bones)
                if blk.first + need > T:
                    errors.append((f.name, a.name, "track table overrun"))
                    continue
                for k in range(need):
                    try:
                        segs = blk.track(k)
                    except Exception as e:  # noqa: BLE001
                        errors.append((f.name, a.name, k, str(e)))
                        continue
                    n_tracks += 1
                    for s in segs:
                        types[s[0]] += 1
                    if len(segs) > 1:
                        total = sum(s[1] - 1 for s in segs) + 1
                        if total != a.n_frames:
                            errors.append((f.name, a.name, k, f"piecewise covers {total} of {a.n_frames}"))
                    elif segs[0][0] in (4, 6, 7) and segs[0][1] < a.n_frames:
                        errors.append((f.name, a.name, k, f"{segs[0][1]} samples < {a.n_frames} frames"))
                    if a.n_frames > 1:
                        vals = np.array([eval_track(segs, t) for t in np.arange(0, a.n_frames - 1 + 1e-9, 0.5)])
                        d = np.diff(vals)
                        if _is_rot_track(blk.chan, k):
                            d = (d + math.pi) % (2 * math.pi) - math.pi
                            steps.append((float(np.abs(d).max()), f.name, a.name, blk.chan, k))
    steps.sort(reverse=True)
    st = np.array([s[0] for s in steps])
    return dict(n_files=len(glob.glob(os.path.join(afl.AFL_DIR, "*.afl"))), n_anims=n_anims, n_tracks=n_tracks,
                segment_types=dict(sorted(types.items())), n_errors=len(errors), errors=errors[:30],
                rot_step_half_frame_deg=dict(median=round(math.degrees(float(np.median(st))), 3),
                                             p99=round(math.degrees(float(np.percentile(st, 99))), 2),
                                             max=round(math.degrees(float(st.max())), 2)),
                largest_steps=[(round(math.degrees(s[0]), 1),) + s[1:] for s in steps[:10]])


def loop_seams(names=("bxRL_BASECYCLEFAST", "bxRL_CROUCHCYCLE", "frRL_BASECYCLEFAST", "frRL_CROUCHCYCLE",
                      "exRL_BASECYCLEFAST", "exR_FAKIECRUISE", "bxJ_AIRT2S", "exJ_AIRT2S", "bxRS_CYCLEBASE",
                      "bxA_SPINCYCLEFS", "bxA_FLIPCYCLEFWD", "cmCC_BUTTSLIDE", "cmFL_FINISHCYCLE")):
    """First vs last frame of cyclic animations: max bone rotation difference and hips translation."""
    body = load_skeleton("brodi")
    out = {}
    for fn in SHARED_SETS:
        f = load_afl(fn)
        for a in f.anims.values():
            if a.name in names and a.blocks:
                e0, t0 = sample_block(a.blocks[0], body, 0.0)
                e1, t1 = sample_block(a.blocks[0], body, float(a.n_frames - 1))
                d = []
                for i in range(len(body)):
                    m = euler_matrix(e0[i]).T @ euler_matrix(e1[i])
                    d.append(math.degrees(math.acos(max(-1.0, min(1.0, (np.trace(m) - 1) / 2)))))
                out[a.name] = dict(n_frames=a.n_frames, seam_max_rot_deg=round(max(d), 2),
                                   seam_hips_cm=round(float(np.linalg.norm(t0[0] - t1[0])), 2))
    return out


def main(argv):
    if len(argv) < 2:
        print(__doc__)
        return
    if argv[1] == "dump":
        f = load_afl(argv[2])
        a = f.anims[int(argv[3])]
        print(a.name, a.n_frames, a.markers)
        body = load_skeleton("brodi")
        for blk in a.blocks:
            n = 60 if blk.chan == 0 else 6
            for k in range(n):
                print(blk.chan, k, [(s[0], s[1]) for s in blk.track(k)])
        e, t = sample_block(a.blocks[0], body, 0.0)
        for i, b in enumerate(body):
            print(b.name, np.round(e[i], 4), np.round(t[i], 2))
    elif argv[1] == "export":
        out = argv[2] if len(argv) > 2 else os.path.join(afl.REPO, "converted", "anim_clips")
        for r in (argv[3:] or list(RIDERS)):
            print(export_rider(r, out))
    elif argv[1] == "validate":
        print(json.dumps(validate(), indent=1, default=str))
        print(json.dumps(loop_seams(), indent=1))
    else:
        print(__doc__)


if __name__ == "__main__":
    main(sys.argv)
