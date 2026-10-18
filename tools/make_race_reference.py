"""Reference values for the SSX.Race automation tests, computed with the Python models.
Usage:  python tools/make_race_reference.py [session]      (default ride_session_1)
"""
import libpath  # noqa: F401,E402  (tools/lib on sys.path)
import traces  # noqa: E402

traces.require()
TR = traces.root()

import math
import struct
import sys
from pathlib import Path

import numpy as np

ROOT = Path(__file__).resolve().parent.parent
import race as rc  # noqa: E402
import ai  # noqa: E402
from race_trace import RaceTrace  # noqa: E402

AIP = ROOT / "unpacked" / "MODELS" / "GARI" / "data" / "models" / "gari.aip"
OUT = ROOT / "Unreal" / "SSXTrickyRebuild" / "SimData"
TRACK = rc.load_track(str(AIP))
TRACK_RIDERS = (4, 0, 3)          # 0xdd8cc0
AI_RIDER = 3                      # 0xd2edb0
AI_SEED = 12345


class StreamB:
    """The game PRNG seeded with seed (0x2402C0); rand() = one 0x2401E0 step, the unsigned value callers take % n."""

    def __init__(self, seed):
        from rng import GameRng
        self.g = GameRng(seed=seed & 0xFFFFFFFF)

    def rand(self):
        return self.g.next()


def f(v):
    return repr(float(v))


def vec(v):
    return [f(x) for x in v]


def identify_ai_paths(t, i):
    pp = t.f32(i, 0x344)
    la = t.f32(i, 0x360, 3)
    out = np.full(t.count, -1)
    cur = None
    for k in range(t.count):
        if cur is None or np.linalg.norm(TRACK.ai_paths[cur].point_at(pp[k] + 800.0) - la[k]) > 2.0:
            cur = None
            for p in TRACK.ai_paths:
                if np.linalg.norm(p.point_at(pp[k] + 800.0) - la[k]) < 2.0:
                    cur = p.index
                    break
        out[k] = -1 if cur is None else cur
    return out


def identify_line_path(pos, dist_to_go):
    best, err = None, 1e30
    for p in TRACK.line_paths:
        prm, _, lat = p.project(pos)
        e = abs(p.dist_to_go_at_start - prm - dist_to_go) + 0.01 * lat
        if e < err:
            best, err = p, e
    return best


def spawn_csv(t):
    rows = ["slot,racer,count,size,x,y,z,heading"]
    for count in (6, 4, 1):
        for i in range(count):
            size = struct.unpack_from("<f", t.profiles[t.riders[i]], 0x60)[0]
            p, h, _ = rc.start_pose(TRACK, i, count, size)
            rows.append(",".join([str(rc.start_slot(i, count)), str(i), str(count), f(size)] + vec(p) + [f(h)]))
    (OUT / "gari.race_spawn.csv").write_text("\n".join(rows) + "\n")


def track_csv(t):
    head = ("rider,frame,px,py,pz,vx,vy,vz,finished,ai_path,param,param_prev,lateral,heading,lax,lay,laz,"
            "ptx,pty,ptz,line_path,dist_to_go,best,mask")
    rows = [head]
    for i in TRACK_RIDERS:
        pos, vel, fin = t.f32(i, 0x140, 3), t.f32(i, 0x150, 3), t.i32(i, 0x418)
        paths = identify_ai_paths(t, i)
        k0 = int(np.argmax(paths >= 0))
        lf = ai.LineFollow(TRACK.ai_paths[paths[k0]])
        tp = rc.TrackProgress(identify_line_path(pos[k0], t.f32(i, 0x374)[k0]))
        tp.best = float(t.f32(i, 0x378)[k0])
        rows.append(f"#init,{i},{paths[k0]},{tp.line.index},{f(tp.best)},{k0}")
        for k in range(k0, t.count):
            frame = int(t.frame[k])
            finished = bool(fin[k] >= 0)
            ai.follow_line(lf, TRACK, pos[k], vel[k], frame, finished, None)
            tp.update(pos[k], vel[k], TRACK, frame, finished, 0.0)
            rows.append(",".join([str(i), str(frame)] + vec(pos[k]) + vec(vel[k]) + [str(int(finished)), str(lf.path.index),
                                 f(lf.param), f(lf.param_prev), f(lf.lateral), f(lf.heading)] + vec(lf.lookahead)
                                 + vec(lf.point) + [str(tp.line.index), f(tp.dist_to_go), f(tp.best), str(tp.mask)]))
    (OUT / "gari.race_track.csv").write_text("\n".join(rows) + "\n")


