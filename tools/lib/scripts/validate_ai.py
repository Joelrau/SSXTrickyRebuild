"""Validate models/ai.py and models/race.py against the live race traces (traces/live/ride_session_*).

Teacher forcing: every frame the AI model is fed the recorded rider state (position, velocity,
render-matrix axes, race-path fields, rival table, control/motion) and its own carried state
(behaviour, target, skill, trick plan). Its word is decoded and compared with what the recorded
control code did with the real word that frame:
  * crouch target   (+0x210) vs the word's crouch field   (Cruise, no brake)
  * steer target    (+0x21C) vs the word's turn field after the steer-input rule
  * boost           (+0x130 > 0) vs the word's boost bit (meter > 0)
  * jump timing     Cruise->Prewind (press) and Prewind->Jump (release) frames
  * catch-up        time_scale (+0x12C) vs the catch-up law
  * spawn           first-frame positions vs the start-slot rule (session 2 starts in the countdown)
Closed loop (kinematic): from a recorded frame, heading is integrated with the model's steer through
a yaw-rate fit (yaw_rate = K * steer, K fitted on the same trace), speed taken from the recording;
horizontal position / heading error after 1 s and 3 s.

Usage: python scripts/validate_ai.py [session ...]
"""
import math
import struct
import sys
from collections import Counter
from pathlib import Path

import numpy as np

HERE = Path(__file__).resolve().parent
sys.path.insert(0, str(HERE))
sys.path.insert(0, str(HERE.parent / "models"))
from race_trace import RaceTrace  # noqa: E402
import race as rc  # noqa: E402
import ai  # noqa: E402
import input as pad  # noqa: E402

AIP = str(HERE.parents[2] / "unpacked" / "MODELS" / "GARI" / "data" / "models" / "gari.aip")
TRACK = rc.load_track(AIP)


def identify_paths(t, i):
    """AI line per frame: the path whose point at (path_param + 800) equals the recorded look-ahead."""
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


def view(t, i, k, path_index, data):
    prof = t.profiles[t.riders[i]]
    rm = data["rm"][k]
    n = len(t.riders)
    rd = np.array([data["riv"][k, j, 0] for j in range(n)])
    rb = np.array([data["riv"][k, j, 1] for j in range(n)])
    g = rc.Grudge(bool(data["gr"][k, 0]), int(data["gr"][k, 1]), int(data["gr"][k, 2]), int(data["gr"][k, 3]),
                  bool(data["gr"][k, 4]))
    return ai.RiderView(
        index=i, pos=data["pos"][k], vel=data["vel"][k], side=rm[0], fwd=rm[1], up=rm[2],
        yaw=data["yaw"][k], motion=int(data["mot"][k]), control=int(data["ctl"][k]),
        path=TRACK.ai_paths[path_index], path_param=data["pp"][k], path_param_prev=data["ppp"][k],
        path_lateral=data["lat"][k], lookahead=data["la"][k], path_heading=data["hd"][k],
        boost_meter=data["meter"][k], rank=int(data["rank"][k]), board_class=int(data["bc"][k]),
        profile_board_class=prof[0x44], character=prof[0x5C], stat_air=prof[0x14], stat_trick=prof[0x1D],
        gate_cur=data["gate"][k, 0], gate_target=data["gate"][k, 2], pred_time_left=data["ptl"][k],
        air_elapsed=data["ael"][k], pred_land_normal=data["pln"][k], spin_angle=data["spin"][k],
        flip_angle=data["flip"][k], rival_dist=rd, rival_bearing=rb, catchup_rival=int(data["cr"][k]),
        catchup_near=data["near"][k], catchup_far=data["far"][k], grudge=g, finished=data["fin"][k] >= 0)


