"""Reference values for the SSX.Anim.* automation tests, from our Python animation model.
Usage:
    python tools/make_anim_reference.py      # -> Unreal/SSXTrickyRebuild/SimData/anim_reference.json
"""
import libpath  # noqa: F401,E402  (tools/lib on sys.path)
import copy
import json
import math
import os
import sys

import numpy as np

HERE = os.path.dirname(os.path.abspath(__file__))
REPO = os.path.dirname(HERE)
OUT = os.path.join(REPO, "Unreal", "SSXTrickyRebuild", "SimData", "anim_reference.json")
sys.path.insert(0, HERE)
import afl_curves as C  # noqa: E402

SETS = ("bxanim.afl", "franim.afl", "exanim.afl", "cmanim.afl", "bxBroUber.afl")


def lst(a):
    return np.asarray(a, dtype=np.float64).tolist()


class Ref:
    def __init__(self):
        self.anims = {}
        for fn in SETS:
            for a in C.load_afl(fn).anims.values():
                self.anims.setdefault(a.name, a)
        self.skel = {}

    def bones(self, rider, part="body"):
        key = (rider, part)
        if key not in self.skel:
            self.skel[key] = C.load_skeleton(rider, part)
        return self.skel[key]

    def n_frames(self, name):
        return self.anims[name].n_frames

    def sample_cases(self):
        out = []
        cases = [("brodi", "bxRL_BASECYCLEFAST", 0.0, False, 0.0, 1.0),
                 ("brodi", "bxRL_BASECYCLEFAST", 23.5, True, -math.pi, 1.0),
                 ("zoe", "exR_TURNHS", 11.430688858, False, 0.0, 1.07),
                 ("mac", "frR_TURNTS", 1.2686446905, True, -math.pi, 0.93),
                 ("brodi", "bxL_TAILBWD", 7.25, False, 0.3, 1.0),
                 ("eddie", "cmI_BUTT", 12.75, True, -math.pi, 1.0),
                 ("brodi", "bxJ_AIRT2S", 15.0, False, 0.0, 1.0),
                 ("kaori", "bxG_GATESTART", 11.923142433, True, -math.pi, 1.0)]
        uber = [n for n in self.anims if self.anims[n].file.name == "bxBroUber.afl"]
        if uber:
            cases.append(("brodi", uber[0], 33.5, False, 0.0, 1.0))
        for rider, name, frame, mirror, yaw, ts in cases:
            a = self.anims[name]
            rec = dict(rider=rider, clip=name, frame=frame, mirror=mirror, yaw=yaw, trans_scale=ts)
            for blk in a.blocks:
                part = {0: "body", 1: "board"}.get(blk.chan)
                if part is None:
                    continue
                e, t = C.sample_block(blk, self.bones(rider, part), frame, mirror=mirror, yaw=yaw, trans_scale=ts)
                rec[part] = dict(euler=lst(e), trans=lst(t))
            out.append(rec)
        return out

    def blend_cases(self):
        out = []
        cases = [("brodi", [("bxR_TURNHS", 0.7184978724, 0.6977327466, True, -math.pi),
                            ("bxRC_TURNHS", 0.7184978724, 0.3022672534, True, -math.pi)]),
                 ("marisol", [("exR_TURNTS", 2.0, 0.3548387290, False, 0.0),
                              ("exR50_TURNTS", 1.9, 0.6451612710, False, 0.0)]),
                 ("psymon", [("bxRL_BASECYCLEFAST", 4.5, 0.4, False, 0.0),
                             ("bxL_TAIL", 9.0, 0.35, False, 0.0),
                             ("bxJ_AIRT2S", 3.0, 0.25, False, 0.0)])]
        for rider, layers in cases:
            body = self.bones(rider)
            L = [(self.anims[n].blocks[0], f, w, m, y) for n, f, w, m, y in layers]
            e, t = C.blend_layers(L, body)
            out.append(dict(rider=rider, layers=[dict(clip=n, frame=f, weight=w, mirror=m, yaw=y)
                                                 for n, f, w, m, y in layers],
                            euler=lst(e), trans=lst(t)))
        return out

    def procedural_cases(self):
        out = []
        base = dict(steer_follow=0.0, brake=0.0, crouch=0.0, goofy=False, air_turn=0.0, air_tilt=0.0,
                    ramp_1f0=0.0, steer=0.0, grind_type=0, pw_x=0.0, pw_y=0.0, gate=0.0)
        cases = []
        for c in range(3):
            cases += [(4, c, {}), (4, c, dict(crouch=0.3)), (4, c, dict(steer_follow=0.62, crouch=0.31)),
                      (4, c, dict(steer_follow=-0.4, crouch=0.7, goofy=True)),
                      (4, c, dict(steer_follow=0.25, brake=0.5, crouch=0.2)),
                      (4, c, dict(steer_follow=-0.8, brake=-0.3, goofy=True)),
                      (8, c, dict(ramp_1f0=0.42)),
                      (9, c, dict()), (9, c, dict(air_turn=0.7, air_tilt=0.2)),
                      (9, c, dict(air_turn=-0.3, air_tilt=0.9, goofy=True)),
                      (9, c, dict(air_turn=-0.6, air_tilt=-0.5)), (9, c, dict(air_turn=0.5, air_tilt=-0.8)),
                      (10, c, dict(crouch=0.4)), (10, c, dict(pw_x=0.6, pw_y=0.3, crouch=0.8)),
                      (10, c, dict(pw_x=-0.5, pw_y=0.5, crouch=0.6, goofy=True)),
                      (10, c, dict(pw_x=-0.2, pw_y=-0.7, crouch=1.0)), (10, c, dict(pw_x=0.3, pw_y=-0.1, crouch=0.5)),
                      (7, c, dict(crouch=0.25)), (7, c, dict(steer=0.5, crouch=0.4)),
                      (7, c, dict(steer=-0.75, crouch=0.1)), (7, c, dict(steer=0.5, grind_type=1))]
        cases += [(4, 2, dict(crouch=1.0)), (4, 2, dict(steer_follow=0.5, crouch=0.75))]
        for mode, c, st in cases:
            s = {**base, **st}
            try:
                r = C.procedural_subs(mode, s, c, self.n_frames)
            except KeyError:
                continue
            subs = [dict(clip=n, frame=None if f is None else float(f), weight=float(w)) for n, f, w in r["subs"]]
            out.append(dict(mode=mode, board_class=c, st=s, subs=subs, clock=bool(r["clock"]),
                            cycle=bool(r.get("cycle", False))))
        return out

    def foot_cases(self):
        out = []
        for rider in ("brodi", "elise"):
            for c in range(3):
                for mirror in (False, True):
                    r = C.foot_rotations(c, mirror, rider)
                    out.append(dict(rider=rider, board_class=c, mirror=mirror, l=lst(r["l_foot"]), r=lst(r["r_foot"])))
        return out

    def palette_cases(self):
        out = []
        cases = [
            dict(rider="brodi", board_class=2, mirror=False, scale=1.0, layers=[("exR_TURNHS", 11.43, 0.355), ("exR50_TURNHS", 11.43, 0.645)],
                 state=dict(quat=(0.92, 0.05, -0.1, 0.37), pos=(5123.5, -2210.25, 6011.0), steer=0.35, motion=2,
                            v2c0=(0.1, -0.2, 0.9747), s308=1.5, v330=(-0.93, -0.36, 0.05), lean=-21.0,
                            spin=0.0, flip=0.0, susp=-3.25, s1cc=0.0, v2a0=(0.1, -0.2, 0.9747))),
            dict(rider="eddie", board_class=0, mirror=True, scale=1.04, layers=[("bxRL_BASECYCLEFAST", 23.5, 1.0)],
                 state=dict(quat=(0.3, -0.02, 0.04, 0.95), pos=(-812.0, 7120.0, 4012.5), steer=0.0, motion=2,
                            v2c0=(0.0, 0.25, 0.968), s308=0.0, v330=(-0.31, -0.95, 0.0), lean=0.0,
                            spin=0.0, flip=0.0, susp=1.75, s1cc=2.0, v2a0=(0.02, 0.24, 0.97))),
            dict(rider="seeiah", board_class=1, mirror=True, scale=0.97, layers=[("frR_TURNTS", 1.27, 0.677), ("frRC_TURNTS", 1.27, 0.323)],
                 state=dict(quat=(0.7071, 0.0, 0.0, -0.7071), pos=(100.0, 200.0, 300.0), steer=-0.6, motion=2,
                            v2c0=(0.0, 0.0, 1.0), s308=0.5, v330=(1.0, 0.0, 0.0), lean=36.0,
                            spin=0.0, flip=0.0, susp=0.0, s1cc=0.0, v2a0=(0.0, 0.0, 1.0))),
            dict(rider="luther", board_class=0, mirror=False, scale=1.0, layers=[("bxJ_AIRT2S", 6.0, 1.0)],
                 state=dict(quat=(0.95, 0.1, 0.2, 0.2), pos=(-50.0, 900.0, 7500.0), steer=0.2, motion=1,
                            v2c0=(0.0, 0.0, 1.0), s308=0.0, v330=(0.0, 1.0, 0.0), lean=-12.0,
                            spin=1.3, flip=-0.6, susp=0.0, s1cc=0.0, v2a0=(0.0, 0.0, 1.0))),
            dict(rider="mac", board_class=1, mirror=False, scale=1.02, layers=[("frG_GATESTART", 14.9, 1.0)],
                 state=dict(quat=(1.0, 0.0, 0.0, 0.0), pos=(0.0, 0.0, 0.0), steer=0.0, motion=4,
                            v2c0=(0.0, 0.0, 1.0), s308=0.0, v330=(0.0, 1.0, 0.0), lean=0.0,
                            spin=0.0, flip=0.0, susp=2.0, s1cc=0.0, v2a0=(0.0, 0.0, 1.0))),
        ]
        for cs in cases:
            rider, c, mir, scale, st = cs["rider"], cs["board_class"], cs["mirror"], cs["scale"], cs["state"]
            body = copy.deepcopy(self.bones(rider))
            for b in body:
                b.translation = np.asarray(b.translation, dtype=np.float64) * scale
            board = self.bones(rider, "board")
            yaw = -math.pi if mir else 0.0
            n = len(body)
            W, R, T = np.zeros(n), np.zeros((n, 3)), np.zeros((n, 3))
            BW, BR, BT = 0.0, np.zeros(3), np.zeros(3)
            for name, fr, w in cs["layers"]:
                a = self.anims[name]
                e, t = C.sample_block(a.blocks[0], body, fr, mirror=mir, yaw=yaw, trans_scale=scale)
                eb, tb = C.sample_block(a.blocks[1], board, fr, mirror=mir, yaw=yaw, trans_scale=scale)
                for i in range(n):
                    if W[i] == 1.0:
                        continue
                    if W[i] == 0.0:
                        W[i], R[i], T[i] = w, e[i], t[i]
                        continue
                    tot = W[i] + w
                    T[i] = T[i] * (W[i] / tot) + t[i] * (w / tot)
                    R[i] = C.euler_blend(R[i], e[i], w / tot)
                    W[i] = tot
                if BW != 1.0:
                    if BW == 0.0:
                        BW, BR, BT = w, eb[0], tb[0]
                    else:
                        tot = BW + w
                        BT = BT * (BW / tot) + tb[0] * (w / tot)
                        BR = C.euler_blend(BR, eb[0], w / tot)
                        BW = tot
            rf = dict(quat=st["quat"], pos=st["pos"], steer=st["steer"], motion=st["motion"], board_class=c,
                      v2c0=st["v2c0"], s308=st["s308"], v330=st["v330"], scale=scale, lean=st["lean"],
                      spin=st["spin"], flip=st["flip"], hips_z=float(T[0][2]), susp=st["susp"])
            root = C.root_frame(**rf)
            root0 = C.root_frame(**{**rf, "ik_branch": False})
            bw = C.board_frame(root0, BR, BT, st["motion"], st["v2c0"], st["s1cc"], st["v2a0"])
            pal = C.build_palette(root, body, R, T, bw, c, mir, scale, rider)
            out.append(dict(rider=rider, board_class=c, mirror=mir, scale=scale, state=st,
                            layers=[dict(clip=nm, frame=f, weight=w) for nm, f, w in cs["layers"]],
                            euler=lst(R), trans=lst(T), board_euler=lst(BR), board_trans=lst(BT),
                            root=lst(root), body=[lst(p) for p in pal], board=lst(bw)))
        return out


def main():
    r = Ref()
    doc = dict(note="game-derived reference values (afl_curves.py); git-ignored, see tools/make_anim_reference.py",
               sample=r.sample_cases(), blend=r.blend_cases(), procedural=r.procedural_cases(),
               foot=r.foot_cases(), palette=r.palette_cases(),
               fade=dict(fade_in=C.fade_in_rate(0.0, 1.0, 7), fade_out=C.fade_out_rate(1.0, 7)))
    os.makedirs(os.path.dirname(OUT), exist_ok=True)
    with open(OUT, "w") as fh:
        json.dump(doc, fh)
    print(f"wrote {OUT}: " + ", ".join(f"{k} {len(v)}" for k, v in doc.items() if isinstance(v, list)))


if __name__ == "__main__":
    main()
