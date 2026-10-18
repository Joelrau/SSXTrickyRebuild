"""Board-audio reference for SSX.Audio.Board.
usage: python tools/make_board_audio_reference.py
"""
import libpath  # noqa: F401,E402  (tools/lib on sys.path)
import traces  # noqa: E402

traces.require()
TR = traces.root()

import json
import os
import struct
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
REPO = os.path.dirname(HERE)
import board_audio as B  # noqa: E402

SNOW = os.path.join(REPO, "extracted", "DATA", "CONFIG", "SNOW.INF")
OUT = os.path.join(REPO, "Unreal", "SSXTrickyRebuild", "SimData", "board_audio_reference.json")
OUT_CSV = os.path.join(REPO, "Unreal", "SSXTrickyRebuild", "SimData", "board_audio_replay.csv")
RS = 0xB50
CB = {0x21B440: "speed", 0x21B508: "glide", 0x21B628: "aiglide", 0x21B718: "carve"}


def outputs(P, motion, surface, rail_surface, cf, vel, drag, steer):
    inp = B.inputs(cf, vel, drag, steer)
    idx = B.snow_index(surface, motion, rail_surface)
    return list(inp) + [idx] + list(B.cb_speed(motion, inp[0])) + list(B.cb_glide(P, motion, idx, inp)) + \
        list(B.cb_carve(P, motion, idx, inp)) + list(B.cb_aiglide(P, motion, idx, inp))


def main():
    P = B.compile_all(open(SNOW, encoding="latin-1").read())
    ref = {"programs": {f"{s}/{m}": c for (s, m), c in P.items()}}
    rows = []
    for s in (1, 2):
        D = open(os.path.join(TR, "live", f"ride_session_{s}", "frames.bin"), "rb").read()
        n = len(D) // (8 + 6 * RS)
        rail = 0
        prev_motion = [0] * 6
        for fr in range(n):
            for ri in range(6):
                b = fr * (8 + 6 * RS) + 8 + ri * RS
                fl = lambda o: struct.unpack_from("<f", D, b + o)[0]
                it = lambda o: struct.unpack_from("<i", D, b + o)[0]
                motion, surface = it(0x424), it(0x290)
                if motion == 3 and prev_motion[ri] != 3:
                    rail = surface
                prev_motion[ri] = motion
                cf = [fl(0x320 + 4 * k) for k in range(4)]
                vel = [fl(0x150 + 4 * k) for k in range(4)]
                drag, steer = fl(0x160), fl(0x214)
                rows.append([s, fr, ri, int(it(0x41C) == 1), motion, surface, rail] + cf[:3] + vel[:3] + [drag, steer] +
                            outputs(P, motion, surface, rail, cf, vel, drag, steer))
    ref["replay_columns"] = ["session", "frame", "rider", "player", "motion", "surface", "rail_surface", "cfx", "cfy", "cfz",
                             "vx", "vy", "vz", "drag", "steer", "slip", "dig", "lean", "board", "bend", "idx",
                             "speed_vol", "speed_bend", "glide_vol", "glide_bend", "carve_vol", "carve_bend",
                             "ai_vol", "ai_bend"]
    nl = chr(10)
    with open(OUT_CSV, "w", newline=nl) as fc:
        fc.write(",".join(ref.pop("replay_columns")) + nl)
        for r in rows:
            fc.write(",".join(f"{v:.9g}" if isinstance(v, float) else str(v) for v in r) + nl)
    voices = []
    for name, path in (("ride_session_1", "traces/live/ride_session_1/spawn_ram.bin"), ("paused_race_1", "traces/ram/paused_race_1.bin")):
        R = open(os.path.join(TR, path.replace("traces/", "", 1)), "rb").read()
        u = lambda a: struct.unpack_from("<I", R, a)[0]
        f = lambda a: struct.unpack_from("<f", R, a)[0]
        i = lambda a: struct.unpack_from("<i", R, a)[0]
        snd = u(0x343050)
        base = u(snd + 0x8E0)
        for k in range(64):
            r = base + 0xB0 * k
            if not (u(r) and u(r + 0xA0) in CB):
                continue
            b = u(r + 0x80)
            role = CB[u(r + 0xA0)]
            motion, surface, rail = i(b + 0x424), i(b + 0x290), i(snd + 0x6E40)
            cf = [f(b + 0x320 + 4 * j) for j in range(4)]
            vel = [f(b + 0x150 + 4 * j) for j in range(4)]
            o = outputs(P, motion, surface, rail, cf, vel, f(b + 0x160), f(b + 0x214))
            model = {"speed": o[6], "glide": o[8], "carve": o[10], "aiglide": o[12]}[role]
            voices.append({"snapshot": name, "role": role, "player": int(i(b + 0x41C) == 1), "r88": f(r + 0x88),
                           "r8c": f(r + 0x8C), "ram_vol": i(r + 0x64), "model_vol": model, "motion": motion,
                           "surface": surface, "rail_surface": rail, "cf": cf[:3], "vel": vel[:3],
                           "drag": f(b + 0x160), "steer": f(b + 0x214)})
    ref["ram_voices"] = voices
    ref["curves"] = [[c, v, B.curve(c, v)] for c in range(8) for v in range(-50, 4200, 7)]
    ref["falloff"] = [[vol, d / 4.0, r, B.falloff(vol, d / 4.0, r)] for vol in (127, 64, 13) for d in range(0, 240, 3)
                      for r in (40.0, 50.0, 0.0)]
    ref["doppler"] = [[p, d, dt, B.doppler(p, d, dt)] for p, d, dt in ((10.0, 9.0, 1 / 60), (5.0, 30.0, 1 / 60),
                                                                      (20.0, 20.5, 1 / 60), (1.0, 1.0, 1 / 60))]
    ba = []
    for tl in (0.5, 1.5, 1.51, 2.0, 3.0, 5.0, 9.0):
        for te in (0.0, 0.3, 1.0, 2.5):
            for ta in (-0.5, 0.0, 0.4, 1.2):
                for m7 in (0, 1):
                    r = B.big_air(tl, te, ta, bool(m7))
                    ba.append([tl, te, ta, m7, -1 if r is None else r[0], 0.0 if r is None else r[1]])
    ref["big_air"] = ba
    with open(OUT, "w") as fo:
        json.dump(ref, fo, separators=(",", ":"))
    print(f"{OUT}: {len(rows)} replay rows, {len(voices)} RAM voices")


if __name__ == "__main__":
    main()