def load(t, i):
    d = dict(pos=t.f32(i, 0x140, 3), vel=t.f32(i, 0x150, 3), yaw=t.f32(i, 0x1B0), mot=t.i32(i, 0x424),
             ctl=t.i32(i, 0x428), pp=t.f32(i, 0x344), ppp=t.f32(i, 0x340), lat=t.f32(i, 0x348),
             la=t.f32(i, 0x360, 3), hd=t.f32(i, 0x370), meter=t.f32(i, 0x1C), rank=t.i32(i, 0x110),
             bc=t.i32(i, 0x420), gate=t.f32(i, 0x238, 3), ptl=t.f32(i, 0x57B8), ael=t.f32(i, 0x57FC),
             pln=t.f32(i, 0x57E0, 3), spin=t.f32(i, 0x1D0), flip=t.f32(i, 0x1D4), cr=t.i32(i, 0x100),
             near=t.f32(i, 0x104), far=t.f32(i, 0x108), fin=t.i32(i, 0x418), ts=t.f32(i, 0x12C),
             crouch_t=t.f32(i, 0x210), steer_t=t.f32(i, 0x21C), brake_t=t.f32(i, 0x204), bstr=t.f32(i, 0x130),
             steer_c=t.f32(i, 0x214))
    d["rm"] = t.f32(i, 0x4AA0, 12).reshape(-1, 3, 4)[:, :, :3]
    n = len(t.riders)
    d["riv"] = np.stack([t.f32(i, 0x40 + 0x20 * j, 2) for j in range(n)], 1)
    d["gr"] = np.stack([t.i32(i, o) for o in (0x118, 0x11C, 0x120, 0x124, 0x128)], 1)
    return d


def steer_rule(turn, boosting, v, board_class):
    if not boosting:
        turn = max(-0.9052, min(0.9052, turn))
    knee = {0: 1134.97, 1: 1119.01, 2: 1138.27}.get(board_class, 1130.0)
    r = v / knee
    return turn * r if r < 1.0 else turn


def human_rank(t, k):
    for j in range(len(t.riders)):
        if t.is_player(j):
            return int(t.i32(j, 0x110)[k])
    return 0


