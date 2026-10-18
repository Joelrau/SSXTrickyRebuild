"""AI reset request (0x139AB8) and trick plan (0x139D58) against the live traces (SSX_TRACES).

The cComputer record (handler +0x868..+0x8A7 = cBoarder - 0x48 .. - 0x09, handler = *(cBoarder+0x5AE0) = cBoarder - 0x8B0)
is not in any frames.bin (trace_live.py records cBoarder +0..+0x600, +0x4600..+0x4B00, +0x57B0..+0x5800 and, with --crash,
handler +0x4F0..+0x600). Its start value is in every spawn_ram.bin; over time it is rebuilt here from the traced cBoarder
fields with the original rules:
  * skill +0x86C            0x13A220 every 60 race frames from +0x100 (rival) and +0x110 (rank) of me and the human
  * off-path count +0x894   0x139AB8 from +0x424 motion, +0x348 lateral (an unsigned distance), game mode 0x32DE8C
  * reset bit               0x139AB8 / 0x1399A8 / 0x139A30 from +0x370, +0x1B0, +0x57FC, +0x100/+0x104/+0x108, +0x40+0x20 r
  * trick plan +0x878..+0x88C seen through the air: +0x1E4 grab, release time left +0x57B8, spin / flip +0x1D0 / +0x1D4
Writes models/ai_state_vectors.json and prints the checks.

Usage: python scripts/ai_state_trace.py
"""
import json
import math
import struct
import sys
from pathlib import Path

import numpy as np

HERE = Path(__file__).resolve().parent
sys.path.insert(0, str(HERE))
from race_trace import RaceTrace  # noqa: E402

TRACES = ["ride_session_1", "ride_session_2", "replay_rep0001"]
GROUND_CTL = (2, 3, 4, 7, 11, 12, 18)        # 0x1384B8 cases calling 0x1389C8 (ground word -> 0x139AB8)
WIPE_CTL = (19, 20)                          # 0x138B60 / case 0x14 -> 0x139AB8
NO_TRICK = 26
F32 = np.float32


def wrap(x):
    """0x139AB8's wrap: x - floor(x / 2pi + 0.5) 2pi in single precision."""
    x = F32(x)
    q = F32(x * F32(0.15915494) + F32(0.5))
    f = F32(math.floor(q))
    return float(F32(x - f * F32(6.2831855)))


def lead_of(t, i, k, rival, hd):
    """-dist x cos(bearing - heading) (0x1399A8 / 0x139A30: dist x cos(bearing - heading + pi)). The rival table
    (0x115A80) at the input of frame k is the one of sample k - 1; the heading +0x370 is frame k's (step 11)."""
    d, b = t.f32(i, 0x40 + 0x20 * rival, 2)[k - 1]
    return float(F32(d) * F32(math.cos(F32(F32(b) - F32(hd)) + F32(math.pi))))


def skill_levels(ram, handler):
    return struct.unpack_from("<3f", ram, handler + 0x86C)   # current, human-ahead (+0x870), AI-ahead (+0x874)


def ai_block(ram, handler):
    f = struct.unpack_from("<4f", ram, handler + 0x868)
    i = struct.unpack_from("<4i", ram, handler + 0x878)
    g = struct.unpack_from("<2f", ram, handler + 0x888)
    j = struct.unpack_from("<4i", ram, handler + 0x890)
    return dict(req_speed=f[0], skill=f[1], skill_human_ahead=f[2], skill_ai_ahead=f[3], trick=i[0], spin_flip_on=i[1],
                short_hold=i[2], trick_allowed=i[3], spin_dir=g[0], flip_dir=g[1], target=j[0], offpath=j[1],
                wrongway=j[2], behaviour=j[3])


