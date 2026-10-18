"""Validation of models/effects.py.

  python scripts/effects_validate.py [--live] [--frames N]

1. Parse: every .ssf of the 12 track folders. Header magic/version, every record size equals the
   size of its type (subtype for 0 / 2), every reference in range (event slots, instance event
   index, op 7 instance/effect, op 21/26 function, op 24 instance, op 25 spline), the effect and
   function bodies tile the data block, per-track counts.
2. RAM (traces/live/*/spawn_ram.bin, Garibaldi circuit race, mode 4): the race-flow mode script
   (RaceMode -> HideShowOff) reproduces the controller type 5 (cDeadNode) set, those instances'
   flag words and the rail enable flags.
3. RAM: the slot-0 controllers of the instances in the active cells (3x3 around each racer)
   against the controllers present in RAM.
4. --live: the recorded rider frames (traces/live/ride_session_1|2): prop contacts (PropCollision query)
   drive World.touch; every predicted op 17 / 18 must show in the recorded timers (+0x134 / +0x138)
   and every recorded timer rise must be predicted.
"""
import json
import struct
import sys
from collections import Counter, defaultdict
from pathlib import Path

import numpy as np

HERE = Path(__file__).resolve().parent
sys.path.insert(0, str(HERE.parent / "models"))
import effects as fx  # noqa: E402

sys.path.insert(0, str(HERE.parent))
import traces  # noqa: E402
MODELS = HERE.parents[2] / "unpacked" / "MODELS"
LIVE = (traces.root() or Path(".")) / "live"
RESET_WAIT = 0x16                 # control state ResetWait
GARI = MODELS / "GARI" / "data" / "models" / "gari"


def ssf_files():
    return sorted(MODELS.glob("*/data/models/*.ssf"))


def check_parse():
    print("[1] parse")
    ok_all = True
    tot = Counter()
    for p in ssf_files():
        s = fx.parse_ssf(p)
        h = s.header
        errs = []
        if s.header["magic"] != 0x1E0200 or (struct.unpack_from("<h", p.read_bytes(), 4)[0] != 0x1500):
            errs.append("magic")
        if h["n_inst"] and h["n_inst"] != len(s.inst_index):
            errs.append("inst count")
        spans = []
        allrec = [r for e in s.effects for r in e] + [r for _n, f in s.functions for r in f]
        for lst in [*s.effects, *(f for _n, f in s.functions)]:
            if lst:
                spans.append((lst[0].offset, lst[-1].offset + lst[-1].size))
        spans = sorted(set(spans))
        pos = s.data_base
        for a, b in spans:
            if a != pos:
                errs.append(f"gap/overlap at {a:#x} (expected {pos:#x})")
                break
            pos = b
        if pos != s.data_end:
            errs.append("tail")
        c = Counter()
        for r in allrec:
            c[r.type] += 1
            exp = fx.expected_size(r)
            if exp is None or exp != r.size:
                errs.append(f"size type {r.type} sub {r.subtype} = {r.size} (expected {exp})")
            if r.type not in fx.OP_NAMES:
                errs.append(f"unknown type {r.type}")
            if r.type == 7 and not (0 <= r.i(0) < h["n_inst"] and 0 <= r.i(1) < h["n_effect"]):
                errs.append("op7 ref")
            if r.type in (21, 26) and not (0 <= r.i(0) < h["n_func"]):
                errs.append("op21 ref")
            if r.type == 24 and not (0 <= r.i(0) < h["n_inst"]):
                errs.append("op24 ref")
            if r.type == 25 and not (0 <= r.i(0) < h["n_spline"] and r.i(1) in (0, 1)):
                errs.append("op25 ref")
        for ev in s.events:
            if any(not (-1 <= e < h["n_effect"]) for e in ev):
                errs.append("event slot ref")
        for rec in s.inst_records:
            if not (-1 <= rec[6] < h["n_event"]):
                errs.append("inst event ref")
        slots = Counter(k for ev in s.events for k, e in enumerate(ev) if e >= 0)
        tot.update(c)
        print(f"  {p.stem:8s} events {h['n_event']:3d} effects {h['n_effect']:4d} functions {h['n_func']:2d} "
              f"records {len(allrec):5d} slots {dict(sorted(slots.items()))} "
              f"{'OK' if not errs else 'FAIL ' + '; '.join(sorted(set(errs))[:4])}")
        ok_all &= not errs
    print(f"  record types over all tracks: {dict(sorted(tot.items()))}")
    print(f"  => {'all 12 parse cleanly' if ok_all else 'FAILURES'}")
    return ok_all


def ram_view(ram):
    U = lambda a: struct.unpack_from("<I", ram, a)[0]
    world = 0x346488
    pbd = U(world + 4)
    n = U(pbd + 0xC)
    arr = U(pbd + 0x48)
    flags, ctype = [], []
    for i in range(n):
        b = arr + 0x100 * i
        flags.append(U(b + 0xE8))
        c = U(b + 0xE4)
        ctype.append(U(c + 0x14) if c else None)
    return dict(mode=U(0x32DE8C), track=U(0x32DE88), flags=flags, ctype=ctype)