def ai_csv(t, name):
    import validate_ai as va
    ram = t.spawn_ram()
    mode = struct.unpack_from("<i", ram, 0x32DE8C)[0]
    diff = struct.unpack_from("<i", ram, 0x32DE9C)[0] & 0xFFFF
    levels = rc.skill_levels(rc.EventMode(mode), diff)
    hr = np.array([va.human_rank(t, k) for k in range(t.count)])
    i = AI_RIDER
    d = va.load(t, i)
    paths = identify_ai_paths(t, i)
    st = ai.AIState()
    ai.reset_ai(st, levels)
    st.time_scale = float(d["ts"][0])
    rng = StreamB(AI_SEED)
    n = len(t.riders)
    cols = ["frame", "mode", "human_rank", "path", "px", "py", "pz", "vx", "vy", "vz", "sx", "sy", "sz", "fx", "fy", "fz",
            "ux", "uy", "uz", "yaw", "motion", "control", "pp", "ppp", "lat", "lax", "lay", "laz", "hd", "meter", "rank",
            "bc", "pbc", "char", "stat_air", "stat_trick", "gate_cur", "gate_target", "ptl", "ael", "plnx", "plny", "plnz",
            "spin", "flip", "cr", "near", "far", "g_active", "g_frame", "g_rival", "g_kind", "g_cooled"]
    cols += [f"rd{j}" for j in range(n)] + [f"rb{j}" for j in range(n)]
    for j in range(n):
        cols += [f"o{j}px", f"o{j}py", f"o{j}pz", f"o{j}vx", f"o{j}vy", f"o{j}vz", f"o{j}rank", f"o{j}human"]
    cols += ["word", "behaviour", "target", "skill", "time_scale"]
    rows = [f"#session,{name},rider,{i},mode,{mode},difficulty,{diff},skill_ahead,{f(levels[0])},skill_behind,"
            f"{f(levels[1])},ts0,{f(st.time_scale)},seed,{AI_SEED}", ",".join(cols)]
    prof = t.profiles[t.riders[i]]
    riders = [ai.OtherRider(j, np.zeros(3), np.zeros(3), 0, t.is_player(j)) for j in range(n)]
    for k in range(t.count):
        if paths[k] < 0:
            continue
        rv = va.view(t, i, k, paths[k], d)
        for j, o in enumerate(riders):
            o.pos = t.data[k, j, 0x140:0x14C].view("<f4").astype(float)
            o.vel = t.data[k, j, 0x150:0x15C].view("<f4").astype(float)
            o.rank = int(t.i32(j, 0x110)[k])
        race = ai.RaceView(frame=int(t.frame[k]), clock=0.0, mode=rc.EventMode(mode), riders=riders, human_rank=int(hr[k]))
        rv.control = int(d["ctl"][k - 1]) if k else rv.control
        w = ai.ai_word(st, rv, race, rng)
        g = rv.grudge
        row = [str(int(t.frame[k])), str(mode), str(int(hr[k])), str(paths[k])] + vec(rv.pos) + vec(rv.vel) + vec(rv.side) \
            + vec(rv.fwd) + vec(rv.up) + [f(rv.yaw), str(rv.motion), str(rv.control), f(rv.path_param), f(rv.path_param_prev),
                                          f(rv.path_lateral)] + vec(rv.lookahead) + [f(rv.path_heading), f(rv.boost_meter),
                                                                                       str(rv.rank), str(rv.board_class),
                                                                                       str(prof[0x44]), str(prof[0x5C]),
                                                                                       str(prof[0x14]), str(prof[0x1D]),
                                                                                       f(rv.gate_cur), f(rv.gate_target),
                                                                                       f(rv.pred_time_left), f(rv.air_elapsed)] \
            + vec(rv.pred_land_normal) + [f(rv.spin_angle), f(rv.flip_angle), str(rv.catchup_rival), f(rv.catchup_near),
                                          f(rv.catchup_far), str(int(g.active)), str(g.frame), str(g.rival), str(g.kind),
                                          str(int(g.cooled))]
        row += [f(x) for x in rv.rival_dist] + [f(x) for x in rv.rival_bearing]
        for o in riders:
            row += vec(o.pos) + vec(o.vel) + [str(o.rank), str(int(o.is_human))]
        row += [str(w), str(st.behaviour), str(st.target), f(st.skill), f(st.time_scale)]
        rows.append(",".join(row))
    (OUT / "gari.race_ai.csv").write_text("\n".join(rows) + "\n")


