"""Wipe-out sequences of the live REP0001 traces -> the free-running reference for SSX.WipeOut.LiveSequence.
Usage:  python tools/make_wipeout_sequence_reference.py
Reads   SSX_TRACES/live/replay_rep0001 and replay_rep0001_crash (PCSX2 recording; the original replaying the
        user's REP0001 on Snowdream; the crash trace appends the sphere set, the pose object and the wipe-out handler block)
Writes  Unreal/SSXTrickyRebuild/SimData/replay_rep0001.wipeout_seq.bin (git-ignored derived data): 'WSQ2', u32 raw size, then
"""
import libpath  # noqa: F401,E402  (tools/lib on sys.path)
import traces  # noqa: E402

traces.require()
TR = traces.root()

import json
import struct
import zlib
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
TRACES = TR / "live"
RUNS = [("replay_rep0001", 0xB50), ("replay_rep0001_crash", 4536)]
OUT = ROOT / "Unreal" / "SSXTrickyRebuild" / "SimData" / "replay_rep0001.wipeout_seq.bin"
REC = 0xB50
FRAME_OFFSET = 127
RIDING_AFTER = 300
SET_POS = 2896


def load(path, size_rider):
    data = path.read_bytes()
    riders = struct.unpack_from("<I", data, 4)[0]
    size = 8 + riders * size_rider
    frames = {}
    for off in range(0, len(data) - size + 1, size):
        counter = struct.unpack_from("<I", data, off)[0]
        frames[counter - FRAME_OFFSET] = [data[off + 8 + i * size_rider: off + 8 + (i + 1) * size_rider] for i in range(riders)]
    return frames, riders


def vec(rec, off):
    return struct.unpack_from("<3f", rec, off)


def dist(a, b):
    return sum((x - y) ** 2 for x, y in zip(a, b)) ** 0.5


def motion(rec):
    return struct.unpack_from("<i", rec, 0x424)[0]


def clean(frames, r, f):
    """As make_onestep_live_reference.clean, without the step-ratio test for a rider at rest (a wipe-out slide / get-up can
    stand still): a resting rider's sample is whole when it differs from neither neighbour's timers."""
    if f - 1 not in frames or f + 1 not in frames:
        return False
    a, b = frames[f - 1][r], frames[f][r]
    if a[0x140:0x14C] == b[0x140:0x14C]:
        return False
    step = dist(vec(a, 0x150), (0, 0, 0)) / 60.0 * struct.unpack_from("<f", a, 0x12C)[0]
    if step < 0.5:
        return False
    org = 0x600 + 0x4A90 - 0x4600
    rp = dist(vec(b, 0x140), vec(a, 0x140)) / step
    ro = dist(vec(b, org), vec(a, org)) / step
    if not (0.7 < rp < 1.4 and 0.7 < ro < 1.4):
        return False
    if motion(b) in (2, 3):
        if b[0x170:0x180] == a[0x170:0x180]:
            return False
        if dist(vec(b, 0x150), (0, 0, 0)) > struct.unpack_from("<f", b, 0x1C4)[0] * (1.0 + 2e-6):
            return False
    return True


def pose_lags(rec):
    p, o, v = vec(rec, 0x140), vec(rec, 0x600 + 0x4A90 - 0x4600), vec(rec, 0x150)
    sp = dist(v, (0, 0, 0))
    if sp == 0.0:
        return False
    s = sum((a - b) * c for a, b, c in zip(p, o, v)) / sp
    return s > 0.4 * sp / 60.0 * struct.unpack_from("<f", rec, 0x12C)[0]


def set_lags(rec):
    p, b, v = vec(rec, 0x140), vec(rec, 0x480), vec(rec, 0x150)
    sp = dist(v, (0, 0, 0))
    if sp == 0.0:
        return False
    s = sum((a - c) * d for a, c, d in zip(p, b, v)) / sp
    return s > 10.0 + 0.5 * sp / 60.0 * struct.unpack_from("<f", rec, 0x12C)[0]


