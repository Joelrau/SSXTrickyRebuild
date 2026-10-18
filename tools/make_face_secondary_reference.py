"""Reference data for the SSX.Anim.Face and SSX.Anim.Secondary tests (git-ignored SimData output).
Usage: python tools/make_face_secondary_reference.py
"""
# 0x11D4E8 / 0x2029D8 / 0x1DF478
import libpath  # noqa: F401,E402  (tools/lib on sys.path)
import traces  # noqa: E402

traces.require()
TR = traces.root()

import json
import os
import struct
import sys

import numpy as np

HERE = os.path.dirname(os.path.abspath(__file__))
REPO = os.path.dirname(HERE)
OUT = os.path.join(REPO, "Unreal", "SSXTrickyRebuild", "SimData", "face_secondary_reference.json")

import afl_curves as C  # noqa: E402
import face_secondary as M  # noqa: E402
import face_secondary_validate as V  # noqa: E402
from afl_ram_check import Ram  # noqa: E402

FE_PREFIX = ["ED", "KA", "LU", "MA", "MO", "ZO", "JP", "EL", "PS", "SE", "BR", "MR"]


def selector_cases():
    cases = []
    for s in ("ride_session_1", "ride_session_2"):
        meta = json.load(open(os.path.join(V.LIVE, s, "meta.json")))
        ram = open(os.path.join(V.LIVE, s, "spawn_ram.bin"), "rb").read()
        chars = [struct.unpack_from("<i", ram, struct.unpack_from("<I", ram, int(r, 16) + 0x464)[0] + 0x3C)[0]
                 for r in meta["riders"]]
        B = open(os.path.join(V.LIVE, s, "frames.bin"), "rb").read()
        n = len(B) // (8 + 6 * V.RS)
        for ri in range(6):
            def g(fi, o, fmt="<i"):
                oo = o if o < 0x600 else (0x600 + o - 0x4600 if o < 0x4B00 else 0xB00 + o - 0x57B0)
                return struct.unpack_from(fmt, B, fi * (8 + 6 * V.RS) + 8 + ri * V.RS + oo)[0]
            for fi in range(1, n):
                F = struct.unpack_from("<I", B, fi * (8 + 6 * V.RS))[0]
                change = g(fi, 0x46D0, "<I") != g(fi - 1, 0x46D0, "<I")
                if F % 18 != 0 and not change:
                    continue
                q = fi if (F % 18 == 0 or fi == 0) else fi - 1
                v4 = lambda o: [g(q, o + 4 * k, "<f") for k in range(4)]
                v, X, Y, Z = v4(0x150), v4(0x4AA0), v4(0x4AB0), v4(0x4AC0)
                ch0 = g(q, 0x46C8, "<I")
                case = dict(v=v, x=X, y=Y, z=Z, character=chars[ri], ch0=ch0, frame=F, control=g(q, 0x428),
                            id=M.hair_idle_id(v, X, Y, Z, chars[ri], ch0))
                if change:
                    case["live"] = g(fi, 0x46D0, "<I")
                cases.append(case)
    rng = np.random.default_rng(47)
    for k in range(400):
        sp = float(rng.choice([0, 100, 138.8, 139.0, 500, 833.3, 833.4, 1200, 2500]))
        d = rng.normal(size=3)
        d /= np.linalg.norm(d)
        v = [float(x) for x in d * sp] + [0.0]
        X, Y, Z = [0.0, 1.0, 0.0, 0.0], [1.0, 0.0, 0.0, 0.0], [0.0, 0.0, 1.0, 0.0]
        ch = int(rng.integers(0, 12))
        ch0 = int(rng.choice([0, 0x1FC, 0x1FD, 0x1FE, 0x1FF, 0x223]))
        cases.append(dict(v=v, x=X, y=Y, z=Z, character=ch, ch0=ch0, frame=18 * k, control=3,
                          id=M.hair_idle_id(v, X, Y, Z, ch, ch0)))
    return cases


def ram_parts():
    out = []
    for s in V.SESSIONS:
        meta = json.load(open(os.path.join(V.LIVE, s, "meta.json")))
        ram = Ram(os.path.join(V.LIVE, s, "spawn_ram.bin"))
        for r in meta["riders"]:
            cb = int(r, 16)
            prof = ram.u32(cb + 0x464)
            ch = ram.i32(prof + 0x3C)
            poses = V.ram_poses(ram, cb)
            pals = {p["part"]: [V.palette(ram, cb, p["shift"] + i) for i in range(p["n"])] for p in poses if p["bones"]}
            eyes = pals.get(5)
            drawn = eyes is not None and np.linalg.norm(eyes[0][3, :3] - pals[0][4][3, :3]) < 50.0
            rec = dict(session=s, rider=V.RIDER_IDS[ch], character=ch, scale=ram.f32(prof + 0x60), drawn=bool(drawn),
                       body=[m.reshape(-1).tolist() for m in pals[0]], parts=[],
                       shifts=[[p["part"], p["shift"], p["n"], int(p["bones"] != 0)] for p in poses])
            for p in poses:
                if p["part"] in (0, 1) or not p["bones"]:
                    continue
                lays = V.ram_layers(ram, p)
                rec["parts"].append(dict(
                    file=p["part"], shift=p["shift"],
                    layers=[dict(clip=L["anim"], frame=L["frame"], weight=L["weight"], mirror=L["mirror"],
                                 mask=L["mask"]) for L in lays],
                    euler=ram.fv(p["addr"], 3 * p["n"]).tolist(), trans=ram.fv(p["addr"] + 0x108, 3 * p["n"]).tolist(),
                    palette=[m.reshape(-1).tolist() for m in pals[p["part"]]]))
            out.append(rec)
    return out


def face_cases():
    f = C.load_afl("feanim.afl")
    out = []
    for a in sorted(f.anims.values(), key=lambda a: a.index):
        blk = next((b for b in a.blocks if b.chan == M.PART_FACE), None)
        if blk is None or not a.name.startswith("fe"):
            continue
        for fr in (0.0, 7.5, a.n_frames * 0.5, a.n_frames - 1.0):
            w, sampled = M.face_weights([(blk, fr, 1.0)])
            w2, _ = M.face_weights([(blk, fr, 0.25), (blk, max(0.0, fr - 3.0), 0.75)])
            out.append(dict(clip=a.name, frame=fr, weights=w.tolist(), blend=w2.tolist(),
                            drawn=[(x if M.key_applied(k, x) else 0.0) for k, x in enumerate(w)]))
    return out


def main():
    ref = dict(selector=selector_cases(), ram_parts=ram_parts(), face=face_cases(),
               thresholds=dict(still=M.V_STILL, fast=M.V_FAST, vertical=M.V_VERTICAL, side=M.V_SIDE),
               morph=dict(threshold=M.MORPH_THRESHOLD, cm_per_unit=M.MORPH_VU_SCALE / M.MORPH_ITOF))
    os.makedirs(os.path.dirname(OUT), exist_ok=True)
    with open(OUT, "w") as fh:
        json.dump(ref, fh)
    print(f"wrote {OUT}: {len(ref['selector'])} selector cases, {len(ref['ram_parts'])} RAM riders, "
          f"{len(ref['face'])} face cases")


if __name__ == "__main__":
    main()
