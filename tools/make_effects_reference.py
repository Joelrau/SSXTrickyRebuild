"""Reference data for the SSX.Effects.* tests (git-ignored SimData output).
Usage: python tools/make_effects_reference.py
"""
import libpath  # noqa: F401,E402  (tools/lib on sys.path)
import traces  # noqa: E402

traces.require()
TR = traces.root()

import json
import struct
import sys
from collections import Counter
from pathlib import Path

import numpy as np

ROOT = Path(__file__).resolve().parent.parent
import effects as fx  # noqa: E402
import effects_validate as ev  # noqa: E402
from export_effects import TRACK_IDS, check  # noqa: E402

OUT = ROOT / "Unreal" / "SSXTrickyRebuild" / "SimData" / "effects_reference.json"
MODELS = ROOT / "unpacked" / "MODELS"
LIVE = TR / "live"
GARI = MODELS / "GARI" / "data" / "models" / "gari"


def parse_section():
    out = {}
    for p in sorted(MODELS.glob("*/data/models/*.ssf")):
        s = fx.parse_ssf(p)
        recs = [r for e in s.effects for r in e] + [r for _n, f in s.functions for r in f]
        out[p.stem.lower()] = dict(n_inst=len(s.inst_index), n_event=s.header["n_event"], n_effect=s.header["n_effect"],
                                   n_func=s.header["n_func"], n_record=len(recs), n_spline=s.header["n_spline"],
                                   types={str(k): v for k, v in sorted(Counter(r.type for r in recs).items())},
                                   ok=not check(s, p.read_bytes()))
    return out


def run_flow(s, mode, track):
    w = fx.World(s, mode, track)
    for name in fx.flow_scripts("prerace", mode, track):
        w.call_function(name)
    return w


def modes_section():
    out = {}
    for p in sorted(MODELS.glob("*/data/models/*.ssf")):
        stem = p.stem.lower()
        if stem == "ssxfe":
            continue
        s = fx.parse_ssf(p)
        per = {}
        for mode in range(10):
            w = run_flow(s, mode, TRACK_IDS[stem])
            d = dict(dead=sorted(i for i, c in w.controllers.items() if c.type == fx.TYPE_DEAD),
                     rails=list(w.rail_enabled), scripts=len(w.scripts))
            if stem == "gari":
                d["flags"] = list(w.flags)
            per[str(mode)] = d
        out[stem] = per
    return out


def ram_section():
    sp = LIVE / "ride_session_1" / "spawn_ram.bin"
    ram = sp.read_bytes()
    rv = ev.ram_view(ram)
    s = fx.parse_ssf(GARI.with_suffix(".ssf"))
    import world_data as wd  # noqa: E402
    d = GARI.with_suffix(".pbd").read_bytes()
    counts, offs = wd.pbd_header(d)
    first = d[offs["spline_segment"]:offs["spline_segment"] + 64]
    base = ram.find(first) - offs["spline_segment"]
    ram_rails = [struct.unpack_from("<I", ram, base + offs["spline"] + 40 * k + 24)[0] & 1 for k in range(counts["splines"])]
    meta = json.loads((sp.parent / "meta.json").read_text())
    pts = [list(struct.unpack_from("<2f", ram, int(r, 16) + 0x140)) for r in meta["riders"]]
    w = run_flow(s, rv["mode"], rv["track"])
    ct = fx.CellTracker(fx.grid_from_ltg(GARI.with_suffix(".ltg").read_bytes(), showoff=rv["mode"] in fx.SHOWOFF_MODES))
    ct.step(w, [tuple(p) for p in pts])
    for name in fx.flow_scripts("countdown_enter", rv["mode"], rv["track"]):
        w.call_function(name)
    return dict(mode=rv["mode"], track=rv["track"],
                dead=sorted(i for i, t in enumerate(rv["ctype"]) if t == fx.TYPE_DEAD),
                dead_flags={str(i): rv["flags"][i] for i, t in enumerate(rv["ctype"]) if t == fx.TYPE_DEAD},
                rails=ram_rails, points=pts,
                ram_controllers={str(i): t for i, t in enumerate(rv["ctype"]) if t is not None and t != fx.TYPE_DEAD},
                model_controllers={str(i): c.type for i, c in w.controllers.items() if c.type != fx.TYPE_DEAD},
                active_cells=len(ct.active))


