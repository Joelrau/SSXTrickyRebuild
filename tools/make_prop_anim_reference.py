"""Reference data for the SSX.World.Crowd / SSX.World.AnimObject / SSX.Dynamic.* / SSX.Particles.Trails tests.
Usage: python tools/make_prop_anim_reference.py
"""
import libpath  # noqa: F401,E402  (tools/lib on sys.path)
import traces  # noqa: E402

traces.require()
TR = traces.root()

import json
import math
import struct
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
sys.path.insert(0, str(Path(__file__).resolve().parent))
import dynamic_objects as dyn  # noqa: E402
import effects as fx  # noqa: E402
import particles as pt  # noqa: E402
import pbd  # noqa: E402
import prop_anim as pa  # noqa: E402

MODELS = ROOT / "unpacked" / "MODELS"
OUT = ROOT / "Unreal" / "SSXTrickyRebuild" / "SimData" / "prop_anim_reference.json"
TRACES = TR
RIDERS = [0x885170, 0xBDAF30, 0xC84E70, 0xD2EDB0, 0xDD8CC0, 0xE8AC70]


class Lcg:
    """Deterministic draws shared with the C++ tests (not the game's PRNG): x = x * 1103515245 + 12345 (mod 2^32)."""
    def __init__(self, seed):
        self.x = seed & 0xFFFFFFFF

    def u32(self):
        self.x = (self.x * 1103515245 + 12345) & 0xFFFFFFFF
        return self.x >> 8

    def u01(self):
        return self.u32() / float(1 << 24)


def snapshots():
    return sorted(TRACES.glob("live/*/spawn_ram.bin")) + [TRACES / "ram" / "paused_race_1.bin"]


