"""Validate models/face_secondary.py against the live RAM snapshots and traces.

  [1] pose objects per rider (part file id, bones / morph keys, palette offset, layers) from RAM;
  [2] head-part bone poses: the model's part_pose (afl clip at the layer frames, bind translations scaled by the
      profile scale from the .mpf) vs the pose outputs pose+0x000 / +0x108;
  [3] head-part palettes: attach_fk from the RAM parent palettes vs the RAM palette (cBoarder+0x4AE0);
  [4] face pose object: layers, sampled flag +0x288, weights;
  [5] idle selector 0x11D4E8 vs every channel-2 id change of the live traces (frames.bin).
usage: python scripts/face_secondary_validate.py
"""
import collections
import json
import math
import os
import struct
import sys

import numpy as np

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(HERE)
sys.path.insert(0, HERE)
sys.path.insert(0, os.path.join(ROOT, "models"))
import afl_curves as C  # noqa: E402
from afl_ram_check import Ram, block_of, ang, afl_files  # noqa: E402

_BANKS = {}


def locate(ram, rec_ptr):
    """RAM curve-record pointer -> (AflFile, record index); compares the bank header with the files on disk
    (afl_ram_check.locate compares 0x200 bytes, which fails for banks shorter than that)."""
    a = rec_ptr - (rec_ptr % 4)
    for _ in range(0x40000):
        if ram.b[a:a + 2] == b"L":
            n, ot = struct.unpack_from("<HI", ram.b, a + 2)
            if ot == 0x0C + n * 36 and a + 0x0C <= rec_ptr < a + ot:
                if a not in _BANKS:
                    _BANKS[a] = None
                    for name, d in afl_files():
                        k = min(len(d), 0x200)
                        if d[:k] == ram.b[a:a + k]:
                            _BANKS[a] = C.load_afl(name)
                            break
                f = _BANKS[a]
                return f, (rec_ptr - a - 0x0C) // 36
        a -= 4
    return None, None
import face_secondary as M  # noqa: E402

sys.path.insert(0, ROOT)
import traces  # noqa: E402
LIVE = os.path.join(str(traces.root() or "."), "live")
SESSIONS = ("ride_session_1", "ride_session_2", "smoke_test")
RIDER_IDS = ["eddie", "kaori", "luther", "mac", "moby", "zoe", "jp", "elise", "psymon", "seeiah", "brodi", "marisol"]
POSE, PALETTE = 0x7C0, 0x4AE0
RS = 0xB50


def mpf_part_bones(rider, file_id):
    sys.path.insert(0, os.path.join(C.REBUILD, "tools"))
    import mpf
    for part in ("body", "head"):
        for m in mpf.load(os.path.join(C.MDL_DIR, f"{rider}_{part}.mpf")):
            if m.file_id == file_id and not m.is_shadow and m.bones:
                return m.bones
    return None


def ram_poses(ram, cb):
    out = []
    for i in range(ram.i32(cb + 0x7BC)):
        p = cb + POSE + i * 0x394
        out.append(dict(addr=p, part=ram.i32(p + 0x384), n=ram.i32(p + 0x38C), shift=ram.i32(p + 0x390),
                        bones=ram.u32(p + 0x388), layers=ram.i32(p + 0x28C), sampled=ram.i32(p + 0x288),
                        scale=ram.f32(p + 0x380)))
    return out


def ram_layers(ram, pose):
    out = []
    for L in range(ram.i32(pose["addr"] + 0x28C)):
        p = pose["addr"]
        f, rec = locate(ram, ram.u32(ram.u32(p + 0x290 + 4 * L) + 8))
        an, blk = block_of(f, rec) if f else (None, None)
        out.append(dict(anim=an.name if an else "?", block=blk, frame=ram.f32(p + 0x2B8 + 4 * L),
                        weight=ram.f32(p + 0x2E0 + 4 * L), yaw=ram.f32(p + 0x308 + 4 * L),
                        mirror=ram.i32(p + 0x330 + 4 * L) != 0, mask=ram.u32(p + 0x358 + 4 * L)))
    return out


def palette(ram, cb, i):
    return ram.fv(cb + PALETTE + 64 * i, 16).reshape(4, 4)     # row-vector, as stored