def run_session(name, out):
    t = RaceTrace(name)
    ram = t.spawn_ram()
    mode = struct.unpack_from("<i", ram, 0x32DE8C)[0]
    diff = struct.unpack_from("<i", ram, 0x32DE9C)[0] & 0xFFFF
    levels = rc.skill_levels(rc.EventMode(mode), diff)
    hr = np.array([human_rank(t, k) for k in range(t.count)])
    out.append(f"## {name}: mode {rc.EventMode(mode).name}, difficulty {diff}, skill levels "
               f"(AI ahead {levels[0]:.4f}, human ahead {levels[1]:.4f})")
    # spawn check (only meaningful when the trace starts before GO)
    errs = []
    for i, r in enumerate(t.riders):
        size = struct.unpack_from("<f", t.profiles[r], 0x60)[0]
        p, h, _ = ai.start_pose(TRACK, i, len(t.riders), size)
        errs.append((np.linalg.norm(p - t.f32(i, 0x140, 3)[0]), abs(rc.wrap_angle(h - t.f32(i, 0x1B0)[0]))))
    out.append(f"spawn: max position error {max(e[0] for e in errs):.3f} cm, max heading error "
               f"{math.degrees(max(e[1] for e in errs)):.4f} deg (frame {t.frame[0]}, control {t.i32(0, 0x428)[0]})")
    tot = Counter()
    for i in range(len(t.riders)):
        if t.is_player(i):
            continue
        d = load(t, i)
        paths = identify_paths(t, i)
        st = ai.AIState()
        ai.reset_ai(st, levels)
        st.time_scale = d["ts"][0]
        rng = ai.GameRNG(i)
        c = Counter()
        model_press, rec_press, model_rel, rec_rel = [], [], [], []
        steer_err = []
        others = []
        for k in range(t.count):
            if paths[k] < 0:
                c["no_path"] += 1
                continue
            rv = view(t, i, k, paths[k], d)
            riders = [ai.OtherRider(j, t.f32(j, 0x140, 3)[k], t.f32(j, 0x150, 3)[k], int(t.i32(j, 0x110)[k]),
                                    t.is_player(j)) for j in range(len(t.riders))] if not others else others
            for j, o in enumerate(riders):
                o.pos, o.vel = t.data[k, j, 0x140:0x14C].view("<f4").astype(float), \
                    t.data[k, j, 0x150:0x15C].view("<f4").astype(float)
            others = riders
            race = ai.RaceView(frame=int(t.frame[k]), clock=0.0, mode=rc.EventMode(mode), riders=riders,
                               human_rank=int(hr[k]))
            ctl_prev = int(d["ctl"][k - 1]) if k else rv.control
            rv.control = ctl_prev          # the word is built for the control active before this frame's update
            w = ai.ai_word(st, rv, race, rng)
            ctl = int(d["ctl"][k])
            v = np.linalg.norm(d["vel"][k])
            if ctl_prev == ai.CRUISE and int(d["mot"][k]) == 2:
                ci = pad.decode_word(ai.CRUISE, w)
                c["cruise"] += 1
                if d["brake_t"][k] == 0.0 and ci.brake == 0.0:
                    c["crouch_n"] += 1
                    c["crouch_ok"] += abs(ci.crouch - d["crouch_t"][k]) < 1e-3
                    c["boost_n"] += d["meter"][k] > 0.0
                    if d["meter"][k] > 0.0:
                        c["boost_ok"] += ci.boost_held == (d["bstr"][k] > 0.0)
                        c["boost_rec"] += d["bstr"][k] > 0.0
                        c["boost_mod"] += ci.boost_held
                if ctl == ai.CRUISE:
                    st_t = steer_rule(ci.turn, d["bstr"][k] > 0.0, v, int(d["bc"][k]))
                    steer_err.append(st_t - d["steer_t"][k])
                if w & 2:
                    model_press.append(k)
            if ctl_prev == ai.CRUISE and ctl == ai.PREWIND:
                rec_press.append(k)
            if ctl_prev == ai.PREWIND:
                pi = pad.decode_word(ai.PREWIND, w)
                if not pi.jump_held:
                    model_rel.append(k)
                if ctl == ai.JUMP:
                    rec_rel.append(k)
        se = np.abs(np.array(steer_err)) if steer_err else np.zeros(1)
        # jump press matching: each recorded press vs the nearest model press frame
        def match(rec, mod, tol=3):
            mod = np.array(mod)
            if not len(rec):
                return 0, 0, []
            dd = [int(np.min(np.abs(mod - r))) if len(mod) else 999 for r in rec]
            return sum(x <= tol for x in dd), len(rec), dd
        mp = match(rec_press, model_press)
        mr = match(rec_rel, model_rel, 1)
        out.append(f"{t.riders[i]:#x}: cruise frames {c['cruise']}, crouch target exact {c['crouch_ok']}/{c['crouch_n']} "
                   f"({100 * c['crouch_ok'] / max(c['crouch_n'], 1):.1f}%), boost state agree "
                   f"{c['boost_ok']}/{c['boost_n']} ({100 * c['boost_ok'] / max(c['boost_n'], 1):.1f}%; recorded "
                   f"{c['boost_rec']}, model {c['boost_mod']}), steer target |err| median {np.median(se):.3f} "
                   f"p90 {np.percentile(se, 90):.3f}, exact(<0.02) {100 * np.mean(se < 0.02):.1f}%, "
                   f"jump press within 3 f {mp[0]}/{mp[1]}, release within 1 f {mr[0]}/{mr[1]}, no-path frames {c['no_path']}")
        for key in ("crouch_ok", "crouch_n", "boost_ok", "boost_n"):
            tot[key] += c[key]
        tot["press_ok"] += mp[0]
        tot["press_n"] += mp[1]
        tot["rel_ok"] += mr[0]
        tot["rel_n"] += mr[1]
        tot["steer_exact"] += int(np.sum(se < 0.02))
        tot["steer_n"] += len(steer_err)
    out.append(f"session total: crouch {tot['crouch_ok']}/{tot['crouch_n']}, boost {tot['boost_ok']}/{tot['boost_n']}, "
               f"steer exact {tot['steer_exact']}/{tot['steer_n']}, jump press {tot['press_ok']}/{tot['press_n']}, "
               f"release {tot['rel_ok']}/{tot['rel_n']}")
    catchup_check(t, out)
    rollout_check(t, out)
    return tot