def crowd_ram():
    people = []
    for sp in snapshots():
        R = sp.read_bytes()
        U = lambda a: struct.unpack_from("<I", R, a & 0x1FFFFFF)[0]
        I = lambda a: struct.unpack_from("<i", R, a & 0x1FFFFFF)[0]
        pbdp = U(0x346488 + 4)
        n, arr = U(pbdp + 0xC), U(pbdp + 0x48)
        for i in range(n):
            c = U(arr + 0x100 * i + 0xE4)
            if not c or U(c + 0x14) != 0x11:
                continue
            rows, cols = I(c + 0x34), I(c + 0x38)
            for k in range(rows * cols):
                ptr = U(c + 0x44 + 4 * k)
                people.append([I(c + 0x84 + 4 * k), I(c + 0xC4 + 4 * k), k, (ptr - 0x31D4B8) // 0x48])
    return people


def crowd_runs():
    runs = []
    for seed, rows, cols in ((7, 4, 4), (99, 2, 8)):
        rng = Lcg(seed)
        box = pa.CrowdBox(rows, cols, rng.u32)
        frames = [list(box.frames[:rows * cols])]
        for f in range(1, 1201):
            phase = 1 if f < 120 else 2
            humans = [(500.0 if f < 900 else 30000.0, 0)]
            box.update(f, phase, humans)
            if f % 5 == 0:
                frames.append(list(box.frames[:rows * cols]))
        runs.append({"seed": seed, "rows": rows, "cols": cols, "frames": frames})
    return runs


def anim_samples():
    out = []
    for stem in ("gari", "aloha", "megaple", "mesa", "merquer", "untrack", "snow", "ssxfe"):
        p = next(MODELS.glob(f"*/data/models/{stem}.pbd"))
        L = pbd.load(p)
        n = 0
        for mi in range(len(L.model_pointers)):
            anims = pa.model_node_anims(L, mi)
            for k, na in anims.items():
                if n >= 4:
                    break
                end = max(s.t1 for segs in na.tracks.values() for s in segs)
                ts = [end * q / 6.0 for q in range(-1, 8)]
                out.append({"track": stem, "model": mi, "object": k, "t": ts,
                            "ch": [[na.channel(c, t) for c in range(6)] for t in ts],
                            "m": [sum(na.matrix(t), []) for t in ts]})
                n += 1
    return out


def anim_runs():
    rec = lambda mode, s, e, spd, d=0: pa.AnimObjectRecord(mode, s, e, spd, 0.0, 0.0, 0, d)
    out = []
    for name, r in (("once", rec(0, 0, 20, 30)), ("loop", rec(1, 0, 60, 30)), ("pingpong", rec(2, 5, 25, 45)),
                    ("back_once", rec(0, 0, 40, 30, 4)), ("close", rec(0, 20, 40, 30))):
        a = pa.AnimObject(r, 60.0)
        ts, done = [], -1
        for f in range(200):
            if a.frame() and done < 0:
                done = f
            ts.append(a.t)
        out.append({"name": name, "mode": r.mode, "start": r.start, "end": r.end, "speed": r.speed, "dir": r.direction,
                    "t": ts, "done": done})
    return out


def doors():
    p = MODELS / "SSXFE" / "data" / "models" / "ssxfe.ssf"
    s = fx.parse_ssf(p)
    L = pbd.load(p.with_suffix(".pbd"))
    names = fx.map_instance_names(p.with_suffix(".map"))
    fn = {n: recs for n, recs in s.functions}
    rows = []
    for menu in ("PR", "SE", "WC"):
        for r in fn[f"on{menu}Open"][1:]:
            inst = r.i(0)
            na = pa.model_node_anims(L, L.instances[inst].model_id)[0]
            rows.append({"menu": menu, "inst": inst, "name": names[inst],
                         "tz": [na.channel(2, f / 30.0) for f in (0, 10, 20, 30, 40)]})
    return rows


def dynamic():
    p = MODELS / "ALASKA" / "data" / "models" / "alaska.ssf"
    s = fx.parse_ssf(p)
    raw = p.read_bytes()
    names = fx.map_instance_names(p.with_suffix(".map"))
    users = [i for i in range(len(s.inst_index)) if s.effect_for(i, 1) >= 0 and
             any(r.type == 0 and r.i(0) == 0 for r in s.effects[s.effect_for(i, 1)])]
    inst = next(i for i in users if fx.base_name(names[i]) == "Mdl_Pylon")
    r = next(r for r in s.effects[s.effect_for(inst, 1)] if r.type == 0 and r.i(0) == 0)
    rec = dyn.RollerRecord(r.f(1), r.f(2), r.f(3), (r.f(4), r.f(5), r.f(6)))
    irec = s.inst_records[s.inst_index[inst]]
    off, _size, _cnt = struct.unpack_from("<3i", raw, s.header["p_tree"] + 12 * irec[5])
    com = struct.unpack_from("<3f", raw, off + 0x1C)
    Mi = [tuple(struct.unpack_from("<3f", raw, off + 0x4C + 12 * k)) for k in range(3)]
    body = dyn.RigidBody(rec.mass, Mi, com, (0.0, 0.0, 40.0), (1.0, 0.0, 0.0, 0.0))
    contact = dict(v_hat=(1.0, 0.0, 0.0), n=(-1.0, 0.0, 0.0), a=2000.0)
    roll = dyn.Roller(rec, body, contact)
    v0 = list(body.v)
    floor = lambda b: (30.0 - b.x[2], (0.0, 0.0, 1.0), (b.x[0], b.x[1], 0.0)) if b.x[2] < 30.0 else None
    frames, peak, track = 0, 0.0, []
    while not roll.frame(terrain_query=floor) and frames < 2000:
        frames += 1
        peak = max(peak, body.x[2])
        if frames % 10 == 0:
            track.append([frames, *body.x, *body.q])
    J = dyn.rider_hits_body(body, (1500.0, 0.0, 0.0), 120.0, (body.x[0] - 20.0, body.x[1], body.x[2]), (-1.0, 0.0, 0.0))
    dv = dyn.rider_movable_dv(2000.0, 120.0, rec.mass, 0.2)
    pylon = {"inst": inst, "mass": rec.mass, "com": list(com), "inv_inertia": [list(x) for x in Mi], "v0": v0,
             "omega0": [-10.0, 0.0, 0.0], "frames": frames, "peak": peak, "track": track,
             "hit_J": list(J) if J else None, "hit_v": list(body.v), "movable_dv": dv}
    mrec = dyn.MeshAnimRecord(0, 0.05, 2.0, (0.0, 0.0, 0.0), (800.0, 800.0, 900.0), 1.0)
    rng = Lcg(1234)
    pieces = [((100.0 * k, 50.0, 300.0), (1.0, 0.0, 0.0, 0.0)) for k in range(3)]
    ident = [(1.0, 0.0, 0.0), (0.0, 1.0, 0.0), (0.0, 0.0, 1.0)]
    m = dyn.MeshAnim(mrec, pieces, ident, dict(v_hat=(0.0, 1.0, 0.0), n=(0.0, -1.0, 0.0), a=400.0), rng.u01,
                     lambda: rng.u32() & 1)
    res, done = [], -1
    for f in range(130):
        r_ = m.frame()
        if r_ and done < 0:
            done = f
        if f in (0, 30, 60, 100):
            res.append([f] + [x for pc in m.pieces for x in (*pc.pos, *pc.q)])
    debris = {"seed": 1234, "done": done, "samples": res}
    c = dyn.Cracked(-1.0, 5.0, dict(v_hat=(0.0, 1.0, 0.0), n=(0.0, -1.0, 0.0), a=100.0))
    glass = {"health_after_100": c.health, "break_speed": 5.0 / dyn.CRACK_DAMAGE}
    p = MODELS / "MEGAPLE" / "data" / "models" / "megaple.ssf"
    s = fx.parse_ssf(p)
    names = fx.map_instance_names(p.with_suffix(".map"))
    panes = sorted(i for i, n in names.items() if fx.base_name(n) == "Mdl_Glass_Pane")
    junk = sorted(i for i, n in names.items() if fx.base_name(n).startswith("Mdl_Glass_Junk"))
    glass.update(panes=panes, junk=junk)
    return {"pylon": pylon, "debris": debris, "glass": glass}


def trails():
    riders, entries = [], 0
    for sp in snapshots():
        R = sp.read_bytes()
        U = lambda a: struct.unpack_from("<I", R, a & 0x1FFFFFF)[0]
        I = lambda a: struct.unpack_from("<i", R, a & 0x1FFFFFF)[0]
        F = lambda a: struct.unpack_from("<f", R, a & 0x1FFFFFF)[0]
        V = lambda a: [F(a + 4 * k) for k in range(3)]
        meta = sp.parent / "meta.json"
        rids = [int(r, 16) for r in json.loads(meta.read_text())["riders"]] if meta.exists() else RIDERS
        for r in rids:
            h = U(r + 0x5AE0)
            t = U(h + 0x3E0) + 0x8020
            head, cnt = I(t + 0x1940), I(t + 0x1944)
            ents = []
            for j in range(1, cnt):
                e = t + 0x40 + 0x40 * ((head - j) % 100)
                ents.append(V(e) + V(e + 0x10) + [F(e + 0x20 + 4 * q) for q in range(5)] + [F(e + 0x3C)])
            entries += len(ents)
            riders.append({"surface": I(r + 0x290), "motion": I(r + 0x424), "kind": I(r + 0x2E0),
                           "active": I(t + 0x1950), "side": F(t + 0x194C), "count": cnt, "entries": ents,
                           "ribbon_counts": [I(h + 0x1E4), I(h + 0x3C4)]})
    rules = {"weights": [[sd, st, *pt.trail_side_weights(sd, st)] for sd in (-1.0, -0.4, 0.3, 1.0) for st in (-0.8, 0.0, 0.5)],
             "push": [[d, sp_, dw, c, a, pt.trail_should_push(d, sp_, dw, c, a)]
                      for d in (50.0, 120.0) for sp_ in (100.0, 500.0) for dw in (2.0, 15.0) for c in (0.999, 0.99) for a in (True, False)],
             "ramp": pt.trail_stop_ramp([1.0] * 6), "ribbon_alpha": [pt.ribbon_alphas(n) for n in range(2, 8)],
             "scale": [[s_, pt.trail_surface_scale(s_) or -1.0] for s_ in range(0, 20)]}
    return {"riders": riders, "entries": entries, "rules": rules}


def main():
    ref = {"crowd_ram": crowd_ram(), "crowd_runs": crowd_runs(), "anim_samples": anim_samples(), "anim_runs": anim_runs(),
           "doors": doors(), "dynamic": dynamic(), "trails": trails()}
    OUT.write_text(json.dumps(ref))
    print(f"{OUT.name}: {len(ref['crowd_ram'])} RAM crowd people, {len(ref['anim_samples'])} keyframe samples, "
          f"{len(ref['doors'])} door objects, pylon rest after {ref['dynamic']['pylon']['frames']} frames, "
          f"{len(ref['trails']['riders'])} trail FIFOs / {ref['trails']['entries']} entries")


if __name__ == "__main__":
    main()