def check_snapshots():
    stats = collections.Counter()
    worst = dict(pose_r=0.0, pose_t=0.0, pal_r=0.0, pal_t=0.0, scale_t=0.0)
    table = {}
    for s in SESSIONS:
        meta = json.load(open(os.path.join(LIVE, s, "meta.json")))
        ram = Ram(os.path.join(LIVE, s, "spawn_ram.bin"))
        for r in meta["riders"]:
            cb = int(r, 16)
            prof = ram.u32(cb + 0x464)
            ch = ram.i32(prof + 0x3C)
            scale = ram.f32(prof + 0x60)
            rider = RIDER_IDS[ch]
            poses = ram_poses(ram, cb)
            table[rider] = [(p["part"], p["n"], p["shift"], p["bones"] != 0, p["layers"]) for p in poses]
            pals = {}
            for p in poses:
                if p["bones"]:
                    pals[p["part"]] = [palette(ram, cb, p["shift"] + i) for i in range(p["n"])]
            for p in poses:
                if p["part"] in (0, 1):
                    continue
                if not p["bones"]:                                   # [4] face
                    stats["face_objects"] += 1
                    w = ram.fv(p["addr"] + 0x210, p["n"])
                    stats["face_no_layer"] += p["layers"] == 0
                    stats["face_unsampled"] += p["sampled"] == 0
                    stats["face_zero_weights"] += bool(np.all(w == 0.0))
                    continue
                bones = ram.bones(p["bones"], p["n"])
                mb = mpf_part_bones(rider, p["part"])
                # bind translations in RAM = .mpf x profile scale (0x2054F0)
                for b, m in zip(bones, mb):
                    worst["scale_t"] = max(worst["scale_t"], float(np.abs(b.translation - np.asarray(m.translation) * scale).max()))
                lays = ram_layers(ram, p)
                layers = [(L["block"], L["frame"], L["weight"], L["mirror"], L["yaw"], L["mask"]) for L in lays if L["block"]]
                stats["part_layers"] += len(layers)
                er, et = M.part_pose(layers, mb, scale, p["shift"])
                E = ram.fv(p["addr"], 3 * p["n"]).reshape(-1, 3)
                T = ram.fv(p["addr"] + 0x108, 3 * p["n"]).reshape(-1, 3)
                for i in range(p["n"]):
                    worst["pose_r"] = max(worst["pose_r"], ang(C.euler_matrix(E[i]), C.euler_matrix(er[i])))
                    worst["pose_t"] = max(worst["pose_t"], float(np.linalg.norm(T[i] - et[i])))
                stats["parts"] += 1
                stats["animated_parts"] += bool(layers)
                # [3] palette from the RAM parent palettes and the RAM pose. The part palettes are written by the
                # render path (0x1364D0 -> 0x208400) only for riders drawn this frame; a culled rider keeps an old
                # one (seen as the eyes far from the head bone), so only drawn riders are compared.
                eyes = pals.get(5)
                if eyes is None or np.linalg.norm(eyes[0][3, :3] - pals[0][4][3, :3]) > 50.0:
                    stats["parts_not_drawn"] += 1
                    continue
                fk = M.attach_fk(mb, E, T, pals, p["part"])
                for i in range(p["n"]):
                    R = pals[p["part"]][i]
                    worst["pal_r"] = max(worst["pal_r"], ang(R[:3, :3], fk[i][:3, :3]))
                    worst["pal_t"] = max(worst["pal_t"], float(np.linalg.norm(R[3, :3] - fk[i][3, :3])))
                    stats["part_bones"] += 1
    return stats, worst, table


def check_selector():
    res = collections.Counter()
    for s in ("ride_session_1", "ride_session_2"):
        meta = json.load(open(os.path.join(LIVE, s, "meta.json")))
        ram = open(os.path.join(LIVE, s, "spawn_ram.bin"), "rb").read()
        chars = [struct.unpack_from("<i", ram, struct.unpack_from("<I", ram, int(r, 16) + 0x464)[0] + 0x3C)[0]
                 for r in meta["riders"]]
        B = open(os.path.join(LIVE, s, "frames.bin"), "rb").read()
        n = len(B) // (8 + 6 * RS)
        fr = lambda fi: struct.unpack_from("<I", B, fi * (8 + 6 * RS))[0]
        for ri in range(6):
            def g(fi, o, fmt="<i"):
                oo = o if o < 0x600 else (0x600 + o - 0x4600 if o < 0x4B00 else 0xB00 + o - 0x57B0)
                return struct.unpack_from(fmt, B, fi * (8 + 6 * RS) + 8 + ri * RS + oo)[0]

            def pred(q):
                v4 = lambda o: [g(q, o + 4 * k, "<f") for k in range(4)]
                return M.hair_idle_id(v4(0x150), v4(0x4AA0), v4(0x4AB0), v4(0x4AC0), chars[ri], g(q, 0x46C8, "<I"))

            for fi in range(2, n):
                a, p = g(fi, 0x46D0, "<I"), g(fi - 1, 0x46D0, "<I")
                F = fr(fi)
                if a == p or fr(fi - 1) != F - 1:
                    continue
                # the poll reads the counter before the blocks: a change shows at F % 18 == 0 or (one poll late) 1
                q = fi if F % 18 == 0 else fi - 1
                res["changes"] += 1
                res["mod_ok"] += F % 18 in (0, 1)
                res["id_ok"] += pred(q) == a
    return res


def main():
    stats, worst, table = check_snapshots()
    print("[1] pose objects (part, n bones/keys, palette offset, has bones, layers):")
    for rider, rows in sorted(table.items()):
        print(f"    {rider:8s} {rows}")
    print(f"[2] part bind translations = .mpf x profile scale: max err {worst['scale_t']:.2e} cm")
    print(f"[2] part poses: {stats['parts']} parts ({stats['animated_parts']} animated, {stats['part_layers']} layers): "
          f"max rot err {worst['pose_r']:.2e} deg, max trans err {worst['pose_t']:.2e} cm")
    print(f"[3] attach FK palettes ({stats['parts_not_drawn']} parts of culled riders skipped): {stats['part_bones']} bones: max rot err {worst['pal_r']:.2e} deg, "
          f"max trans err {worst['pal_t']:.2e} cm")
    print(f"[4] face pose objects: {stats['face_objects']}, no layer {stats['face_no_layer']}, "
          f"+0x288 == 0 {stats['face_unsampled']}, all weights 0 {stats['face_zero_weights']}")
    sel = check_selector()
    print(f"[5] idle selector: {sel['id_ok']}/{sel['changes']} channel-2 changes predicted, "
          f"{sel['mod_ok']}/{sel['changes']} at race frame % 18 in (0, 1)")
    ok = (worst["pose_r"] < 1e-3 and worst["pose_t"] < 1e-3 and worst["pal_r"] < 0.01 and worst["pal_t"] < 0.01
          and stats["face_no_layer"] == stats["face_objects"] and sel["mod_ok"] == sel["changes"]
          and sel["id_ok"] >= sel["changes"] - 15)
    print("ALL OK" if ok else "CHECK FAILED")
    return 0 if ok else 1


if __name__ == "__main__":
    sys.exit(main())