def live_section(sess):
    import instance_collision as ic  # noqa: E402
    import collision as col  # noqa: E402
    S = LIVE / sess
    meta = json.loads((S / "meta.json").read_text())
    riders = meta["riders"]
    rec = sum(sz for _o, sz in meta["blocks"])
    fr = 8 + len(riders) * rec
    raw = np.memmap(S / "frames.bin", dtype=np.uint8, mode="r")
    n = len(raw) // fr
    s = fx.parse_ssf(GARI.with_suffix(".ssf"))
    names = fx.map_instance_names(GARI.with_suffix(".map"))
    w = fx.World(s, 4, 0, names=names)
    w.call_function("RaceMode")
    insts, ltg = ic.load_track(GARI, keep_ltg=True)
    world = ic.InstanceWorld(insts, alternate_mode=False, ltg=ltg)
    all_idx = dict(world.by_index)
    R = [fx.Rider(slot=k) for k in range(len(riders))]
    ct = fx.CellTracker(fx.grid_from_ltg(GARI.with_suffix(".ltg").read_bytes(), showoff=False))
    n_sub = (1536 - 0x4A0) // 0x20

    def shape(b, o, mask):
        c = np.array(struct.unpack_from("<3f", b, o + 0x480), float)
        r, = struct.unpack_from("<f", b, o + 0x490)
        cnt, = struct.unpack_from("<i", b, o + 0x498)
        subs = [(np.array(struct.unpack_from("<3f", b, o + 0x4A0 + 0x20 * k), float),
                 struct.unpack_from("<f", b, o + 0x4A0 + 0x20 * k + 0x10)[0]) for k in range(min(cnt, n_sub))]
        return col.SphereSet(c, r, subs, mask)

    points, contacts, recorded, predicted = [], [], [], []
    prev = None
    for f in range(n):
        b = bytes(raw[f * fr:(f + 1) * fr])
        pts = [struct.unpack_from("<2f", b, 8 + k * rec + 0x140) for k in range(len(riders))]
        points.append([v for p in pts for v in p])
        ct.step(w, pts)
        world.by_index = {k: v for k, v in all_idx.items() if w.flags[k] & fx.FL_COLLIDABLE}
        for k in range(len(riders)):
            o = 8 + k * rec
            motion, _control = struct.unpack_from("<2i", b, o + 0x424)
            burst, launch = struct.unpack_from("<2f", b, o + 0x134)
            if prev is not None:
                pb, pl = struct.unpack_from("<2f", prev, o + 0x134)
                if burst > pb + 0.1:
                    recorded.append([f, k, 17, burst])
                if launch > pl + 0.1:
                    recorded.append([f, k, 18, launch])
            if motion not in ic.MOTION_MASK:
                continue
            sh = shape(b, o, ic.MOTION_MASK[motion])
            if not (sh.radius > 0):
                continue
            hit = world.prop_query(sh)
            if hit is None:
                continue
            i = hit.cell[0]
            contacts.append([f, k, int(i), int(motion)])
            R[k].motion = motion
            before = len(R[k].log)
            w.touch(i, R[k])
            for o_ in R[k].log[before:]:
                if o_[0] in ("boost_burst", "launch_boost"):
                    predicted.append([f, k, 17 if o_[0] == "boost_burst" else 18, float(o_[1]), int(i)])
        w.frame()
        prev = b
    fw = sorted(i for i, nm in names.items() if fx.base_name(nm) == "Mdl_FWTrigger")
    return dict(frames=n, riders=len(riders), points=points, contacts=contacts, recorded=recorded,
                predicted=predicted, fw_triggers=fw,
                dead_end=sorted(i for i, c in w.controllers.items() if c.type == fx.TYPE_DEAD),
                scripts_end=len(w.scripts), controllers_end=len(w.controllers))


CLASS_ID = {v: k for k, v in fx.NODE_CLASSES.items()}


def seq_rng():
    """Deterministic stand-in for the stream-B draw: r_i = ((i * 7919) % 997) / 997 (the C++ test uses the same)."""
    state = {"i": 0}

    def draw():
        v = ((state["i"] * 7919) % 997) / 997.0
        state["i"] += 1
        return v
    return draw