def run(name):
    t = RaceTrace(name)
    ram = t.spawn_ram()
    mode = struct.unpack_from("<i", ram, 0x32DE8C)[0]
    human = next(j for j in range(len(t.riders)) if t.is_player(j))
    hrank = t.i32(human, 0x110)
    out = dict(trace=name, game_mode=mode, frames=[int(t.frame[0]), int(t.frame[-1])], riders=[])
    for i, b in enumerate(t.riders):
        if i == human:
            continue
        h = struct.unpack_from("<I", ram, b + 0x5AE0)[0]
        spawn = ai_block(ram, h)
        _, lv_h, lv_a = skill_levels(ram, h)
        prof = t.profiles[b]
        stat_trick, stat_air = prof[0x1D], prof[0x14]
        T = 0.8 / (stat_air / 255.0 * 0.8057834 + 0.78212035)
        ctl, mot = t.i32(i, 0x428), t.i32(i, 0x424)
        lat, hd, yaw = t.f32(i, 0x348), t.f32(i, 0x370), t.f32(i, 0x1B0)
        el, tl, ts = t.f32(i, 0x57FC), t.f32(i, 0x57B8), t.f32(i, 0x12C)
        riv, near, far = t.i32(i, 0x100), t.f32(i, 0x104), t.f32(i, 0x108)
        rank, grab = t.i32(i, 0x110), t.i32(i, 0x1E4)
        spin, flip = t.f32(i, 0x1D0), t.f32(i, 0x1D4)
        skill = spawn["skill"]
        off = spawn["offpath"]
        ww_free, ww_eff = 0, 0                                  # wrong-way run counted as if no target (upper bound)
        max_ww, max_off_scaled = 0, 0.0
        skill_series = []
        wipe_vec, reset_ok, reset_bad, late_wipe = [], 0, 0, []
        for k in range(1, t.count):
            # control at step 13 of frame k: a landing bail in the motion step (8) of frame k already set WipeOut
            # (sample k = 22 after a Spin sample); a first WipeOut sample after a non-wipe sample is ambiguous
            # (step 8, or a later step of frame k: then the input still ran the old control)
            first_wipe = ctl[k] in WIPE_CTL and ctl[k - 1] not in WIPE_CTL
            if ctl[k] == 22 and ctl[k - 1] != 22:
                c = 19
            elif ctl[k] in WIPE_CTL and not first_wipe:
                c = int(ctl[k])
            else:
                c = int(ctl[k - 1])
            called = c in GROUND_CTL or c in WIPE_CTL
            if called:
                err = wrap(hd[k] - yaw[k])
                ww_free = ww_free + 1 if abs(err) > 1.0471976 else 0
                m = int(mot[k]) if ctl[k] != 22 else 5
                if m == 5:
                    off += 1
                elif mode == 0:
                    off = off + 1 if lat[k] >= 1500.0 else 0
                elif m in (1, 6):
                    off = 0
                elif lat[k] >= 1000.0:
                    off += 1
                    ww_eff = max(ww_eff, ww_free)     # the flag survives only off the path (or in knockdown)
                else:
                    off = 0                           # on the path: the flag cleared, the +0x898 count is kept
                sc = float(F32(off) * (F32(skill) / F32(31.410845))) if skill != 1.0 else float(off)
                max_ww, max_off_scaled = max(max_ww, ww_free), max(max_off_scaled, sc)
                if mode == 0:
                    ww_eff = max(ww_eff, ww_free)
                if c in WIPE_CTL or ctl[k] == 22:
                    want = sc > 222.0
                    lead = lead_of(t, i, k, int(riv[k]), hd[k]) if riv[k] >= 0 else None
                    behind = lead is not None and lead < near[k]
                    ahead = lead is not None and far[k] < lead
                    pred = (want or behind) and not ahead
                    obs = bool(ctl[k] == 22)
                    reset_ok += pred == obs
                    reset_bad += pred != obs
                    if obs or abs((lead or 0) - near[k]) < 60 or pred != obs:
                        wipe_vec.append(dict(frame=int(t.frame[k]), control_at_input=c, motion=int(m),
                                             lead=round(lead, 2) if lead is not None else None, near=float(near[k]),
                                             far=float(far[k]), offpath=int(off), skill=float(skill), time_scale=float(ts[k]),
                                             reset_expected=pred, reset_seen=obs))
            if first_wipe and riv[k] >= 0:
                lead = lead_of(t, i, k, int(riv[k]), hd[k])
                if lead < near[k] and not far[k] < lead:
                    late_wipe.append(int(t.frame[k]))   # the rule would have reset: this wipe-out began after step 13
            if t.frame[k] % 60 == 0:                # 0x13A220 runs after the word
                new = lv_h if (riv[k] < 0 or hrank[k] < rank[k]) else lv_a
                if new != skill:
                    skill_series.append([int(t.frame[k]), round(new, 6)])
                skill = new
        # trick plans at AI take-off zones (Cruise/approach -> Prewind = the jump press, 0x137858)
        plans = []
        k = 1
        while k < t.count:
            if ctl[k] == 14 and ctl[k - 1] != 14:
                press = k
                while k < t.count and mot[k] != 1:
                    k += 1
                s = k
                while k < t.count and mot[k] == 1:
                    k += 1
                if k >= t.count:
                    break
                air = range(s, k)
                held = [j for j in air if grab[j] != NO_TRICK]
                rel_tl = float(tl[held[-1] + 1]) if held and held[-1] + 1 < k else None
                hold = None if rel_tl is None else (rel_tl < 0.31)
                plans.append(dict(press_frame=int(t.frame[press]), skill=None, grab=int(grab[held[0]]) if held else NO_TRICK,
                                  release_time_left=None if rel_tl is None else round(rel_tl, 3), short_hold=hold,
                                  spin_sign=int(np.sign(spin[k - 1])) if abs(spin[k - 1]) > 0.01 else 0,
                                  flip_sign=int(np.sign(flip[k - 1])) if abs(flip[k - 1]) > 0.01 else 0,
                                  landing_control=int(ctl[k]), time_scale=round(float(ts[k - 1]), 4),
                                  air_elapsed=round(float(el[k - 1]), 4), time_left=round(float(tl[k - 1]), 4)))
            else:
                k += 1
        # skill at each press from the rebuilt series
        for p in plans:
            sk = spawn["skill"]
            for f, v in skill_series:
                if f < p["press_frame"]:
                    sk = v
            p["skill"] = sk
        n = len(plans)
        ng = sum(p["grab"] != NO_TRICK for p in plans)
        holds = [p["short_hold"] for p in plans if p["short_hold"] is not None]
        exp_hold = np.mean([sum(1 for r in range(100) if 20.0 < r * (0.0 if p["skill"] == 1.0 else 1.0 - p["skill"] / 14.454015))
                            / 100.0 for p in plans]) if plans else 0.0
        rel_long = [p["release_time_left"] for p in plans if p["short_hold"] is False]
        out["riders"].append(dict(
            racer=i, name=prof[4:12].split(b"\0")[0].decode(), stat_trick=int(stat_trick), stat_air=int(stat_air),
            spawn_ai_record=spawn, skill_changes=skill_series, max_wrongway_run_no_target=int(max_ww), max_wrongway_run_off_path=int(ww_eff),
            max_offpath_scaled=round(max_off_scaled, 3), wipe_reset_frames=wipe_vec,
            wipe_reset_agree=[reset_ok, reset_ok + reset_bad], late_wipeouts=late_wipe, trick_plans=plans,
            grab_rate=[int(ng), n, round(min(1.0, (stat_trick + 16) / 100.0), 3)],
            short_hold_rate=[int(sum(holds)), len(holds), round(float(exp_hold), 3)],
            long_release_T=[round(T, 3), rel_long]))
    return out


