"""Live check of models/instance_collision.py (PropCollision query + response) against
traces/live/ride_session_1|2 (Garibaldi, race mode).

  python scripts/instance_live_check.py [ride_session_1] [--frames N]

Per recorded rider frame (post-update state), the rider sphere set (cBoarder+0x470; bound sphere and the first
11 sub-spheres fit in the recorded block 0..0x600) with the mask live when PropCollision runs for the recorded
motion (instance_collision.MOTION_MASK) is put through prop_query (unsurfaced collidable instances, .ltg visit
order, first hit).  Contacts are grouped by the instance's reaction class:
  none   = no flag 0x80 (trigger boxes etc.): handler only, no push;
  bump   = flag 0x80, record mass 0: bump animation only, no push;
  bounce = flag 0x80, mass != 0, no movable controller: push 1.1 depth + v_n -> max(e a, 55.56).
For every contact the next frame is predicted from the model (prop_response) and compared with the recording:
  push seen  = (p[f+1] - p[f] - v[f]/60) . n  (a bounce pushes ~1.1 depth along n),
  v_n after  = v[f+1] . n.
Garibaldi has no surfaced instance, so the main world query never sees a prop there.
"""
import json
import struct
import sys
from collections import defaultdict
from pathlib import Path

import numpy as np

HERE = Path(__file__).resolve().parent
sys.path.insert(0, str(HERE.parent / "models"))
import collision as col  # noqa: E402
import instance_collision as ic  # noqa: E402

TRACK = HERE.parents[2] / "unpacked" / "MODELS" / "GARI" / "data" / "models" / "gari"
REC = 1536 + 1280 + 80
N_SUB = (1536 - 0x4A0) // 0x20


def shape_at(b, o, mask):
    c = np.array(struct.unpack_from("<3f", b, o + 0x480), float)
    r, = struct.unpack_from("<f", b, o + 0x490)
    count, = struct.unpack_from("<i", b, o + 0x498)
    subs = []
    for k in range(min(count, N_SUB)):
        q = o + 0x4A0 + 0x20 * k
        subs.append((np.array(struct.unpack_from("<3f", b, q), float), struct.unpack_from("<f", b, q + 0x10)[0]))
    return col.SphereSet(c, r, subs, mask)


def klass(inst):
    if not inst.reacts:
        return "none"
    return "bump" if inst.mass == 0.0 else "bounce"


def main():
    sess = next((a for a in sys.argv[1:] if not a.startswith("--")), "ride_session_1")
    sys.path.insert(0, str(HERE.parent))
    import traces
    S = traces.path("live", sess)
    meta = json.loads((S / "meta.json").read_text())
    riders = meta["riders"]
    fr = 8 + len(riders) * REC
    raw = np.memmap(S / "frames.bin", dtype=np.uint8, mode="r")
    n = len(raw) // fr
    if "--frames" in sys.argv:
        n = min(n, int(sys.argv[sys.argv.index("--frames") + 1]))
    insts, ltg = ic.load_track(TRACK, keep_ltg=True)
    world = ic.InstanceWorld(insts, alternate_mode=False, ltg=ltg)
    stats = defaultdict(lambda: dict(frames=0, depth=[], push=[], vn=[], pred=[], types=defaultdict(int)))
    run = {}
    for f in range(n - 1):
        b = bytes(raw[f * fr:(f + 1) * fr])
        nb = None
        for k, name in enumerate(riders):
            o = 8 + k * REC
            motion, control = struct.unpack_from("<2i", b, o + 0x424)
            if motion not in ic.MOTION_MASK:
                continue
            sh = shape_at(b, o, ic.MOTION_MASK[motion])
            if not (sh.radius > 0):
                continue
            hit = world.prop_query(sh)
            if hit is None:
                run.pop(name, None)
                continue
            inst = world.by_index[hit.cell[0]]
            st = stats[klass(inst)]
            st["frames"] += 1
            st["depth"].append(hit.t)
            st["types"][inst.mtype] += 1
            if nb is None:
                nb = bytes(raw[(f + 1) * fr:(f + 2) * fr])
            p0 = np.array(struct.unpack_from("<3f", b, o + 0x140))
            v0 = np.array(struct.unpack_from("<3f", b, o + 0x150))
            p1 = np.array(struct.unpack_from("<3f", nb, o + 0x140))
            v1 = np.array(struct.unpack_from("<3f", nb, o + 0x150))
            g = np.array(struct.unpack_from("<3f", b, o + 0x2A0))
            m = np.asarray(hit.normal, float)
            if motion == 2:
                m = m - np.dot(m, g) * g
                m /= np.linalg.norm(m)
            pred = ic.prop_response(inst, hit, v0, motion, control, g)
            st["push"].append(float((p1 - p0 - v0 / 60.0) @ m) / max(hit.t, 1e-6))
            st["vn"].append((float(v0 @ m), float(v1 @ m), float(pred.v @ m)))
            run[name] = run.get(name, 0) + 1
    print(f"{sess}: {n} frames x {len(riders)} riders (prop_query, .ltg order, motion masks)")
    for kname in ("none", "bump", "bounce"):
        st = stats[kname]
        if not st["frames"]:
            print(f"  {kname:6s}: no contact")
            continue
        d = np.array(st["depth"])
        push = np.array(st["push"])
        vn = np.array(st["vn"])
        rise = vn[:, 1] - vn[:, 0]
        print(f"  {kname:6s}: {st['frames']} rider-frames, model types {dict(st['types'])}, depth median "
              f"{np.median(d):.1f} max {d.max():.1f} cm; push seen / depth median {np.median(push):.2f}; "
              f"v.n rise median {np.median(rise):.1f} cm/s, frames with v.n[f+1] >= 55 cm/s: "
              f"{int((vn[:, 1] >= 55.0).sum())}; v.n before median {np.median(vn[:, 0]):.1f}, model-predicted v.n after "
              f"median {np.median(vn[:, 2]):.1f}, recorded {np.median(vn[:, 1]):.1f}")


if __name__ == "__main__":
    main()
