"""Animation-start reference for SSX.Anim.Bridge (live traces SSX_TRACES/live/ride_session_1/2, channel-0 id at
+0x46C8). Prints "skipped (no traces)" without SSX_TRACES.
usage: python tools/make_anim_bridge_reference.py
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
from slus import u32  # noqa: E402  (the user's SLUS_203.26)

OUT = os.path.join(REPO, "Unreal", "SSXTrickyRebuild", "SimData", "anim_bridge_reference.json")
RS = 0xB50
LATE = {0x22C, 0x228, 0x227, 0x229, 0x22D, 0x22A, 0x226, 0x22B}


def cat(i):
    return 8 if i == 0x300 else u32(0x325FA8 + i * 0x1C)


def main():
    ref = {"takeoff": [], "tumble": [], "recovery": [], "late": []}
    for s in (1, 2):
        B = open(os.path.join(TR, "live", f"ride_session_{s}", "frames.bin"), "rb").read()
        n = len(B) // (8 + 6 * RS)
        for ri in range(6):
            def g(f, o, fmt="<i"):
                oo = o if o < 0x600 else (0x600 + o - 0x4600 if o < 0x4B00 else 0xB00 + o - 0x57B0)
                return struct.unpack_from(fmt, B, f * (8 + 6 * RS) + 8 + ri * RS + oo)[0]
            for fr in range(2, n - 10):
                a, p = g(fr, 0x46C8, "<I"), g(fr - 1, 0x46C8, "<I")
                if g(fr, 0x428) == 0xA and g(fr - 1, 0x428) == 0xE:
                    pa, pb = g(fr - 1, 0x220, "<f"), g(fr - 1, 0x22C, "<f")
                    ta, tb, ra, rb = (g(fr - 1, o, "<f") for o in (0x228, 0x234, 0x224, 0x230))
                    pa += max(-ra / 60, min(ra / 60, ta - pa))
                    pb += max(-rb / 60, min(rb / 60, tb - pb))
                    ref["takeoff"].append([pa, pb, g(fr - 1, 0x1B4), g(fr - 1, 0x1DC), a, g(fr + 1, 0x46C8, "<I")])
                if a != p and 0x2E1 <= a <= 0x2ED and a != 0x2E7:
                    dot = lambda f, o: sum(g(f, o + 4 * k, "<f") * g(f, 0x2A0 + 4 * k, "<f") for k in range(4))
                    ref["tumble"].append([[dot(f, 0x1A0), dot(f, 0x190), dot(f, 0x180)] for f in (fr - 1, fr)] + [cat(p), a])
                if a != p and a in (0x250, 0x251):
                    ref["recovery"].append([cat(p), a])
                if a != p and a in LATE:
                    ds = g(fr + 8, 0x1D0, "<f") - g(fr, 0x1D0, "<f")
                    df = g(fr + 8, 0x1D4, "<f") - g(fr, 0x1D4, "<f")
                    ref["late"].append([ds, df, g(fr, 0x1B4), a])
    with open(OUT, "w") as f:
        json.dump(ref, f)
    print(f"{OUT}: " + ", ".join(f"{k} {len(v)}" for k, v in ref.items()))


if __name__ == "__main__":
    main()
