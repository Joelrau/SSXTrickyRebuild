"""Reference data for the SSX.Ground automation tests, generated from models/ground.py.
Usage:  python tools/ground_reference.py
Writes  Unreal/SSXTrickyRebuild/SimData/ground_reference.csv   (frame-by-frame grounded steps)
Prints  the models/ground.py demo numbers at full precision (hard-coded in GroundTests.cpp).
"""
import libpath  # noqa: F401,E402  (tools/lib on sys.path)
import csv
import math
import sys
from dataclasses import replace
from pathlib import Path

import numpy as np

ROOT = Path(__file__).resolve().parent.parent
import ground as g  # noqa: E402

OUT = ROOT / "Unreal" / "SSXTrickyRebuild" / "SimData" / "ground_reference.csv"
STAT128 = 128.0 / 255.0


def profile128() -> g.Profile:
    return g.Profile(stat_speed=STAT128, stat_brake=STAT128, stat_grip=STAT128, stat_gate=STAT128,
                     stat_landing=STAT128, stat_jump=STAT128, stat_aero=STAT128, lean_offset=0.0)


def run_scenario(name, slope, speed, S, P, inputs, frames, path_heading_from_spawn=True, stop_on_air=True):
    probe = g.plane_probe(slope)
    st = g.spawn_on_plane(slope, speed, S)
    rows = []
    for i in range(frames):
        inp = inputs(i)
        st = g.step(st, inp, S, P, probe)
        rows.append([name, i, slope, *st.pos, *st.vel, *st.fwd, *st.up, st.steer.cur, st.crouch.cur, st.brake.cur,
                     st.height, st.motion, st.control,
                     -1.0 if st.last_launch is None else st.last_launch])
        if stop_on_air and st.motion != "ground":
            break
    return rows


def demo_numbers():
    S, P = g.SurfaceParams(), g.Profile()
    slope = 20.0
    probe = g.plane_probe(slope)
    st = g.spawn_on_plane(slope, 0.0, S)
    tuck = g.InputSample(crouch=1.0)
    top, t95 = 0.0, None
    for i in range(60 * 60):
        st = g.step(st, tuck, S, P, probe)
        sp = g.norm(st.vel)
        top = max(top, sp)
        if t95 is None and sp >= 0.95 * 2788.84:
            t95 = (i + 1) * g.DT
    print(f"run: top {top:.9f} t95 {t95:.9f} motion {st.motion}")

    flat = g.plane_probe(0.0)
    for stick in (0.5, 1.0):
        st = g.spawn_on_plane(0.0, 1500.0, g.SurfaceParams(drag_lin=0.0, drag_quad=0.0, drag_cubic=0.0))
        Sc = g.SurfaceParams(drag_lin=0.0, drag_quad=0.0, drag_cubic=0.0, cruise_kmh=0.0)
        inp = g.InputSample(turn=stick, crouch=1.0)
        heads = []
        for i in range(150):
            st = g.step(st, inp, Sc, P, flat)
            heads.append((math.atan2(st.vel[1], st.vel[0]), g.norm(st.vel)))
        (h0, _), (h1, v1) = heads[-31], heads[-1]
        omega = g.wrap_pi(h1 - h0) / (30 * g.DT)
        print(f"carve {stick}: steer {st.steer.cur:.9f} speed {v1:.9f} omega_deg {math.degrees(omega):.9f} "
              f"radius_cm {v1 / abs(omega):.9f}")

    for stat in (0.5, 1.0):
        Pj = replace(P, stat_jump=stat)
        for charge in (0.0, 0.5, 1.0):
            st = g.spawn_on_plane(slope, 2500.0, S)
            st.launch_charge, st.crouch.cur = charge, 0.0
            J = g.launch(st, Pj, g.JUMP_MIN)
            print(f"jump stat {stat} charge {charge}: J {J:.9f} v ({st.vel[0]:.9f}, {st.vel[1]:.9f}, {st.vel[2]:.9f})")
    for speed in (500.0, 1500.0):
        for charge in (0.5, 1.0):
            print(f"impulse speed {speed} charge {charge} stat 1.0: {g.launch_impulse(speed, charge, replace(P, stat_jump=1.0), 0.0):.9f}")

    st = g.spawn_on_plane(slope, 1500.0, S)
    for i in range(30):
        st = g.step(st, g.InputSample(jump_held=True), S, P, probe)
    charge = st.crouch.cur
    frames = 0
    while st.motion == "ground" and frames < 30:
        st = g.step(st, g.InputSample(), S, P, probe)
        frames += 1
    print(f"prewind: charge {charge:.9f} frames {frames} J {st.last_launch:.9f} control {st.control}")

    for args in ((0.1, 0.1, -900.0), (0.6, 0.4, -1500.0), (0.2, 0.2, -3000.0), (0.2, -1.0, -900.0)):
        r = g.classify_landing(*args, P)
        print(f"land {args}: {r['kind']} factor {r['speed_factor']:.9f} absorb {r['absorb']:.9f}")


def main():
    demo_numbers()
    P = profile128()
    S = g.SurfaceParams()
    Sc = g.SurfaceParams(drag_lin=0.0, drag_quad=0.0, drag_cubic=0.0, cruise_kmh=0.0)
    rows = []
    rows += run_scenario("run20", 20.0, 0.0, S, P, lambda i: g.InputSample(crouch=1.0), 600)
    rows += run_scenario("carve05", 0.0, 1500.0, Sc, P, lambda i: g.InputSample(turn=0.5, crouch=1.0), 150)
    rows += run_scenario("carve10", 0.0, 1500.0, Sc, P, lambda i: g.InputSample(turn=1.0, crouch=1.0), 150)

    def sturn(i):
        turn = 0.8 if (i // 90) % 2 == 0 else -0.8
        brake = 0.6 if 300 <= i < 330 else 0.0
        return g.InputSample(turn=turn, crouch=0.0, brake=brake)
    rows += run_scenario("sturn15", 15.0, 800.0, S, P, sturn, 600)
    rows += run_scenario("prewind20", 20.0, 1500.0, S, P, lambda i: g.InputSample(jump_held=i < 30), 60)
    OUT.parent.mkdir(parents=True, exist_ok=True)
    with open(OUT, "w", newline="", encoding="utf-8") as fh:
        w = csv.writer(fh)
        w.writerow(["scenario", "frame", "slope", "px", "py", "pz", "vx", "vy", "vz", "fx", "fy", "fz", "ux", "uy", "uz",
                    "steer", "crouch", "brake", "height", "motion", "control", "launch_j"])
        for r in rows:
            w.writerow([f"{x:.12g}" if isinstance(x, float) else x for x in r])
    print(f"wrote {OUT} ({len(rows)} rows)")


if __name__ == "__main__":
    main()
