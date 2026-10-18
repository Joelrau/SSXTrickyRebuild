"""Front-end rider data and reference values.
Usage:
    python tools/export_fe_riders.py        # -> Unreal/SSXTrickyRebuild/SimData/fe_riders.json
"""
import libpath  # noqa: F401,E402  (tools/lib on sys.path)
import json
import math
import os
import sys

import numpy as np

HERE = os.path.dirname(os.path.abspath(__file__))
REPO = os.path.dirname(HERE)
OUT = os.path.join(REPO, "Unreal", "SSXTrickyRebuild", "SimData", "fe_riders.json")
sys.path.insert(0, HERE)
import fe_riders as FE  # noqa: E402
import afl  # noqa: E402
import afl_curves as C  # noqa: E402

RIDERS = ["eddie", "kaori", "luther", "mac", "moby", "zoe", "jp", "elise", "psymon", "seeiah", "brodi", "marisol"]
PLATFORMS = ["01", "02", "03", "04", "05", "06", "07", "08", "09", "010", "011", "012"]


def light_json(L):
    return {"ambient": list(L["ambient"]), "azimuth": list(L["azimuth"]), "elevation": list(L["elevation"]),
            "colour": [v for c in L["colour"] for v in c]}


def fk(bones, eul, tr, root):
    out = []
    for i, b in enumerate(bones):
        m = C.local_rows(eul[i], tr[i])
        parent = root if b.parent_file == -1 else out[b.parent_index]
        out.append((m @ parent).astype(np.float32))
    return out


def main():
    inst = FE._platforms()
    p01 = inst["mdl_Cplatform_01"]
    radius, z = math.hypot(p01[0], p01[1]), float(p01[2])
    out = {"ring": {"radius": radius, "z": z},
           "lights": {"unselected": light_json(FE.LIGHTS_UNSELECTED), "selected": light_json(FE.LIGHTS_SELECTED)}}

    place = []
    for slot in range(12):
        m = FE.rider_matrix(slot, radius, z)
        place.append({"slot": slot, "character": FE.CAROUSEL[slot], "rows": m,
                      "platform": [float(v) for v in inst["mdl_Cplatform_" + PLATFORMS[slot]]]})
    out["placement"] = place

    s = FE.FeSession()
    seq = []
    for _load in range(2):
        variant = s.on_fe_load()
        seq.append({"variant": variant, "trans": [s.next_trans() for _ in range(4)]})
    s = FE.FeSession()
    s.on_fe_load()
    r = FE.FeRider(character=3, slot=0, loaded=True)
    r.request(FE.STATE_IDLE); r.update(s, False)
    r.request(FE.STATE_PICKED); r.update(s, False); r.update(s, False); r.update(s, True)
    out["trans"] = {"loads": seq, "mac_log": r.log}

    bank = afl.Bank()
    cases = [(3, 0, FE.ANIM_INIT, 0.0), (3, 0, FE.ANIM_INIT, 10.5), (0, 4, FE.ANIM_INIT, 37.0),
             (10, 3, FE.ANIM_TRANS1 + 2, 20.0), (7, 5, FE.ANIM_CYCLE, 15.5), (6, 9, FE.ANIM_TRANS1 + 1, 30.0)]
    files = {}
    pal = []
    board_bones = C.load_skeleton("brodi", "board")
    for ch, slot, anim_id, frame in cases:
        rec = bank.resolve(anim_id, 0, ch)[0]
        f = files.setdefault(rec.file, C.load_afl(rec.file))
        a = f.anims[rec.index]
        body = C.load_skeleton(RIDERS[ch])
        root = np.asarray(FE.rider_matrix(slot, radius, z), dtype=np.float32)
        blk0 = next(b for b in a.blocks if b.chan == 0)
        blk1 = next(b for b in a.blocks if b.chan == 1)
        e0, t0 = C.sample_block(blk0, body, frame)
        e1, t1 = C.sample_block(blk1, board_bones, frame)
        B = fk(body, e0, t0, root)
        Bd = fk(board_bones, e1, t1, root)
        pal.append({"character": ch, "rider": RIDERS[ch], "slot": slot, "anim": anim_id, "clip": a.name,
                    "frame": frame, "n_frames": a.n_frames, "events": [list(e) for e in rec.events],
                    "body": [m.astype(float).tolist() for m in B], "board": Bd[0].astype(float).tolist()})
    out["palette"] = pal

    b, n = 0.0, 0
    while True:
        b, changed = FE.step_blend(b, True)
        if not changed:
            break
        n += 1
    mid = FE.light_set(7.0 / 15.0)
    out["blend"] = {"frames_0_to_1": n, "mid_t": 7.0 / 15.0,
                    "mid": {"ambient": list(mid["ambient"]), "direction": [list(d) for d in mid["direction"]],
                            "colour": [v for c in mid["colour"] for v in c]}}
    ev = []
    for ch in range(12):
        for t in range(4):
            recs = bank.resolve(FE.ANIM_TRANS1 + t, 0, ch)
            ev.append({"character": ch, "trans": t, "events": [list(e) for e in recs[0].events]})
    out["speech_events"] = ev

    os.makedirs(os.path.dirname(OUT), exist_ok=True)
    with open(OUT, "w") as fh:
        json.dump(out, fh, indent=1)
    print(f"wrote {OUT}: radius {radius:.3f} z {z:.3f}, {len(pal)} palette cases")


if __name__ == "__main__":
    main()
