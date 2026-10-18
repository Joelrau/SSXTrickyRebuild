"""Reference cases for SSX.World.LightmapBlend.
Usage: python tools/make_lightmap_reference.py
"""
import libpath  # noqa: F401,E402  (tools/lib on sys.path)
import traces  # noqa: E402

traces.require()
TR = traces.root()

import csv
import random
import struct
import sys
from pathlib import Path

import numpy as np

ROOT = Path(__file__).resolve().parent.parent
import look  # noqa: E402

SIMDATA = ROOT / "Unreal" / "SSXTrickyRebuild" / "SimData"


def blend_cases():
    d = open(ROOT / "unpacked/MODELS/GARI/data/models/gari_L.ssh", "rb").read()
    off = struct.unpack_from("<I", d, 16 + 4)[0]
    page = d[off + 16: off + 16 + 128 * 128 * 4]
    rng = random.Random(19)
    rows = []
    for _ in range(400):
        k = rng.randrange(128 * 128) * 4
        lm = tuple(page[k:k + 4])
        base = tuple(rng.randrange(256) for _ in range(3))
        out = look.terrain_pixel((*base, 128), lm, (0, 0, 0))
        rows.append([*base, *lm, *(int(v) for v in out)])
    return rows


def clamp_cases():
    rows = []
    for s in ("ride_session_1", "ride_session_2"):
        raw = open(TR / "live" / s / "spawn_ram.bin", "rb").read()
        q = np.frombuffer(raw, dtype="<u8")
        for i in np.nonzero(q[1::2] == 9)[0] * 2:
            v = int(q[i])
            if v & 0xF != 0xA or (int(q[i - 8]) & 0xFFFFFFFF) != 0x30000015:
                continue
            rect = struct.unpack_from("<4f", raw, (int(q[i - 8]) >> 32) & 0x1FFFFFF)
            got = ((v >> 4) & 0x3FF, (v >> 14) & 0x3FF, (v >> 24) & 0x3FF, (v >> 34) & 0x3FF)
            rows.append([s, *(repr(float(x)) for x in rect), *got])
    return rows


def main():
    b = blend_cases()
    with open(SIMDATA / "lightmap_blend_reference.csv", "w", newline="") as fh:
        w = csv.writer(fh)
        w.writerow(["base_r", "base_g", "base_b", "lm_r", "lm_g", "lm_b", "lm_a", "out_r", "out_g", "out_b"])
        w.writerows(b)
    c = clamp_cases()
    with open(SIMDATA / "lightmap_clamp_reference.csv", "w", newline="") as fh:
        w = csv.writer(fh)
        w.writerow(["session", "x", "y", "w", "h", "minu", "maxu", "minv", "maxv"])
        w.writerows(c)
    print(f"{len(b)} blend cases, {len(c)} clamp packets -> {SIMDATA}")


if __name__ == "__main__":
    main()
