"""Track trigger data decoder (race events, AI events, world floor, SSF effect records).

  python scripts/track_triggers.py <track dir or .aip> [--verify session]

Decodes:
  * .aip/.sop  line-path (kind 1) events = the race triggers the boarder scan reports (finish, lap,
    checkpoint, audio zones, announcer cues, re-pick markers) and AI-path (kind 0) events (take-off
    zones, speed requests, no-reset zones, re-pick markers), with the internal type remap, world
    position and distance-to-go of every event.
  * .ltg header  world bbox (+0x04..+0x18; +0x0C = world floor z), cell size, grid size; the
    fell-out-of-world height = floor - 10000.
  * .ssf  effect records found by structure: controller-node records (main type 0, subtype =
    node class: Boost 7, LapBoost 15, RandomBoost 16, ZBoost 18, TubeEndBoost 24, TrickTrigger 21,
    Rail 9 ...) with the boost fields decoded, and the rider-effect records 13 (forced reset),
    17 (boost burst seconds), 18 (launch boost seconds).
--verify <session> checks the decoded line-path events against the trigger-mask changes recorded in
traces/live/<session> (the distance to go at which each rider's mask bit turned on).
"""
import struct
import sys
from collections import Counter, defaultdict
from pathlib import Path

import numpy as np

HERE = Path(__file__).resolve().parent
sys.path.insert(0, str(HERE.parent / "models"))
sys.path.insert(0, str(HERE))
import race as rc  # noqa: E402

NODE_CLASSES = {0: "Roller", 2: "Debounce", 5: "type5", 6: "Counter", 7: "Boost", 8: "Timer", 9: "Rail",
                10: "UVScroll", 11: "TexFlip", 12: "Fence", 13: "Flag", 14: "Cracked", 15: "LapBoost",
                16: "RandomBoost", 17: "CrowdBox", 18: "ZBoost", 19: "UVScrollTexFlip", 20: "cMeshAnim",
                21: "TrickTrigger", 22: "Particle", 23: "Movie", 24: "TubeEndBoost", 256: "AnimObject",
                257: "AnimDelta", 258: "AnimCombo", 259: "AnimTexFlip"}
EVENT_NAMES = {0: "marker(-1)", 1: "cue0", 2: "zone1", 3: "zone2", 4: "zone3", 5: "zone4", 9: "cue9",
               10: "FINISH", 12: "CHECKPOINT", 13: "LAP", 14: "audio_zone_A", 18: "final_lap_cue",
               19: "cue_0x13", 20: "cue_0x14", 21: "cue_0x15", 22: "cue_0x16", 23: "audio_zone_B",
               24: "audio_zone_C", 25: "AI_jump", 26: "AI_speed", 27: "AI_102", 29: "AI_no_reset",
               30: "AI_105", 31: "repick_marker"}
WORLD_FLOOR_MARGIN = 10000.0


def ltg_header(path):
    d = open(path, "rb").read(64)
    magic, = struct.unpack_from("<I", d, 0)
    bmin = struct.unpack_from("<3f", d, 4)
    bmax = struct.unpack_from("<3f", d, 16)
    cell, = struct.unpack_from("<f", d, 0x28)
    gx, gy, cells = struct.unpack_from("<3I", d, 0x2C)
    return dict(magic=hex(magic), bbox_min=bmin, bbox_max=bmax, world_floor=bmin[2],
                kill_z=bmin[2] - WORLD_FLOOR_MARGIN, cell=cell, grid=(gx, gy), cells=cells)


def list_events(track):
    rows = []
    for p in track.line_paths:
        base = p.dist_to_go_at_start
        for e in p.events:
            rows.append(("line", p.index, EVENT_NAMES.get(e.type, f"type{e.type}"), e.type, e.file_type, e.param,
                         e.start, e.end, base - e.start, p.point_at(e.start)))
    for p in track.ai_paths:
        for e in p.events:
            rows.append(("ai", p.index, EVENT_NAMES.get(e.type, f"type{e.type}"), e.type, e.file_type, e.param,
                         e.start, e.end, None, p.point_at(e.start)))
    return rows


