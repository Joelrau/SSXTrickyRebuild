"""Live traces of the original replaying REP0001 -> the one-step reference for SSX.Rail.LiveOneStep.
Usage:  python tools/make_onestep_live_reference.py [racer:first:last ...]   (extra windows: investigations only)
Reads   SSX_TRACES/live/replay_rep0001/frames.bin and replay_rep0001_crash/frames.bin (PCSX2 recording;
        per frame u32 race frame, u32 rider count, then per rider the record: the first 0xB50 bytes of both layouts are
        cBoarder 0x0000..0x0600, 0x4600..0x4B00, 0x57B0..0x5800; the crash trace appends its sphere set / pose / wipe-out
"""
import libpath  # noqa: F401,E402  (tools/lib on sys.path)
import traces  # noqa: E402

traces.require()
TR = traces.root()

import json
import struct
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
TRACES = TR / "live"
RUNS = [("replay_rep0001", 0xB50), ("replay_rep0001_crash", 4536)]
OUT = ROOT / "Unreal" / "SSXTrickyRebuild" / "SimData" / "replay_rep0001.onestep.bin"
REC = 0xB50
FRAME_OFFSET = 127
BEFORE, AFTER = 10, 6


def load(path, size_rider, full=False):
    data = path.read_bytes()
    riders = struct.unpack_from("<I", data, 4)[0]
    size = 8 + riders * size_rider
    frames = {}
    for off in range(0, len(data) - size + 1, size):
        counter = struct.unpack_from("<I", data, off)[0]
        frames[counter - FRAME_OFFSET] = [data[off + 8 + i * size_rider: off + 8 + i * size_rider + (size_rider if full else REC)] for i in range(riders)]
    return frames, riders


def vec(rec, off):
    return struct.unpack_from("<3f", rec, off)


def pos(rec):
    return vec(rec, 0x140)


def origin(rec):
    return vec(rec, 0x600 + 0x4A90 - 0x4600)


def dist(a, b):
    return sum((x - y) ** 2 for x, y in zip(a, b)) ** 0.5


def clean(frames, r, f):
    """Sample f is the whole state after update f: the position moved one frame's step from sample f - 1 (a lagged
    sample repeats it, a torn one moved part of a step) and so did the pose block's board origin (the pose of update
    f - 1: the sampler reads the boarder block after the update and the pose before its cPostAI)."""
    if f - 1 not in frames or f + 1 not in frames:
        return False
    a, b = frames[f - 1][r], frames[f][r]
    if pos(a) == pos(b):
        return False
    step = dist(vec(a, 0x150), (0, 0, 0)) / 60.0 * struct.unpack_from("<f", a, 0x12C)[0]
    if step < 0.5:
        return False
    rp = dist(pos(b), pos(a)) / step
    ro = dist(origin(b), origin(a)) / step
    if not (0.7 < rp < 1.4 and 0.7 < ro < 1.4):
        return False
    if struct.unpack_from("<i", b, 0x424)[0] in (2, 3):
        if b[0x170:0x180] == a[0x170:0x180]:
            return False
        if dist(vec(b, 0x150), (0, 0, 0)) > struct.unpack_from("<f", b, 0x1C4)[0] * (1.0 + 2e-6):
            return False
    return True


def pose_lags(rec):
    """The sampler reads the pose block either before the update's cPostAI (it holds the pose of update f - 1, the board
    the next update does NOT read) or after it (the pose of update f). With the position a step ahead of the pose the
    along-track offset (p - O) . v_hat is a frame's step plus the small root offset (-9..0 cm on the REP0001 rails)."""
    p, o, v = pos(rec), origin(rec), vec(rec, 0x150)
    sp = dist(v, (0, 0, 0))
    if sp == 0.0:
        return False
    s = sum((a - b) * c for a, b, c in zip(p, o, v)) / sp
    return s > 0.4 * sp / 60.0 * struct.unpack_from("<f", rec, 0x12C)[0]


def set_lags(rec):
    """The same for the sphere set +0x470 (cPostAI 0x126748; read with the boarder block, before the pose block, so it can
    be the previous update's while the board is already this one's): the bound centre +0x480 sits 11..15 cm behind the
    position along the track when it is this update's, a frame's step more when it is the previous one's."""
    p, b, v = pos(rec), vec(rec, 0x480), vec(rec, 0x150)
    sp = dist(v, (0, 0, 0))
    if sp == 0.0:
        return False
    s = sum((a - c) * d for a, c, d in zip(p, b, v)) / sp
    return s > 10.0 + 0.5 * sp / 60.0 * struct.unpack_from("<f", rec, 0x12C)[0]


def agree(fa, fb, r, f):
    """The two traces replay the same file: a frame whose cBoarder block (+0x1C..+0x460; +0x18 differs between the
    sessions) and board matrix agree in both was read whole in both (the tears fall at different points)."""
    if f not in fa or f not in fb:
        return False
    a, b = fa[f][r], fb[f][r]
    return a[0x1C:0x460] == b[0x1C:0x460] and a[0x660:0x6A0] == b[0x660:0x6A0]


FIELDS = [(0x140, 0x180), (0x2A0, 0x2AC), (0x2D0, 0x2DC), (0x424, 0x42C)]


