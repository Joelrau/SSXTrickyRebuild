"""Validate scripts/afl_curves.py against the live RAM snapshots.

For each rider (cBoarder addresses in traces/live/<session>/meta.json):
  pose blender  = cBoarder + 0x7C0  (model object +0x79C, pose at model +0x24; the 0x202B00 object)
     +0x000 euler[22][3], +0x108 trans[22][3] (outputs), +0x28C n_layers, +0x290 layer anim objects,
     +0x2B8 layer frame, +0x2E0 layer weight, +0x308 layer yaw, +0x330 layer mirror, +0x358 layer bone mask,
     +0x380 translation scale, +0x388 bones (84 B), +0x38C n_bones, +0x390 mask bit shift
  layer object  +0x008 curve record (RAM pointer into the loaded bank), +0x02C first track per bone,
                +0x0A4 bound track pointers
  palette       = cBoarder + 0x4AE0: 19 world matrices (row-vector 4x4), board at +0x4A60 / +0x4FA0,
                  root frame at +0x4AA0 (hips position, rider orientation incl. lean)

Checks: (1) our blended pose vs pose+0x000/+0x108 (what the sampler wrote this frame);
        (2) our pose composed with the skeleton vs the palette, bone relative to parent
            (rotation error deg, translation error cm), which removes the world placement.
Usage: python scripts/afl_ram_check.py
"""
import glob
import json
import math
import os
import struct
import sys
from types import SimpleNamespace as NS

import numpy as np

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import afl  # noqa: E402
import afl_curves as C  # noqa: E402

sys.path.insert(0, afl.ROOT)
import traces  # noqa: E402
LIVE = str(traces.root() / "live") if traces.root() else "live"
POSE, PALETTE, ROOTF, BOARD = 0x7C0, 0x4AE0, 0x4AA0, 0x4A60


class Ram:
    def __init__(self, path):
        self.b = open(path, "rb").read()

    def u32(self, a): return struct.unpack_from("<I", self.b, a)[0]
    def i32(self, a): return struct.unpack_from("<i", self.b, a)[0]
    def f32(self, a): return struct.unpack_from("<f", self.b, a)[0]
    def fv(self, a, n): return np.array(struct.unpack_from("<%df" % n, self.b, a))

    def bones(self, p, n):
        out = []
        for i in range(n):
            o = p + 84 * i
            name = self.b[o:o + 16].split(b"\0")[0].decode("latin1")
            pf, pi, fl, bid = struct.unpack_from("<4h", self.b, o + 16)
            v = struct.unpack_from("<15f", self.b, o + 24)
            out.append(NS(name=name, parent_file=pf, parent_index=pi, unknown=fl, bone_id=bid,
                          translation=np.array(v[0:3]), rotation=np.array(v[3:6]), extra=v[6:15]))
        return out


_FILES = None


def afl_files():
    global _FILES
    if _FILES is None:
        _FILES = []
        for p in sorted(glob.glob(os.path.join(afl.AFL_DIR, "*.afl"))):
            d = open(p, "rb").read(0x400)
            _FILES.append((os.path.basename(p), d))
    return _FILES


_BANKS = {}


def locate(ram: Ram, rec_ptr: int):
    """RAM curve-record pointer -> (AflFile, record index)."""
    a = rec_ptr - (rec_ptr % 4)
    for _ in range(0x40000):
        if ram.b[a:a + 2] == b"\x4c\x11":
            n, ot = struct.unpack_from("<HI", ram.b, a + 2)
            if ot == 0x0C + n * 36 and a + 0x0C <= rec_ptr < a + ot:
                key = (a, ram.b[a:a + 0x200])
                if key not in _BANKS:
                    for name, d in afl_files():
                        if d[:0x200] == ram.b[a:a + 0x200]:
                            _BANKS[key] = C.load_afl(name)
                            break
                    else:
                        _BANKS[key] = None
                f = _BANKS[key]
                return f, (rec_ptr - a - 0x0C) // 36
        a -= 4
    return None, None


def block_of(f, rec):
    for an in f.anims.values():
        for b in an.blocks:
            if b.rec == rec:
                return an, b
    return None, None


def euler_blend(a, b, w):
    """0x202520 / 0x202758: wrap each delta to [-pi, pi], also try the equivalent Euler triple
    (x + pi, pi - y, z + pi); take the one with the smaller |delta| sum; result a + w * delta."""
    two = 2 * math.pi

    def wrap(d):
        return d - two * math.floor(d / two + 0.5)

    d1 = np.array([wrap(b[0] - a[0]), wrap(b[1] - a[1]), wrap(b[2] - a[2])])
    d2 = np.array([wrap(b[0] + math.pi - a[0]), wrap(-b[1] - a[1] + math.pi), wrap(b[2] + math.pi - a[2])])
    d = d1 if np.abs(d1).sum() < np.abs(d2).sum() else d2
    return a + w * d