def ssf_records(path):
    """Structural scan of .ssf effect records {main type, byte size, ...}."""
    d = open(path, "rb").read()
    n = len(d) // 4
    w = np.frombuffer(d[:n * 4], "<u4")
    f = np.frombuffer(d[:n * 4], "<f4")
    nodes = []
    for i in np.nonzero((w[:-12] == 0) & (w[1:-11] >= 12) & (w[1:-11] <= 128) & (w[1:-11] % 4 == 0))[0]:
        sub, size = int(w[i + 2]), int(w[i + 1])
        rec = dict(offset=int(i) * 4, cls=NODE_CLASSES.get(sub, sub), size=size)
        if sub == 7 and size == 40:            # Boost: mode, seconds, gain, target m/s, direction
            rec.update(mode=int(w[i + 3] & 0xFFFF), seconds=float(f[i + 4]), gain=float(f[i + 5]),
                       target_cms=float(f[i + 6]) * 100.0, direction=tuple(np.round(f[i + 7:i + 10], 4)))
        elif sub in (15, 18) and size >= 32:   # LapBoost / ZBoost: gain, target m/s, direction
            rec.update(gain=float(f[i + 3]), target_cms=float(f[i + 4]) * 100.0,
                       direction=tuple(np.round(f[i + 5:i + 8], 4)))
        elif sub in (7, 15, 16, 18, 21, 24, 9):
            pass
        else:
            continue
        nodes.append(rec)
    effects = defaultdict(list)
    for t in (13, 17, 18):
        for i in np.nonzero((w[:-3] == t) & (w[1:-2] == 12))[0]:
            effects[t].append(round(float(f[i + 2]), 3))
    return nodes, effects


def verify(track, session):
    from race_trace import RaceTrace
    t = RaceTrace(session)
    expect = defaultdict(list)          # bit -> list of dist-to-go where an event starts
    for p in track.line_paths:
        for e in p.events:
            expect[e.type].append(p.dist_to_go_at_start - e.start)
    hits, total, worst = 0, 0, 0.0
    detail = Counter()
    for i in range(len(t.riders)):
        m = t.u32(i, 0x388)
        best = t.f32(i, 0x378)
        for k in np.nonzero(np.diff(m) != 0)[0] + 1:
            new = int(m[k]) & ~int(m[k - 1])
            for b in range(32):
                if new >> b & 1:
                    total += 1
                    cands = expect.get(b, [])
                    err = min((abs(best[k] - c) for c in cands), default=1e9)
                    # entering a long zone after a line switch starts the mask mid-zone: accept if
                    # the rider is inside any event range of that type
                    inside = any(p.dist_to_go_at_start - e.end - 1 <= best[k] <= p.dist_to_go_at_start - e.start + 1
                                 for p in track.line_paths for e in p.events if e.type == b)
                    ok = err < 120.0 or inside
                    hits += ok
                    detail[(b, ok)] += 1
                    if err < 1e8 and not inside:
                        worst = max(worst, err)
    return hits, total, worst, detail


def main():
    arg = Path(sys.argv[1])
    d = arg if arg.is_dir() else arg.parent
    stem = next(d.glob("*.aip")).stem if arg.is_dir() else arg.stem
    track = rc.load_track(str(d / f"{stem}.aip"))
    print(f"track {stem}: {len(track.ai_paths)} AI lines, {len(track.line_paths)} line paths, start slots "
          f"{track.start_paths}, race length {track.race_length():.0f} cm")
    for row in list_events(track):
        kind, idx, name, typ, ft, prm, s, e, togo, pt = row
        if kind == "line" or typ in (25, 26, 29):
            extra = f"to-go {togo:9.0f}" if togo is not None else ""
            print(f"  {kind:4s} path {idx:2d} {name:14s} file {ft:4d} param {prm:4d} [{s:8.0f},{e:8.0f}] {extra} "
                  f"at ({pt[0]:.0f}, {pt[1]:.0f}, {pt[2]:.0f})")
    ltg = d / f"{stem}.ltg"
    if ltg.exists():
        print("ltg:", ltg_header(ltg))
    ssf = d / f"{stem}.ssf"
    if ssf.exists():
        nodes, eff = ssf_records(ssf)
        print("ssf boost-type node records:", Counter(n["cls"] for n in nodes))
        for n in nodes:
            if "gain" in n:
                print("   ", n)
        print("ssf rider effects: reset(13) %d, burst(17) %s, launch boost(18) %s" %
              (len(eff[13]), sorted(Counter(eff[17]).items()), sorted(Counter(eff[18]).items())))
    if "--verify" in sys.argv:
        s = sys.argv[sys.argv.index("--verify") + 1]
        hits, total, worst, detail = verify(track, s)
        print(f"verify {s}: {hits}/{total} recorded trigger-mask bits explained by decoded line events "
              f"(worst start mismatch {worst:.0f} cm); per bit {dict(detail)}")


if __name__ == "__main__":
    main()