def respawn_csv():
    """Cases: (AI line, parameter, other racers [(pos, vel)]) -> placement, or ok = 0 when the crowd shift
    exceeds 800 cm. Row: case, path, param, n, then n x
    (pos, vel), then ok, x, y, z, heading, vx, vy, vz."""
    rows = []
    path = TRACK.ai_paths[TRACK.start_paths[0]]
    cases = []
    for c, param in enumerate((5000.0, 20000.0, 60000.0, 90000.0, 120000.0)):
        p = path.point_at(param)
        tng = path.tangent_at(param)
        side = np.cross(np.array([0.0, 0.0, 1.0]), tng)
        side /= np.linalg.norm(side)
        v = np.array([tng[0], tng[1], 0.0]) * 1500.0
        if c == 0:
            others = []
        elif c == 1:
            others = [(p + side * 50.0, v), (p + tng * 300.0 - side * 120.0, v * 0.5)]
        elif c == 2:
            others = [(p + side * 10.0, v), (p + side * 205.0, v)]
        elif c == 3:     # a wall of racers: shift beyond 800 cm -> fallback
            others = [(p + side * (-10.0 + 195.0 * k), v) for k in range(5)]
        else:            # racer far away (> 1000 cm) is ignored; one turned 40 deg is not adopted
            rot = np.array([[math.cos(0.7), -math.sin(0.7), 0], [math.sin(0.7), math.cos(0.7), 0], [0, 0, 1]])
            others = [(p + tng * 1200.0, v), (p - side * 30.0, rot @ v)]
        cases.append((param, others))
    for c, (param, others) in enumerate(cases):
        res = rc.respawn_point(path, param, others)
        row = [str(c), str(path.index), f(param), str(len(others))]
        for op, ov in others:
            row += vec(op) + vec(ov)
        if res is None:
            row += ["0"] + ["0"] * 7
        else:
            p, h, v = res
            row += ["1"] + vec(p) + [f(h)] + vec(v)
        rows.append(",".join(row))
    (OUT / "gari.race_respawn.csv").write_text("\n".join(rows) + "\n")


def main():
    name = sys.argv[1] if len(sys.argv) > 1 else "ride_session_1"
    t = RaceTrace(name)
    OUT.mkdir(parents=True, exist_ok=True)
    spawn_csv(t)
    respawn_csv()
    track_csv(t)
    ai_csv(t, name)
    print("wrote gari.race_spawn.csv, gari.race_respawn.csv, gari.race_track.csv, gari.race_ai.csv to", OUT)


if __name__ == "__main__":
    main()