def main():
    res = [run(n) for n in TRACES]
    for r in res:
        print(f"== {r['trace']} (mode {r['game_mode']})")
        for x in r["riders"]:
            sa = x["spawn_ai_record"]
            print(f" racer {x['racer']} {x['name']:8s} skill {sa['skill']:.4f} (+0x870 {sa['skill_human_ahead']:.4f}, "
                  f"+0x874 {sa['skill_ai_ahead']:.4f}) changes {len(x['skill_changes'])}; wrong-way run <= {x['max_wrongway_run_no_target']} (off path {x['max_wrongway_run_off_path']})"
                  f" (limit 60); off-path scaled <= {x['max_offpath_scaled']} (limit 222); wipe reset agree "
                  f"{x['wipe_reset_agree'][0]}/{x['wipe_reset_agree'][1]}; grabs {x['grab_rate'][0]}/{x['grab_rate'][1]} "
                  f"(p {x['grab_rate'][2]}); short hold {x['short_hold_rate'][0]}/{x['short_hold_rate'][1]} "
                  f"(p {x['short_hold_rate'][2]}); T {x['long_release_T'][0]} seen {x['long_release_T'][1]}")
            for w in x["wipe_reset_frames"]:
                if w["reset_expected"] != w["reset_seen"]:
                    print("   MISMATCH", w)
    path = HERE.parent / "models" / "ai_state_vectors.json"
    path.write_text(json.dumps(res, indent=1, default=lambda o: o.item() if hasattr(o, "item") else str(o)))
    print("wrote", path)


if __name__ == "__main__":
    main()