def blend_pose(ram: Ram, pose: int, info: list):
    """Re-run 0x202B00: two passes (pass 0 = masked layers, pass 1 = full-body layers), per-bone
    weight accumulation, Euler lerp; then pass 0 over pass 1 by the pass-0 weight."""
    nl = ram.i32(pose + 0x28C)
    nb = ram.i32(pose + 0x38C)
    bones = ram.bones(ram.u32(pose + 0x388), nb)
    tscale = ram.f32(pose + 0x380)
    shift = ram.i32(pose + 0x390)
    acc = [dict(w=np.zeros(nb), r=np.zeros((nb, 3)), t=np.zeros((nb, 3))) for _ in range(2)]
    for p in range(2):
        for L in range(nl):
            mask = ram.u32(pose + 0x358 + 4 * L)
            if (p == 0 and mask == 0xFFFFFFFF) or (p == 1 and mask != 0xFFFFFFFF):
                continue
            lay = ram.u32(pose + 0x290 + 4 * L)
            f, rec = locate(ram, ram.u32(lay + 8))
            an, blk = block_of(f, rec) if f else (None, None)
            t = ram.f32(pose + 0x2B8 + 4 * L)
            w = ram.f32(pose + 0x2E0 + 4 * L)
            yaw = ram.f32(pose + 0x308 + 4 * L)
            mir = ram.i32(pose + 0x330 + 4 * L) != 0
            info.append(dict(layer=L, pass_=p, anim=an.name if an else "?", file=f.name if f else "?",
                             record=an.index if an else None, n_frames=an.n_frames if an else None,
                             frame=t, weight=w, mirror=mir, yaw=round(yaw, 4), mask=hex(mask)))
            if blk is None:
                continue
            e, tr = C.sample_block(blk, bones, t, mirror=mir, yaw=yaw, trans_scale=tscale)
            A = acc[p]
            for i in range(nb):
                if not (mask >> (i + shift)) & 1 or A["w"][i] == 1.0:
                    continue
                if A["w"][i] == 0.0:
                    A["w"][i] = w
                    A["t"][i], A["r"][i] = tr[i], e[i]
                else:
                    W = A["w"][i] + w
                    fo, fn = A["w"][i] / W, w / W
                    A["w"][i] = W
                    if bones[i].unknown & 5:
                        A["t"][i] = A["t"][i] * fo + tr[i] * fn
                    if bones[i].unknown & 10:
                        A["r"][i] = euler_blend(A["r"][i], e[i], fn)
    out_r, out_t = np.zeros((nb, 3)), np.zeros((nb, 3))
    for i in range(nb):
        w0 = acc[0]["w"][i]
        if w0 == 1.0:
            out_r[i], out_t[i] = acc[0]["r"][i], acc[0]["t"][i]
        elif w0 == 0.0:
            out_r[i], out_t[i] = acc[1]["r"][i], acc[1]["t"][i]
        else:
            out_t[i] = acc[1]["t"][i] * (1 - w0) + acc[0]["t"][i] * w0
            out_r[i] = euler_blend(acc[1]["r"][i], acc[0]["r"][i], w0)
    return bones, out_r, out_t


def ang(Ra, Rb):
    """Rotation difference in degrees, chord form 2 asin(|Ra - Rb|_F / (2 sqrt 2)): well conditioned near
    0 (the acos-of-trace form reads float32 noise of 1e-7 as ~0.03 deg)."""
    d = float(np.linalg.norm(np.asarray(Ra, float) - np.asarray(Rb, float))) / (2 * math.sqrt(2))
    return math.degrees(2 * math.asin(min(1.0, d)))