def main():
    traces = [load(TRACES / name / "frames.bin", size) for name, size in RUNS]
    meta = json.loads((TRACES / RUNS[1][0] / "meta.json").read_text())
    hpos = next(x["record_pos"] for x in meta["record_layout"] if x["base"] == "handler")
    riders = traces[0][1]
    crashes = []
    for ti in (1, 0):
        frames = traces[ti][0]
        fs = sorted(frames)
        for r in range(riders):
            for i in range(1, len(fs)):
                if motion(frames[fs[i]][r]) == 5 and motion(frames[fs[i - 1]][r]) != 5:
                    e = fs[i]
                    if ti == 0 and any(c[1] == r and abs(c[2] - e) <= 3 for c in crashes):
                        continue
                    crashes.append((ti, r, e))
    out = bytearray(struct.pack("<I", len(crashes)))
    others = {}
    for ti, r, e in sorted(crashes, key=lambda c: (c[1], c[2])):
        frames = traces[ti][0]
        t = e - 1
        m0 = motion(frames[t][r]) if t in frames else -1
        while t - 1 in frames and t > e - 240 and motion(frames[t - 1][r]) == m0:
            t -= 1
        t -= 1
        while t > e - 250 and not clean(frames, r, t):
            t -= 1
        last, f, riding = t, e, None
        while f in frames and f < e + 2400:
            m = motion(frames[f][r])
            if riding is None and m in (1, 2, 3) and f > e + 2:
                riding = f
            last = f
            if riding is not None and f >= riding + RIDING_AFTER:
                break
            f += 1
        out += struct.pack("<6I", ti, r, e, t, last, riders)
        for j in range(riders):
            a, b = frames[t][j], frames.get(t + 1, frames[t])[j]
            ok = clean(frames, j, t)
            flags = (1 if ok else 0) | (2 if pose_lags(a[:REC]) else 0) | (4 if clean(frames, j, t + 1) and pose_lags(b[:REC]) else 0)
            if ti == 1:
                flags |= 8 | (16 if set_lags(a) else 0)
                sset = a[0x470:0x600] + a[SET_POS:SET_POS + 0xE0]
            else:
                sset = bytes(0x270)
            out += struct.pack("<I", flags) + a[:REC] + b[:REC] + sset
        for g in range(t + 1, last + 1):
            rec = frames[g][r] if g in frames else bytes(len(frames[t][r]))
            h = rec[hpos:hpos + 0x110] if ti == 1 and g in frames else bytes(0x110)
            nxt = frames.get(g + 1, frames.get(g, {}))
            flags = (1 if clean(frames, r, g) else 0) | (2 if pose_lags(rec[:REC]) else 0)
            flags |= 4 if g + 1 in frames and clean(frames, r, g + 1) and pose_lags(frames[g + 1][r][:REC]) else 0
            sset, cblk = bytes(0x270), bytes(0x558)
            if ti == 1 and g in frames:
                cblk = rec[SET_POS:SET_POS + 0x558]
                flags |= 8 | (16 if set_lags(rec) else 0)
                sset = rec[0x470:0x600] + rec[SET_POS:SET_POS + 0xE0]
            out += struct.pack("<2I", g, flags) + rec[:REC] + h + sset + cblk
        for g in range(t, last + 1):
            others.setdefault((ti, g), None)
        print(f"trace {ti} racer {r}: entry f{e} force f{t} window to f{last}")
    keys = [k for k in sorted(others) if k[1] in traces[k[0]][0]]
    out += struct.pack("<I", len(keys))
    for ti, g in keys:
        frames = traces[ti][0]
        out += struct.pack("<2I", ti, g)
        for j in range(riders):
            a = frames[g][j]
            flags = (1 if clean(frames, j, g) else 0) | (2 if pose_lags(a[:REC]) else 0)
            flags |= 4 if g + 1 in frames and clean(frames, j, g + 1) and pose_lags(frames[g + 1][j][:REC]) else 0
            sset = bytes(0x270)
            if ti == 1:
                flags |= 8 | (16 if set_lags(a) else 0)
                sset = a[0x470:0x600] + a[SET_POS:SET_POS + 0xE0]
            nxt = frames.get(g + 1, frames[g])[j]
            board = (nxt if flags & 4 else a)[0x660:0x6A0]
            out += struct.pack("<I", flags) + a[:0x460] + board + sset
    OUT.parent.mkdir(parents=True, exist_ok=True)
    OUT.write_bytes(b"WSQ2" + struct.pack("<I", len(out)) + zlib.compress(bytes(out), 6))
    print(f"wrote {OUT} ({len(crashes)} crashes, {len(keys)} other-racer frames, {OUT.stat().st_size // 1024} KiB)")


if __name__ == "__main__":
    main()
