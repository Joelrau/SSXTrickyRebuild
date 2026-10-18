"""Live Prewind reference for SSX.Pawn.PadDevice.
usage: python tools/make_pad_reference.py
"""
# 0xDD8CC0
import libpath  # noqa: F401,E402  (tools/lib on sys.path)
import traces  # noqa: E402

traces.require()
TR = traces.root()

import os
import struct

HERE = os.path.dirname(os.path.abspath(__file__))
REPO = os.path.dirname(HERE)
OUT = os.path.join(REPO, "Unreal", "SSXTrickyRebuild", "SimData", "pad_live_prewind.csv")
RS = 0xB50


def main():
    rows = []
    for s in (1, 2):
        B = open(os.path.join(TR, "live", f"ride_session_{s}", "frames.bin"), "rb").read()
        n = len(B) // (8 + 6 * RS)
        for fr in range(n):
            base = fr * (8 + 6 * RS) + 8 + 4 * RS
            if struct.unpack_from("<i", B, base + 0x428)[0] != 14:
                continue
            a, b, st = (struct.unpack_from("<f", B, base + o)[0] for o in (0x228, 0x234, 0x21C))
            rows.append(f"{s},{fr},{a:.9g},{b:.9g},{st:.9g}")
    os.makedirs(os.path.dirname(OUT), exist_ok=True)
    with open(OUT, "w", newline="\n") as f:
        f.write("session,frame,prewind_a_target,prewind_b_target,steer_target\n")
        f.write("\n".join(rows) + "\n")
    print(f"{OUT}: {len(rows)} Prewind frames")


if __name__ == "__main__":
    main()