def check_rider(ram: Ram, cb: int):
    info = []
    pose = cb + POSE
    bones, er, et = blend_pose(ram, pose, info)
    nb = len(bones)
    E = ram.fv(pose, 3 * nb).reshape(nb, 3)
    T = ram.fv(pose + 0x108, 3 * nb).reshape(nb, 3)
    # (1) pose arrays (Euler compared as rotations)
    pr = [ang(C.euler_matrix(E[i]), C.euler_matrix(er[i])) for i in range(nb)]
    pt = [float(np.linalg.norm(T[i] - et[i])) for i in range(nb)]
    # (2) palette, bone relative to parent
    W = C.world_matrices(bones, er, et)
    M = [ram.fv(cb + PALETTE + 64 * i, 16).reshape(4, 4).T for i in range(nb)]
    rows = []
    for i in range(1, nb):
        p = bones[i].parent_index
        R = np.linalg.inv(M[p]) @ M[i]
        Q = np.linalg.inv(W[p]) @ W[i]
        rows.append((bones[i].name, ang(R[:3, :3], Q[:3, :3]), float(np.linalg.norm(R[:3, 3] - Q[:3, 3]))))
    # root: palette hips = rootframe(+0x4AA0) * R(hips euler)
    RF = ram.fv(cb + ROOTF, 16).reshape(4, 4).T
    hips_r = ang((np.linalg.inv(RF) @ M[0])[:3, :3], C.euler_matrix(er[0]))
    # (3) full world palette from the boarder state: root frame, board frame, FK, leg IK, foot rotations
    #     (afl_curves.root_frame / board_frame / build_palette), compared in absolute world space.
    prof = ram.u32(cb + 0x464)
    st = dict(quat=ram.fv(cb + 0x170, 4), pos=ram.fv(cb + 0x140, 3), steer=ram.f32(cb + 0x214),
              motion=ram.i32(cb + 0x424), board_class=ram.i32(cb + 0x420), v2c0=ram.fv(cb + 0x2C0, 3),
              s308=ram.f32(cb + 0x308), v330=ram.fv(cb + 0x330, 3), scale=ram.f32(prof + 0x60),
              lean=ram.f32(cb + 0x244), spin=ram.f32(cb + 0x1D0), flip=ram.f32(cb + 0x1D4),
              hips_z=float(et[0][2]), susp=ram.f32(cb + 0x1C0))
    root = C.root_frame(**st)
    root0 = C.root_frame(**{**st, "ik_branch": False})
    bpose = pose + 0x394                                  # board part pose object
    bw = C.board_frame(root0, ram.fv(bpose, 3), ram.fv(bpose + 0x108, 3), st["motion"], st["v2c0"],
                       ram.f32(cb + 0x1CC), ram.fv(cb + 0x2A0, 3))
    mir = ram.i32(cb + 0x46D4) != 0
    pal = C.build_palette(root, bones, er, et, bw, st["board_class"], mir, st["scale"])
    Mrow = [ram.fv(cb + PALETTE + 64 * i, 16).reshape(4, 4) for i in range(nb)]
    world = [(b.name, ang(pal[i][:3, :3].astype(float).T, Mrow[i][:3, :3].T),
              float(np.linalg.norm(pal[i][3, :3] - Mrow[i][3, :3]))) for i, b in enumerate(bones)]
    RB = ram.fv(cb + BOARD, 16).reshape(4, 4)
    board_err = (ang(bw[:3, :3].astype(float).T, RB[:3, :3].T), float(np.linalg.norm(bw[3, :3] - RB[3, :3])))
    return dict(info=info, pose_rot=pr, pose_tr=pt, rel=rows, hips_vs_rootframe=hips_r, world=world,
                board_err=board_err, bones=bones, er=er, et=et, M=M, cb=cb)


def main():
    out = {}
    for sess in ("ride_session_1", "ride_session_2"):
        meta = json.load(open(os.path.join(LIVE, sess, "meta.json")))
        ram = Ram(os.path.join(LIVE, sess, "spawn_ram.bin"))
        for r in meta["riders"]:
            cb = int(r, 16)
            res = check_rider(ram, cb)
            upper = [x for x in res["rel"] if not any(k in x[0] for k in ("thigh", "calf", "foot"))]
            legs = [x for x in res["rel"] if any(k in x[0] for k in ("thigh", "calf", "foot"))]
            ch = ram.u32(cb + 0x46C8)
            line = dict(
                session=sess, rider=r, player=(cb == 0xDD8CC0), channel0_id=hex(ch),
                layers=res["info"],
                pose_rot_max_deg=round(max(res["pose_rot"]), 4), pose_tr_max_cm=round(max(res["pose_tr"]), 4),
                upper_rot_max=round(max(x[1] for x in upper), 3), upper_rot_med=round(float(np.median([x[1] for x in upper])), 3),
                upper_tr_max=round(max(x[2] for x in upper), 3),
                legs_rot_max=round(max(x[1] for x in legs), 3), legs_rot_med=round(float(np.median([x[1] for x in legs])), 3),
                legs_tr_max=round(max(x[2] for x in legs), 3),
                hips_vs_rootframe_deg=round(res["hips_vs_rootframe"], 3),
                world_rot_max_deg=round(max(x[1] for x in res["world"]), 4),
                world_pos_max_cm=round(max(x[2] for x in res["world"]), 4),
                world_legs_rot_max_deg=round(max(x[1] for x in res["world"] if any(k in x[0] for k in ("thigh", "calf", "foot"))), 4),
                board_rot_deg=round(res["board_err"][0], 4), board_pos_cm=round(res["board_err"][1], 4),
                per_bone={x[0]: [round(x[1], 3), round(x[2], 3)] for x in res["rel"]})
            out.setdefault(sess, []).append(line)
            print(json.dumps({k: v for k, v in line.items() if k != "per_bone"}))
    json.dump(out, open("animation_ram_check.json", "w"), indent=1)


if __name__ == "__main__":
    main()