def compared(fa, fb, r, f):
    """A frame that can be compared but not forced from: the compared fields (p, v, q, rail up, rail point, motion /
    control) agree in both traces and one of them is whole; or the catch, where one trace shows the update torn
    before its catch (same position, still Ground) and the other the caught state (RailSlide), which is used."""
    a, b = fa[f][r], fb[f][r]
    if all(a[x:y] == b[x:y] for x, y in FIELDS) and (clean(fa, r, f) or clean(fb, r, f)):
        return a, True
    if a[0x140:0x14C] == b[0x140:0x14C]:
        ma, mb = struct.unpack_from("<i", a, 0x424)[0], struct.unpack_from("<i", b, 0x424)[0]
        if (ma, mb) == (2, 3) and clean(fb, r, f):
            return b, True
        if (ma, mb) == (3, 2) and clean(fa, r, f):
            return a, True
    return a, False


CRASH_META = TRACES / "replay_rep0001_crash" / "meta.json"
CONTACT_MARGIN = 8


SET_POS = 2896


def sphere_set(crash_rec):
    """The cPostAI sphere set of the crash trace's sample (cBoarder +0x470..+0x6E0: palette / bound / mask / count, 18
    spheres of 0x20 bytes: centre, radius): the set the rider contacts of the next update read."""
    return crash_rec[0x470:0x600] + crash_rec[SET_POS:SET_POS + 0xE0]


def contact_runs(path, size_rider):
    """Rider-rider contacts of the crash trace (it holds every rider's whole cPostAI sphere set): frames where a pair's
    bounding spheres (+0x480, +0x490) overlap and two enabled sub-spheres overlap. Returns
    {(i, j): [(first, last), ...]} by replay frame (runs of contact frames, gaps of 1 merged)."""
    meta = json.loads(CRASH_META.read_text())
    at = {x["name"]: x["record_pos"] for x in meta["crash_fields"]}
    data = path.read_bytes()
    riders = struct.unpack_from("<I", data, 4)[0]
    size = 8 + riders * size_rider
    hits = {}
    for off in range(0, len(data) - size + 1, size):
        f = struct.unpack_from("<I", data, off)[0] - FRAME_OFFSET
        recs = [data[off + 8 + i * size_rider: off + 8 + (i + 1) * size_rider] for i in range(riders)]
        bound = [(vec(x, 0x480), struct.unpack_from("<f", x, 0x490)[0]) for x in recs]
        sets = []
        for x in recs:
            n = min(struct.unpack_from("<i", x, at["set_count"])[0], 18)
            sets.append([(struct.unpack_from("<3f", x, at[f"sph{k}"]), struct.unpack_from("<f", x, at[f"sph{k}_r"])[0]) for k in range(max(n, 0))])
        for i in range(riders):
            for j in range(i + 1, riders):
                (ci, ri), (cj, rj) = bound[i], bound[j]
                if not (ri > 0 and rj > 0 and dist(ci, cj) < ri + rj):
                    continue
                if any(dist(a, b) < ra + rb for a, ra in sets[i] for b, rb in sets[j] if ra > 0 and rb > 0):
                    hits.setdefault((i, j), []).append(f)
    runs = {}
    for k, fs in hits.items():
        out = []
        for f in sorted(fs):
            if out and f - out[-1][1] <= 2:
                out[-1] = (out[-1][0], f)
            else:
                out.append((f, f))
        runs[k] = out
    return runs


def main():
    traces = []
    for name, size_rider in RUNS:
        path = TRACES / name / "frames.bin"
        if not path.exists():
            raise SystemExit(f"missing {path}")
        traces.append(load(path, size_rider))
    (fa, riders), (fb, _) = traces
    fc, _ = load(TRACES / RUNS[1][0] / "frames.bin", RUNS[1][1], full=True)
    frames = {}
    for r in range(riders):
        start = None
        for f in sorted(fa):
            m = struct.unpack_from("<i", fa[f][r], 0x424)[0]
            if m == 3 and start is None:
                start = f
            elif m != 3 and start is not None:
                print(f"racer {r}: rail ride {start}..{f - 1}")
                for g in range(start - BEFORE, f + AFTER):
                    frames.setdefault((r, g), 0)
                start = None
    for arg in sys.argv[1:]:
        r, a, b = (int(x) for x in arg.split(":"))
        for g in range(a, b + 1):
            frames.setdefault((r, g), 0)
    runs = contact_runs(TRACES / RUNS[1][0] / "frames.bin", RUNS[1][1])
    for (i, j), rs in sorted(runs.items()):
        print(f"racers {i} / {j}: contacts {', '.join(f'{a}..{b}' for a, b in rs)}")
        for a, b in rs:
            for r in (i, j):
                for g in range(a - CONTACT_MARGIN, b + CONTACT_MARGIN + 1):
                    frames[(r, g)] = frames.get((r, g), 0) | (8 if a - 1 <= g <= b + 1 else 0)
    entries, n = [], 0
    for (r, f), kind in sorted(frames.items()):
        if f not in fa or f not in fb:
            continue
        ok = agree(fa, fb, r, f) and (clean(fa, r, f) or clean(fb, r, f))
        rec, cmp = fa[f][r], ok
        if not ok:
            rec, cmp = compared(fa, fb, r, f)
        n += ok
        flags = (1 if ok else 0) | (2 if (ok or cmp) and pose_lags(rec) else 0) | (4 if cmp else 0) | kind
        flags |= 16 if set_lags(fc[f][r]) else 0
        entries.append(struct.pack("<4I", 0, f, r, flags) + rec + sphere_set(fc[f][r]))
    OUT.parent.mkdir(parents=True, exist_ok=True)
    OUT.write_bytes(b"RLV1" + struct.pack("<I", len(entries)) + b"".join(entries))
    print(f"wrote {OUT} ({len(entries)} entries, {n} clean)")


if __name__ == "__main__":
    main()