def outcome_str(o):
    """One model log entry as the comparison string (SSX.Effects.Scenarios builds the same from FOutcome)."""
    k = o[0]
    cid = lambda c: CLASS_ID.get(c, c) if isinstance(c, str) else c
    if k == "node":
        return f"node {o[1]} {cid(o[2])}"
    if k == "node_restart":
        return f"restart {o[1]}"
    if k == "hide":
        return f"hide {o[1]} {'dead' if o[2] == 'dead' else 'restore'}"
    if k == "message":
        return f"msg {o[1]} {cid(o[2])} {o[3]} {round(o[4] * 1000)}"
    if k == "emitter":
        return f"emit 0 {o[1]}"
    if k == "spline_path":
        return f"emit 1 {o[1]} {o[2]}"
    if k == "collide_emitter":
        return f"emit 2 {o[1]}"
    if k == "sound3d":
        return f"sound {o[2]} {o[3]} {1 if o[4] == 'variant' else 0}"
    if k == "rail":
        return f"rail {o[1]} {o[2]}"
    if k == "camera":
        return f"camera {o[1]}"
    if k == "roller_stop":
        return f"rollerstop {o[1]}"
    if k == "fence_wobble":
        return f"fence {o[1]}"
    if k == "rider":
        s = o[1]
        if s == "popup":
            return f"rider mult {o[3]}" if o[2] == 0xC else None
        if s in ("boost_burst", "launch_boost", "meter_reward", "add_clock", "time_sound", "multiplier_sound"):
            return f"rider {s} {round(o[2] * 1000)}"
        if s == "showoff_points":
            return f"rider showoff_points {o[2]}"
        if s == "teleport":
            return f"rider teleport {o[2]}"
        return f"rider {s}"
    return k


def scenario(stem, mode):
    """Every instance activated, every touchable trigger touched (and the one three back touched again) one per
    frame by a human / AI rider, the countdown flows, 300 frames, every second instance deactivated, 300 frames."""
    p = MODELS / stem.upper() / "data" / "models" / f"{stem}.ssf"
    if not p.exists():
        p = next(MODELS.glob(f"*/data/models/{stem}.ssf"))
    s = fx.parse_ssf(p)
    tid = TRACK_IDS[stem]
    w = fx.World(s, mode, tid, rng=seq_rng())
    out, frame, seen = [], 0, 0

    def mark():
        nonlocal seen
        for o in w.log[seen:]:
            st = outcome_str(o)
            if st is not None:
                out.append(f"{frame} {st}")
        seen = len(w.log)

    for name in fx.flow_scripts("prerace", mode, tid):
        w.call_function(name)
    mark()
    n = len(s.inst_index)
    for i in range(n):
        w.activate(i)
    mark()
    for name in fx.flow_scripts("countdown_enter", mode, tid):
        w.call_function(name)
    mark()
    trig = [t["instance"] for t in fx.trigger_instances(s)]
    riders = [fx.Rider(slot=0, human=True), fx.Rider(slot=1, human=False)]
    for k, i in enumerate(trig):
        r = riders[k % 2]
        r.motion = 1 if k % 3 == 0 else 2
        w.touch(i, r, impact=(0.5, 400.0))
        if k >= 3:
            w.touch(trig[k - 3], riders[(k + 1) % 2], impact=(0.5, 400.0))
        mark()
        w.frame()
        frame += 1
        mark()
    for name in fx.flow_scripts("countdown_end", mode, tid):
        w.call_function(name)
    mark()
    for _ in range(300):
        w.frame()
        frame += 1
        mark()
    for i in range(0, n, 2):
        w.deactivate(i)
    mark()
    for _ in range(300):
        w.frame()
        frame += 1
        mark()
    return dict(track=stem, mode=mode, triggers=trig, frames=frame, log=out,
                dead=sorted(i for i, c in w.controllers.items() if c.type == fx.TYPE_DEAD),
                controllers=len(w.controllers), scripts=len(w.scripts), rails=list(w.rail_enabled))


def scenarios_section():
    return [scenario(stem, mode) for stem in sorted(TRACK_IDS) if stem != "ssxfe" for mode in (4, 3, 1)]


def main():
    ref = dict(parse=parse_section(), modes=modes_section(), ram=ram_section(), scenarios=scenarios_section())
    ref["live"] = {sess: live_section(sess) for sess in ("ride_session_1", "ride_session_2")
                   if (LIVE / sess / "frames.bin").exists()}
    OUT.parent.mkdir(parents=True, exist_ok=True)
    OUT.write_text(json.dumps(ref, separators=(",", ":")))
    r = ref["ram"]
    print(f"{OUT.name}: {len(ref['parse'])} tables, RAM dead {len(r['dead'])}, rails {len(r['rails'])}, "
          f"controllers RAM {len(r['ram_controllers'])} model {len(r['model_controllers'])}; live "
          + ", ".join(f"{k}: {v['frames']} frames, {len(v['contacts'])} contacts, {len(v['predicted'])} op17/18, "
                      f"{len(v['recorded'])} recorded rises" for k, v in ref["live"].items()))


if __name__ == "__main__":
    main()
