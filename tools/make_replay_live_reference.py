"""Live trace of the original replaying the user's replay REP0001 -> reference rows for SSX.Replay.LiveTrace.
Usage:  python tools/make_replay_live_reference.py
Reads   SSX_TRACES/live/replay_rep0001/frames.bin (PCSX2 recording: per frame u32 race frame, u32 rider
        count, then per rider the cBoarder blocks (0x0000, 0x600), (0x4600, 0x500), (0x57B0, 0x50); riders in cBoarder
        order = racer index, racer 4 = the human "JOE" at 0xdd8cc0)
"""
import libpath  # noqa: F401,E402  (tools/lib on sys.path)
import traces  # noqa: E402

traces.require()
TR = traces.root()

import struct
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
TRACE = TR / "live" / "replay_rep0001" / "frames.bin"
OUT = ROOT / "Unreal" / "SSXTrickyRebuild" / "SimData" / "replay_rep0001.live.csv"
BLOCKS = [(0x0000, 0x600), (0x4600, 0x500), (0x57B0, 0x50)]
REC_RIDER = sum(b[1] for b in BLOCKS)
FRAME_OFFSET = 127
WINDOWS = {4: (150, 715), 3: (150, 730), 1: (150, 700), 5: (150, 805), 2: (150, 800), 0: (150, 700)}
ANIM_OUT = OUT.parent / "replay_rep0001.anim.csv"
ANIM_WINDOW = (0, 240)


def load(path):
    data = path.read_bytes()
    riders = struct.unpack_from("<I", data, 4)[0]
    size = 8 + riders * REC_RIDER
    frames = {}
    for off in range(0, len(data) - size + 1, size):
        counter = struct.unpack_from("<I", data, off)[0]
        frames[counter - FRAME_OFFSET] = [data[off + 8 + i * REC_RIDER: off + 8 + (i + 1) * REC_RIDER] for i in range(riders)]
    return frames


def state(rec):
    p = struct.unpack_from("<3f", rec, 0x140)
    v = struct.unpack_from("<3f", rec, 0x150)
    m, c = struct.unpack_from("<2i", rec, 0x424)
    return p, v, m, c


def norm(a):
    return sum(x * x for x in a) ** 0.5


def sub(a, b):
    return [x - y for x, y in zip(a, b)]


def clean(frames, racer, f):
    """One-frame position steps to both neighbours and a smooth velocity change (the sample is not torn)."""
    if f - 1 not in frames or f + 1 not in frames:
        return False
    p0, v0, _, _ = state(frames[f - 1][racer])
    p1, v1, _, _ = state(frames[f][racer])
    p2, v2, _, _ = state(frames[f + 1][racer])
    s0, s1 = norm(v0) / 60.0, norm(v1) / 60.0
    if s0 <= 0.5:
        return True
    if abs(norm(sub(p1, p0)) / s0 - 1.0) > 0.1 or abs(norm(sub(p2, p1)) / max(s1, 1e-9) - 1.0) > 0.1:
        return False
    d1, d2 = sub(v1, v0), sub(v2, v1)
    if norm(sub(d2, d1)) > 0.35 * (norm(d1) + norm(d2)) + 0.05:
        return False
    m = state(frames[f][racer])[2]
    if m == 2:
        est = [x * 60.0 for x in sub(p2, p1)]
    elif m == 1:
        est = [x * 30.0 for x in sub(p2, p0)]
    else:
        return True
    return norm(sub(est, v1)) <= 1.0


def main():
    if not TRACE.exists():
        raise SystemExit(f"missing {TRACE}")
    frames = load(TRACE)
    rows = ["racer,frame,px,py,pz,vx,vy,vz,motion,control"]
    for racer, (a, b) in WINDOWS.items():
        kept = 0
        for f in range(a, b + 1):
            if f in frames and clean(frames, racer, f):
                p, v, m, c = state(frames[f][racer])
                rows.append(f"{racer},{f},{p[0]:.9g},{p[1]:.9g},{p[2]:.9g},{v[0]:.9g},{v[1]:.9g},{v[2]:.9g},{m},{c}")
                kept += 1
        print(f"racer {racer}: {kept} clean samples in frames {a}..{b}")
    OUT.parent.mkdir(parents=True, exist_ok=True)
    OUT.write_text("\n".join(rows) + "\n")
    print(f"wrote {OUT}")
    # 0x160AF0
    rows = ["racer,frame,motion,control,gate,ch0,ch1,ch2,mirror,yaw," + ",".join(f"board{k}" for k in range(12))
            + "," + ",".join(f"root{k}" for k in range(12)) + "," + ",".join(f"hips{k}" for k in range(6))]
    for f in range(ANIM_WINDOW[0], ANIM_WINDOW[1] + 1):
        for racer, rec in enumerate(frames.get(f, [])):
            m, c = struct.unpack_from("<2i", rec, 0x424)
            g = struct.unpack_from("<f", rec, 0x238)[0]
            o = 0x600 + 0x46C8 - 0x4600
            ids = struct.unpack_from("<3I", rec, o)
            mirror = struct.unpack_from("<I", rec, o + 0xC)[0]
            yaw = struct.unpack_from("<f", rec, o + 0x18)[0]
            mats = []
            for base, nrows in ((0x4A60, 4), (0x4AA0, 4), (0x4AE0, 2)):
                for r in range(nrows):
                    mats += struct.unpack_from("<3f", rec, 0x600 + base - 0x4600 + 16 * r)
            rows.append(f"{racer},{f},{m},{c},{g:.9g},{ids[0]},{ids[1]},{ids[2]},{mirror},{yaw:.9g},"
                        + ",".join(f"{x:.9g}" for x in mats))
    ANIM_OUT.write_text("\n".join(rows) + "\n")
    print(f"wrote {ANIM_OUT}")


if __name__ == "__main__":
    main()