def check_ram():
    snaps = sorted(LIVE.glob("*/spawn_ram.bin"))
    if not snaps:
        print("[2] no RAM snapshot")
        return True
    sp = snaps[0]
    ram = sp.read_bytes()
    rv = ram_view(ram)
    s = fx.parse_ssf(GARI.with_suffix(".ssf"))
    print(f"[2] RAM {sp.parent.name}: mode {rv['mode']} track {rv['track']}; flow scripts at PreRace: "
          f"{fx.flow_scripts('prerace', rv['mode'], rv['track'])}")
    w = fx.World(s, rv["mode"], rv["track"])
    for name in fx.flow_scripts("prerace", rv["mode"], rv["track"]):
        w.call_function(name)
    pred_dead = {i for i, c in w.controllers.items() if c.type == fx.TYPE_DEAD}
    ram_dead = {i for i, t in enumerate(rv["ctype"]) if t == fx.TYPE_DEAD}
    print(f"  cDeadNode set: predicted {len(pred_dead)}, RAM {len(ram_dead)}, equal: {pred_dead == ram_dead}")
    good = 0
    for i in ram_dead:
        low = rv["flags"][i] & 0xFFFF
        init = (s.initial_flags(i) >> 16) & 0xFFFF
        if (low & 0xF2) == 0 and low & fx.FL_NODE_DRAWN and (low & 0x0D & ~fx.FL_NODE_DRAWN) == (init & 0x09):
            good += 1
    print(f"  their flag words (0x2/0x10/0x20/0x40/0x80 clear, 0x4 set, 0x1/0x8 kept): {good}/{len(ram_dead)}")
    # rails: compare with the in-RAM spline flags
    import world_data as wd  # noqa: E402
    d = GARI.with_suffix(".pbd").read_bytes()
    counts, offs = wd.pbd_header(d)
    first = d[offs["spline_segment"]:offs["spline_segment"] + 64]
    base = ram.find(first) - offs["spline_segment"]
    ram_en = [struct.unpack_from("<I", ram, base + offs["spline"] + 40 * k + 24)[0] & 1 for k in range(counts["splines"])]
    eq = sum(a == b for a, b in zip(ram_en, w.rail_enabled))
    print(f"  rail enable after the mode script: {eq}/{len(ram_en)} splines match RAM")
    ok = pred_dead == ram_dead and eq == len(ram_en) and good == len(ram_dead)

    # 3. slot-0 controllers in the active cells
    meta = json.loads((sp.parent / "meta.json").read_text())
    pts = [struct.unpack_from("<2f", ram, int(r, 16) + 0x140) for r in meta["riders"]]
    g = fx.grid_from_ltg(GARI.with_suffix(".ltg").read_bytes(), showoff=rv["mode"] in fx.SHOWOFF_MODES)
    ct = fx.CellTracker(g)
    ct.step(w, pts)
    pred0 = {i: c.type for i, c in w.controllers.items() if c.type != fx.TYPE_DEAD}
    # the snapshot was taken after the countdown started: StartCountdown -> CountDownStart
    for name in fx.flow_scripts("countdown_enter", rv["mode"], rv["track"]):
        w.call_function(name)
    pred = {i: c.type for i, c in w.controllers.items() if c.type != fx.TYPE_DEAD}
    print(f"  countdown scripts {fx.flow_scripts('countdown_enter', rv['mode'], rv['track'])} add "
          f"{sorted(set(pred) - set(pred0))}; running scripts {len(w.scripts)} (CountDownStart waits)")
    ramc = {i: t for i, t in enumerate(rv["ctype"]) if t is not None and t != fx.TYPE_DEAD}
    both = {i for i in pred if ramc.get(i) == pred[i]}
    only_p = {i: pred[i] for i in pred if i not in ramc}
    only_r = {i: ramc[i] for i in ramc if i not in pred}
    names = fx.map_instance_names(GARI.with_suffix(".map"))
    print(f"[3] active cells around {len(pts)} racers: {len(ct.active)}; slot-0 controllers predicted {len(pred)}, "
          f"RAM {len(ramc)}, same instance+type {len(both)}")
    if only_p:
        print("  predicted only:", Counter((fx.base_name(names.get(i, '?')), fx.NODE_CLASSES.get(t, t)) for i, t in only_p.items()))
    if only_r:
        print("  RAM only:", Counter((fx.base_name(names.get(i, '?')), fx.NODE_CLASSES.get(t, t)) for i, t in only_r.items()))
    ok &= not only_p and not only_r and len(both) == len(ramc)
    return ok