def catchup_check(t, out):
    worst, frac_slow = 0.0, []
    for i in range(len(t.riders)):
        if t.is_player(i):
            continue
        rv, near, far, hd, ts = t.i32(i, 0x100), t.f32(i, 0x104), t.f32(i, 0x108), t.f32(i, 0x370), t.f32(i, 0x12C)
        sim = ts[0]
        for k in range(1, t.count):
            j = rv[k]
            lead = None
            if j >= 0:
                dist = t.data[k, i, 0x40 + 0x20 * j:0x44 + 0x20 * j].view("<f4")[0]
                br = t.data[k, i, 0x44 + 0x20 * j:0x48 + 0x20 * j].view("<f4")[0]
                lead = rc.catchup_lead(float(dist), float(br), hd[k])
            sim = rc.step_time_scale(sim, rc.catchup_factor_ai(lead, near[k], far[k]))
            worst = max(worst, abs(sim - ts[k]))
        frac_slow.append(np.mean(ts < 0.71))
    out.append(f"catch-up: AI time_scale reproduced within {worst:.4f} (one step = 0.0084) over all frames; "
               f"fraction of frames at the 0.70 floor per AI: {', '.join(f'{x:.2f}' for x in frac_slow)}")


def rollout_check(t, out, horizons=(60, 180), stride=97):
    """Kinematic closed loop: heading driven by the model steer through the fitted yaw-rate gain."""
    errs = {h: [] for h in horizons}
    herr = {h: [] for h in horizons}
    for i in range(len(t.riders)):
        if t.is_player(i):
            continue
        d = load(t, i)
        paths = identify_paths(t, i)
        yaw = np.unwrap(np.arctan2(d["vel"][:, 1], d["vel"][:, 0]))
        rate = np.gradient(yaw) * 60.0
        g = (d["mot"] == 2) & (np.abs(d["steer_c"]) > 0.2)
        x = d["steer_c"][g] * d["ts"][g]
        K = float(np.sum(rate[g] * x) / max(np.sum(x ** 2), 1e-9))
        for k0 in range(0, t.count - max(horizons), stride):
            if paths[k0] < 0 or d["mot"][k0] != 2 or d["ctl"][k0] != ai.CRUISE:
                continue
            lf = ai.LineFollow(TRACK.ai_paths[paths[k0]])
            pos = d["pos"][k0].copy()
            hdg = yaw[k0]
            steer = d["steer_c"][k0]
            lf.param = d["pp"][k0]
            st = ai.AIState(skill=1.0)
            for h in range(1, max(horizons) + 1):
                k = k0 + h
                sp = np.linalg.norm(d["vel"][k, :2])
                ai.follow_line(lf, TRACK, pos, np.array([math.cos(hdg), math.sin(hdg), 0.0]) * sp, int(t.frame[k]), True)
                fwd = np.array([math.cos(hdg), math.sin(hdg), 0.0])
                side = np.array([math.sin(hdg), -math.cos(hdg), 0.0])
                rv = ai.RiderView(index=i, pos=pos, vel=fwd * sp, side=side, fwd=fwd, up=np.array([0, 0, 1.0]),
                                  yaw=hdg, motion=2, control=ai.CRUISE, path=lf.path, path_param=lf.param,
                                  path_param_prev=lf.param_prev, path_lateral=lf.lateral, lookahead=lf.lookahead,
                                  path_heading=lf.heading, boost_meter=0.0)
                turn = steer_rule(ai.steer_toward(rv, lf.lookahead, 1.0), False, sp, 0)
                rate_s = min(max(abs(turn - steer) * 7.0174, 0.1), 8.0183)
                steer += max(-rate_s / 60, min(rate_s / 60, turn - steer))
                ts = d["ts"][k]
                hdg += K * steer * ts / 60.0
                pos = pos + np.array([math.cos(hdg), math.sin(hdg), 0.0]) * sp * ts / 60.0
                pos[2] = d["pos"][k, 2]
                if h in horizons:
                    errs[h].append(np.linalg.norm(pos[:2] - d["pos"][k, :2]))
                    herr[h].append(abs(math.degrees(rc.wrap_angle(hdg - yaw[k]))))
    for h in horizons:
        e, he = np.array(errs[h]), np.array(herr[h])
        if len(e):
            out.append(f"kinematic rollout {h / 60:.0f} s ({len(e)} starts): horizontal error median {np.median(e):.0f} cm, "
                       f"p90 {np.percentile(e, 90):.0f} cm; heading error median {np.median(he):.1f} deg, p90 "
                       f"{np.percentile(he, 90):.1f} deg")


def main():
    names = sys.argv[1:] or ["ride_session_1", "ride_session_2"]
    out = []
    for n in names:
        run_session(n, out)
    print("\n".join(out))


if __name__ == "__main__":
    main()