def check_live(sess, max_frames=None):
    import instance_collision as ic  # noqa: E402
    import collision as col  # noqa: E402
    S = LIVE / sess
    meta = json.loads((S / "meta.json").read_text())
    riders = meta["riders"]
    rec = sum(sz for _o, sz in meta["blocks"])
    fr = 8 + len(riders) * rec
    raw = np.memmap(S / "frames.bin", dtype=np.uint8, mode="r")
    n = len(raw) // fr if max_frames is None else min(len(raw) // fr, max_frames)
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

    pred_events, rec_events = [], []
    resets, reset_entries = [], []
    touches = Counter()
    contact_runs = Counter()
    prev = None
    for f in range(n):
        b = bytes(raw[f * fr:(f + 1) * fr])
        ct.step(w, [struct.unpack_from("<2f", b, 8 + k * rec + 0x140) for k in range(len(riders))])
        # keep the query's instance set equal to the collidable instances of the model world
        world.by_index = {k: v for k, v in all_idx.items() if w.flags[k] & fx.FL_COLLIDABLE}
        for k in range(len(riders)):
            o = 8 + k * rec
            motion, control = struct.unpack_from("<2i", b, o + 0x424)
            if prev is not None and control == RESET_WAIT and struct.unpack_from("<i", prev, o + 0x428)[0] != RESET_WAIT:
                reset_entries.append((f, k))
            burst, launch = struct.unpack_from("<2f", b, o + 0x134)
            if prev is not None:
                pb, pl = struct.unpack_from("<2f", prev, o + 0x134)
                if burst > pb + 0.1:
                    rec_events.append((f, k, 17, round(burst, 2)))
                if launch > pl + 0.1:
                    rec_events.append((f, k, 18, round(launch, 2)))
            if motion not in ic.MOTION_MASK:
                continue
            sh = shape(b, o, ic.MOTION_MASK[motion])
            if not (sh.radius > 0):
                continue
            hit = world.prop_query(sh)
            if hit is None:
                continue
            i = hit.cell[0]
            R[k].motion = motion
            before = len(R[k].log)
            nlog = len(w.log)
            w.touch(i, R[k])
            if len(w.log) > nlog:
                contact_runs[fx.base_name(names.get(i, "?"))] += 1
            for o_ in R[k].log[before:]:
                if o_[0] == "reset":
                    resets.append((f, k, names.get(i, i)))
                if o_[0] in ("boost_burst", "launch_boost"):
                    pred_events.append((f, k, 17 if o_[0] == "boost_burst" else 18, o_[1], names.get(i, i)))
                    touches[fx.base_name(names.get(i, "?"))] += 1
        w.frame()
        prev = b
    # match within +-2 samples, same rider and op
    matched_p, matched_r = set(), set()
    for a, pe in enumerate(pred_events):
        for c, re_ in enumerate(rec_events):
            if c in matched_r:
                continue
            if re_[1] == pe[1] and re_[2] == pe[2] and abs(re_[0] - pe[0]) <= 2 and abs(re_[3] - pe[3]) < 0.1:
                matched_p.add(a)
                matched_r.add(c)
                break
    print(f"[4] {sess}: {n} samples x {len(riders)} riders; predicted op17/18 {len(pred_events)} "
          f"({dict(touches)}), recorded timer rises {len(rec_events)}; matched {len(matched_p)}")
    for a, pe in enumerate(pred_events):
        if a not in matched_p:
            print("   unmatched prediction", pe)
    for c, re_ in enumerate(rec_events):
        if c not in matched_r:
            print("   unexplained recorded rise", re_)
    print(f"  rider-frames whose contact produced effect outcomes, by prop: {dict(contact_runs.most_common(12))}")
    # forced resets (op 13): the rider must enter ResetWait within a few samples
    first = {}
    for f, k, nm in resets:
        if (k, nm) not in first or f - first[(k, nm)][-1] > 120:
            first.setdefault((k, nm), []).append(f)
    starts = sorted((fs, k, nm) for (k, nm), lst in first.items() for fs in lst)
    hit = [any(k2 == k and 0 <= f2 - f <= 30 for f2, k2 in reset_entries) for f, k, nm in starts]
    explained = sum(any(k2 == k and 0 <= f2 - f <= 30 for f, k, _n in starts) for f2, k2 in reset_entries)
    print(f"  op 13 resets: {len(starts)} contact runs ({Counter(fx.base_name(str(n)) for _f, _k, n in starts)}); "
          f"followed by ResetWait within 30 samples: {sum(hit)}/{len(starts)}; recorded ResetWait entries "
          f"{len(reset_entries)}, explained by an op-13 contact: {explained}")
    return len(matched_p) == len(pred_events) == len(rec_events) and all(hit)


def main():
    ok = check_parse()
    ok &= check_ram()
    if "--live" in sys.argv:
        nf = int(sys.argv[sys.argv.index("--frames") + 1]) if "--frames" in sys.argv else None
        for sess in ("ride_session_1", "ride_session_2"):
            if (LIVE / sess / "frames.bin").exists():
                ok &= check_live(sess, nf)
    print("effects_validate:", "OK" if ok else "MISMATCH")


if __name__ == "__main__":
    sys.path.insert(0, str(HERE))
    main()
