"""Integrated boarder model.

One deterministic 60 Hz `step(state, input_source, world)` running the per-frame sequence and dispatching to the
subsystem models through the state machine:

    ground motion / ground controls  -> models/ground.py
    air motion / NaturalAir / Spin    -> models/air.py
    ground probe contract, patches    -> models/collision.py
    wipe-out tumble / slide / get-up  -> models/wipeout.py
    pad -> mapper -> control word     -> models/input.py

Frame order (game order):
    1 approach filters, timers           2 motion update (by motion state)
    3 speed cap (not in Air)             4 input word read for the *current* control, control update
    5 decays, stuck / floor checks       6 Tricky timer and passive meter drain (race handler)
    7 lean filter

Worlds implement a small interface (probe, plane_under, race_line, floor_z, surface). Two are provided: an analytic
constant-slope plane and an analytic bicubic-patch course queried through the collision probe contract. Surface
parameters come from the exe surface table 0x256188.

Simplifications are marked ASSUMPTION (animation lengths, things not exported). The spawn state is a
PLACEHOLDER until the PCSX2 frame-1 dump is available.
"""
from __future__ import annotations

import csv
import math
import os
import sys
from collections import Counter
from dataclasses import dataclass, field
from typing import Optional

import numpy as np

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(HERE)
if HERE not in sys.path:
    sys.path.insert(0, HERE)

import air                      # noqa: E402
import collision as col         # noqa: E402
import ground as gnd            # noqa: E402
import input as pad             # noqa: E402
import state_machine as sm      # noqa: E402
import wipeout as wo            # noqa: E402

M, C = sm.MotionState, sm.ControlState
DT = 1.0 / 60.0
Z = np.array([0.0, 0.0, 1.0])

SPEED_TIERS = (2788.8394, 2932.43, 3072.1418, 3347.2222)   # cm/s
CAP_DECAY_PER_FRAME = 3.472222
BOOST_DRAIN_PER_FRAME = 0.0007501507                      # while boosting (boost handler)
PASSIVE_DRAIN = (15.483508, -15.210359, 9.166667e-06)     # race handler: (a + b*stat/255) * k per frame
TRICKY_WINDOW = 20.0                                       # s
STUCK_A_DECAY, STUCK_A_LIMIT = 0.95603746, 4.492007
STUCK_B_DECAY, STUCK_B_LIMIT = 0.9782825, 12.002146
FLOOR_MARGIN = 10000.0
RESET_WAIT_TIME = 0.8                                      # s until respawn
RESPAWN_DROP = 101.46                                      # cm (float passed by every forced reset)
RESPAWN_SPEED = 833.3333                                   # cm/s along the path tangent
LEAN_PER_STEER, LEAN_RATE = -60.0, 66.66667                # deg, deg/s
LAND_ABSORB_K, LAND_ABSORB_RATE = 0.00045, 8.3333
NATURAL_AIR_RELEASE_RATE = 4.0
PREWIND_CHARGE_RATE = 2.0
LAND_HARD_ANIM_SECONDS = 0.5     # fallback only; LandHard waits for its animation (gnd.anim_control_seconds)
WIPEOUT_START_SPIN = 9.0         # ASSUMPTION: rad/s demo value (rigid-body helpers not exported)

CONTROL_FROM_B = {"cruise": C.CRUISE, "prewind": C.PREWIND, "jump": C.JUMP, "change_fakie": C.CHANGE_FAKIE,
                  "fakie_cruise": C.FAKIE_CRUISE, "land_normal": C.LAND_NORMAL, "land_hard": C.LAND_HARD,
                  "natural_air": C.NATURAL_AIR, "spin": C.SPIN, "wipeout": C.WIPE_OUT,
                  "rail_slide": C.RAIL_SLIDE}
B_FROM_CONTROL = {v: k for k, v in CONTROL_FROM_B.items()}


def load_surface_table():
    """Surface table 0x256188 (25-field records, built by running the game's initialiser on the exe) ->
    ground.SurfaceParams per surface id."""
    if os.path.dirname(HERE) not in sys.path:
        sys.path.insert(0, os.path.dirname(HERE))
    import exe_tables
    table = {}
    for sid, f in enumerate(exe_tables.surface_table()):
        table[sid] = gnd.SurfaceParams(
            gravity=f[0], drag_lin=f[1], drag_quad=f[2], drag_cubic=f[3], grip=f[4], edge_deg=f[5],
            max_height=f[6], spring_depth=f[7], spring_max=f[8], normal_damping=f[9],
            cruise_kmh=f[11], propulsion_scale=f[12])
    return table


SURFACES = load_surface_table()


def surface_params(sid: int) -> gnd.SurfaceParams:
    return SURFACES.get(sid, SURFACES.get(1, gnd.SurfaceParams()))


class SlopeWorld:
    """Analytic plane through the origin falling along +Y at `slope_deg`; race line x = 0 heading +Y."""

    def __init__(self, slope_deg=15.0, surface_id=1):
        a = math.radians(slope_deg)
        self.slope = a
        self.n0 = np.array([0.0, math.sin(a), math.cos(a)])
        self.surface_id = surface_id
        self.floor_z = -1.0e7
        self._probe = gnd.plane_probe(slope_deg, surface_id)

    def probe(self, origin, old_normal) -> gnd.ProbeHit:
        return self._probe(origin, old_normal)

    def plane_under(self, pos):
        z = -pos[1] * math.tan(self.slope)
        return np.array([pos[0], pos[1], z]), self.n0.copy(), self.surface_id

    def race_line(self, pos):
        return np.array([0.0, pos[1], -pos[1] * math.tan(self.slope)]), math.pi / 2.0

    def spawn_frame(self, distance=0.0):
        a = self.slope
        fwd = np.array([0.0, math.cos(a), -math.sin(a)])
        return self.race_line(np.array([0.0, distance, 0.0]))[0], fwd, self.n0.copy()


def course_patch(oy, oz, size, drop, hump=0.0, surface=1):
    """Bicubic patch: x = -size/2 + size u, y = oy + size v, z = oz - drop v + 4 hump v (1 - v).

    u runs across the course and v along it, so dP/du x dP/dv points up (D's normal convention).
    """
    c = np.zeros((16, 3))
    c[0] = (-0.5 * size, oy, oz)
    c[4] = (size, 0.0, 0.0)                    # u
    c[1] = (0.0, size, -drop + 4.0 * hump)     # v
    c[2] = (0.0, 0.0, -4.0 * hump)             # v^2
    return col.Patch(c, surface)


class PatchWorld:
    """Analytic bicubic-patch course queried through D's ground-probe contract (collision.py)."""

    def __init__(self, n_patches=24, size=2000.0, slope_deg=15.0, hump_every=4, hump=260.0, surface_id=1):
        drop = size * math.tan(math.radians(slope_deg))
        self.patches = [course_patch(k * size, -k * drop, size, drop,
                                     hump if (hump_every and k % hump_every == hump_every - 1) else 0.0,
                                     surface_id) for k in range(n_patches)]
        self.world = col.World(self.patches)
        self.size, self.drop, self.surface_id = size, drop, surface_id
        self.floor_z = -n_patches * drop - 5.0e4

    def probe(self, origin, old_normal) -> gnd.ProbeHit:
        res = col.ground_probe(self.world, col.ProbeInput(
            position=np.asarray(origin, float), ground_normal=np.asarray(old_normal, float),
            forward=np.array([0.0, 1.0, 0.0]), contact_side=np.array([-1.0, 0.0, 0.0]), lean=0.0))
        if not res.found:
            return gnd.ProbeHit(False, np.asarray(origin, float), Z.copy(), self.surface_id)
        return gnd.ProbeHit(True, res.contact_point, res.ground_normal, res.surface)

    def plane_under(self, pos):
        hit = self.world.line_query(pos + np.array([0.0, 0.0, 400.0]), pos - np.array([0.0, 0.0, 3000.0]),
                                    query_type=2, t_ref=-1.0)
        if hit is None:
            return np.array([pos[0], pos[1], self.floor_z]), Z.copy(), self.surface_id
        return hit.point, hit.normal, hit.surface

    def race_line(self, pos):
        k = max(0, min(len(self.patches) - 1, int(pos[1] // self.size)))
        v = min(max((pos[1] - k * self.size) / self.size, 0.0), 1.0)
        p, _, dv = self.patches[k].eval(0.5, v)
        return p, math.atan2(dv[1], dv[0])

    def spawn_frame(self, distance=0.0):
        p, heading = self.race_line(np.array([0.0, distance, 0.0]))
        k = max(0, int(distance // self.size))
        _, du, dv = self.patches[k].eval(0.5, (distance - k * self.size) / self.size)
        n = du / np.linalg.norm(du)
        n = np.cross(n, dv / np.linalg.norm(dv))
        return p, dv / np.linalg.norm(dv), n / np.linalg.norm(n)


@dataclass
class BoarderState:
    rider: gnd.RiderState
    motion: M = M.GROUND
    control: C = C.LESSON_WAIT
    frame: int = 0
    profile: gnd.Profile = field(default_factory=gnd.Profile)
    stats: air.RiderStats = field(default_factory=air.RiderStats)
    boost_stat: int = 128                        # profile+0x1B (passive drain)
    tricky_timer: float = 0.0
    infinite_boost: bool = False
    stuck_a: float = 0.0
    stuck_b: float = 0.0
    finished: bool = False
    prewind_a: gnd.Filter = field(default_factory=gnd.Filter)
    prewind_b: gnd.Filter = field(default_factory=gnd.Filter)
    lean: gnd.Filter = field(default_factory=lambda: gnd.Filter(rate=LEAN_RATE))
    absorb: float = 0.25
    control_time: float = 0.0
    body: Optional[air.AirBody] = None
    spin: Optional[air.SpinState] = None
    scorer: air.TrickScorer = field(default_factory=air.TrickScorer)
    n_pred: np.ndarray = field(default_factory=lambda: Z.copy())
    wipe: Optional[wo.WipeoutState] = None
    reset_timer: float = 0.0
    auto_reset: bool = False
    last_word: int = 0
    events: list = field(default_factory=list)
    character: int = 0                           # profile+0x5C trick-set character (grab table)
    trick_table: Optional[dict] = None           # grab id -> animation data (air.load_trick_table)
    land_hard_anim: int = 0x242                  # LandHard animation chosen at touchdown (air 7.2)


def set_control(s: BoarderState, ctl: C, why: str = ""):
    if ctl != s.control:
        s.events.append((s.frame, "control", s.control.name, ctl.name, why))
        s.control, s.control_time = ctl, 0.0
        s.rider.control = B_FROM_CONTROL.get(ctl, s.rider.control)


def set_motion(s: BoarderState, mot: M, why: str = ""):
    if mot != s.motion:
        s.events.append((s.frame, "motion", s.motion.name, mot.name, why))
        s.motion = mot
        s.rider.motion = {M.GROUND: "ground", M.AIR: "air", M.RAIL_SLIDE: "rail"}.get(mot, "other")


def quat_from_axes(side, fwd, up):
    """Quaternion (w, x, y, z) with q*X = side, q*Y = fwd, q*Z = up (C's convention)."""
    m = np.column_stack([side, fwd, up])
    tr = m[0, 0] + m[1, 1] + m[2, 2]
    if tr > 0.0:
        w = math.sqrt(1.0 + tr) * 0.5
        return air.qnormalize(np.array([w, (m[2, 1] - m[1, 2]) / (4 * w), (m[0, 2] - m[2, 0]) / (4 * w),
                                        (m[1, 0] - m[0, 1]) / (4 * w)]))
    i = int(np.argmax([m[0, 0], m[1, 1], m[2, 2]]))
    j, k = (i + 1) % 3, (i + 2) % 3
    r = math.sqrt(max(1.0 + m[i, i] - m[j, j] - m[k, k], 1e-12))
    q = np.zeros(4)
    q[1 + i] = 0.5 * r
    q[0] = (m[k, j] - m[j, k]) / (2 * r)
    q[1 + j] = (m[j, i] + m[i, j]) / (2 * r)
    q[1 + k] = (m[k, i] + m[i, k]) / (2 * r)
    return air.qnormalize(q)


def spawn(world, distance=0.0, speed=0.0, board_class=0, control=C.LESSON_WAIT) -> BoarderState:
    """PLACEHOLDER spawn: race-line point at `distance`, resting in the snow, facing along the line.

    Replace with the PCSX2 frame-1 dump (position, velocity, orientation quaternion, filters).
    """
    p, fwd, n = world.spawn_frame(distance)
    S = surface_params(getattr(world, "surface_id", 1))
    rest = -S.spring_depth * n[2]
    rider = gnd.RiderState(pos=p + rest * n, vel=speed * fwd, fwd=fwd, up=n.copy(), normal=n.copy(),
                           slope_fwd=fwd.copy(), slope_left=np.cross(n, fwd), fall_line=fwd.copy(),
                           height=rest, board_class=board_class,
                           path_heading=world.race_line(p)[1], boost_meter=0.0)
    s = BoarderState(rider=rider, motion=M.GROUND, control=control)
    s.rider.control = B_FROM_CONTROL.get(control, "cruise")
    return s


def takeoff(s: BoarderState, world, from_jump: bool):
    r = s.rider
    side = np.cross(r.fwd, r.up)
    body = air.AirBody(pos=r.pos.copy(), vel=r.vel.copy(), q=quat_from_axes(side, r.fwd, r.up),
                       time_scale=r.time_scale, board_class=r.board_class, riding_fakie=r.riding_switch,
                       profile_goofy=getattr(s, "profile_goofy", False), character=s.character)
    pt, n, sid = world.plane_under(r.pos)
    t_land, s.n_pred = air.predict_touchdown(body.pos, body.vel, air.GroundPlane(pt, n, sid))
    body.time_to_land = t_land
    body.trick_table = s.trick_table
    s.body = body
    set_motion(s, M.AIR, "take-off")
    r.speed_cap = air.AIR_SPEED_CAP
    if from_jump:
        # the raw stance bit +0x1B4 = profile stance XOR riding switch (0x126E30 negates the flip seed on it)
        seed_s, seed_f = air.prewind_seeds(s.prewind_a.cur, s.prewind_b.cur, s.stats, r.board_class,
                                           getattr(s, "profile_goofy", False) != r.riding_switch)
        s.spin = air.spin_enter(body, air.SpinState(), seed_s, seed_f, seed_s != 0.0 or seed_f != 0.0, s.n_pred,
                                 prewind=(s.prewind_a.cur, s.prewind_b.cur))
        set_control(s, C.SPIN, "T26 take-off seeds")
    else:
        s.spin = None
        set_control(s, C.NATURAL_AIR, "T28 natural take-off")
    s.prewind_a = gnd.Filter()
    s.prewind_b = gnd.Filter()


def start_wipeout(s: BoarderState, impact_normal, why: str):
    r = s.rider
    r.boost_meter, s.tricky_timer = wo.start_wipeout_boost(r.boost_meter, s.tricky_timer,
                                                           s.scorer.value > 0.0, s.infinite_boost)
    s.wipe = wo.WipeoutState(p=r.pos.copy(), v=wo.entry_velocity(r.vel, impact_normal), spin=WIPEOUT_START_SPIN)
    s.body, s.spin = None, None
    set_control(s, C.WIPE_OUT, why)
    set_motion(s, M.WIPE_OUT, why)


def _local(v):
    """World -> D's wipe-out frame (rotate -90 deg about Z so the race line x = 0 maps to y = 0)."""
    return np.array([v[1], -v[0], v[2]])


def _world(v):
    return np.array([-v[1], v[0], v[2]])


class _SlopeAdapter:
    """D's Slope interface (normal, height) evaluated on the real world at the rider's position."""

    def __init__(self, world, pos_world):
        self.world = world
        _, n, sid = world.plane_under(pos_world)
        self.normal = _local(n)
        S = surface_params(sid)                 # slide uses fields 0 and 7
        self.gravity, self.p7 = S.gravity, S.spring_depth

    def height(self, p_local):
        pw = _world(p_local)
        pt, n, _ = self.world.plane_under(pw)
        return float(np.dot(pw - pt, n))


def motion_ground(s: BoarderState, world):
    r = s.rider
    r.path_heading = world.race_line(r.pos)[1]
    gnd.ground_motion(r, surface_params(r.surface_id), s.profile, world.probe)
    if r.motion == "air":
        takeoff(s, world, from_jump=(s.control == C.JUMP))
    elif r.motion == "wipeout":
        start_wipeout(s, r.normal, "T60 bail surface")
    elif r.control == "change_fakie" and s.control != C.CHANGE_FAKIE:
        set_control(s, C.CHANGE_FAKIE, "T27")


def motion_air(s: BoarderState, world):
    b, r = s.body, s.rider
    b.pos, b.vel = air.ballistic_step(b.pos, b.vel, b.time_scale / 60.0)
    b.air_time += DT
    b.time_to_land -= DT
    r.pos, r.vel, r.speed_cap = b.pos.copy(), b.vel.copy(), air.AIR_SPEED_CAP
    pt, n, sid = world.plane_under(b.pos)
    h = float(np.dot(b.pos - pt, n))
    S = surface_params(sid)
    if not (h < 0.5 * S.max_height and float(np.dot(n, b.vel)) < 0.0):
        return
    # touchdown (C 1.4 / 7): fold trick angles (SetControl(Void)), test, classify
    if h < 0.0:
        b.pos = b.pos - h * n
    air.fold_trick(b)
    verdict, psi, phi = air.classify_touchdown(b.q, b.vel, n, s.stats, r.board_class, not r.riding_switch, sid,
                                               b.anim_gated, b.grab_landable, b.anim_category == air.CAT_GRAB)
    vn = float(np.dot(n, b.vel))
    if verdict == "LandHard":
        s.land_hard_anim = air.land_hard_anim(psi, phi, vn, r.board_class, not r.riding_switch)
    side, fwd, up = air.basis(b.q)
    r.pos, r.fwd, r.up, r.normal, r.surface_id = b.pos.copy(), fwd, up, n.copy(), sid
    r.slope_fwd = gnd.unit(fwd - np.dot(fwd, n) * n, fwd)
    r.slope_left = np.cross(n, r.slope_fwd)
    r.height = float(np.dot(b.pos - pt, n))
    s.events.append((s.frame, "touchdown", verdict, round(math.degrees(psi), 1), round(math.degrees(phi), 1),
                     round(vn, 1)))
    if verdict == "WipeOut":
        reward = s.scorer.bail()
        r.boost_meter, s.tricky_timer = air.boost_add(r.boost_meter, reward, s.stats, s.tricky_timer)
        r.vel = b.vel.copy()
        start_wipeout(s, n, "T60 landing rejected")
        return
    r.vel = b.vel * air.landing_speed_factor(psi, phi)
    reward, _, _ = s.scorer.land()
    r.boost_meter, s.tricky_timer = air.boost_add(r.boost_meter, reward, s.stats, s.tricky_timer)
    s.absorb = min(max(-LAND_ABSORB_K * vn, 0.25), 1.0)
    s.body, s.spin = None, None
    set_motion(s, M.GROUND, "touchdown")
    set_control(s, C.LAND_NORMAL if verdict == "LandNormal" else C.LAND_HARD,
                "T52" if verdict == "LandNormal" else "T53")


def motion_wipeout(s: BoarderState, world):
    w, r = s.wipe, s.rider
    adapter = _SlopeAdapter(world, r.pos)
    lw = wo.WipeoutState(p=_local(w.p), v=_local(w.v), spin=w.spin, lying_angle=w.lying_angle, phase=w.phase,
                         total_time=w.total_time + DT, phase_time=w.phase_time, air_time=w.air_time)
    speed = float(np.linalg.norm(lw.v))
    if s.motion == M.WIPE_OUT and lw.phase == "tumble":
        grounded = wo.tumble_step(lw, adapter)
        if grounded and lw.phase_time > wo.SETTLE_PHASE_TIME and adapter.normal[2] > wo.STEEP_NZ:
            lw.phase, lw.phase_time = "slide", 0.0
        elif lw.phase_time >= wo.TUMBLE_PHASE_TIMEOUT or lw.total_time > wo.TUMBLE_TOTAL_TIMEOUT:
            if not s.finished:
                s.stuck_a = wo.STUCK_TIMEOUT_VALUE          # forces T05 on this frame's checks
            else:
                lw.phase, lw.phase_time = "recover", 0.0
    elif s.motion == M.WIPE_OUT:
        wo.slide_step(lw, adapter)
        if math.sin(lw.lying_angle) < wo.SLIDE_ALIGNED_SIN and speed < wo.getup_speed_limit(lw.phase_time):
            lw.phase, lw.phase_time = "recover", 0.0
            set_motion(s, M.WIPE_OUT_RECOVER, "T63")
            set_control(s, C.WIPE_OUT_RECOVER, "T63")
    else:                                               # WIPE_OUT_RECOVER: slide while the get-up plays
        wo.slide_step(lw, adapter)
        if s.control_time >= wo.getup_seconds(wo.DEFAULT_GETUP_ANIM, s.rider.board_class):
            _end_wipeout(s, world, lw)
            return
    s.wipe = wo.WipeoutState(p=_world(lw.p), v=_world(lw.v), spin=lw.spin, lying_angle=lw.lying_angle,
                             phase=lw.phase, total_time=lw.total_time, phase_time=lw.phase_time,
                             air_time=lw.air_time)
    r.pos, r.vel = s.wipe.p.copy(), s.wipe.v.copy()


def _end_wipeout(s: BoarderState, world, lw):
    r = s.rider
    r.pos, r.vel = _world(lw.p), _world(lw.v)
    pt, n, sid = world.plane_under(r.pos)
    _, heading = world.race_line(r.pos)
    f = np.array([math.cos(heading), math.sin(heading), 0.0])
    f = gnd.unit(f - np.dot(f, n) * n, f)
    r.fwd, r.up, r.normal, r.surface_id = f, n.copy(), n.copy(), sid
    r.slope_fwd, r.slope_left = f.copy(), np.cross(n, f)
    r.height = float(np.dot(r.pos - pt, n))
    s.wipe = None
    set_motion(s, M.GROUND, "T64")
    set_control(s, C.CRUISE, "T64")


def forced_reset(s: BoarderState, auto: bool, why: str):
    s.auto_reset, s.reset_timer = auto, 0.0
    s.body, s.spin, s.wipe = None, None, None
    set_motion(s, M.STATIC, why)
    set_control(s, C.RESET_WAIT, why)
    s.stuck_a = s.stuck_b = 0.0


def respawn(s: BoarderState, world):
    """T06 / (no rivals in these worlds): path point, tangent * 833.33, drop 101.46."""
    r = s.rider
    p, heading = world.race_line(r.pos)
    t = np.array([math.cos(heading), math.sin(heading), 0.0])
    pt, n, sid = world.plane_under(p)
    r.pos = pt + RESPAWN_DROP * Z
    r.vel = RESPAWN_SPEED * t
    r.fwd, r.up, r.normal = t.copy(), Z.copy(), n.copy()
    r.surface_id = sid
    s.tricky_timer = 0.0 if not s.infinite_boost else s.tricky_timer
    takeoff(s, world, from_jump=False)                 # spawn routine with drop > 0 -> Air + NaturalAir (T02)


def speed_cap(s: BoarderState):
    """0x117968 in float (C++ FRiderSim::ApplySpeedCap): skipped in the air; |v| by VSQRT; the tier from the burst timer,
    the boost strength and the meter (the second tier only while the meter is above 0); cap = max(tier, cap - 3.472222)
    (sub.s); above the cap v x (cap / |v|) (div.s, VMULx)."""
    from ee_math import v3, vlength
    from fpu import f32, fpu_sub
    r = s.rider
    if s.motion == M.AIR:
        return
    sp = vlength(v3(r.vel))
    tier = f32(SPEED_TIERS[0])
    if f32(0.0) < f32(r.burst_timer):
        tier = f32(SPEED_TIERS[3])
    elif f32(0.0) < f32(r.boost_strength):
        meter = f32(r.boost_meter)
        if f32(0.66599226) < meter:
            tier = f32(SPEED_TIERS[3])
        elif f32(0.33362994) < meter:
            tier = f32(SPEED_TIERS[2])
        elif f32(0.0) < meter:
            tier = f32(SPEED_TIERS[1])
    dropped = fpu_sub(f32(r.speed_cap), f32(CAP_DECAY_PER_FRAME))
    cap = dropped if tier < dropped else tier
    r.speed_cap = float(cap)
    if cap < sp:
        k = f32(cap / sp)
        r.vel = np.array([float(f32(k * f32(c))) for c in r.vel])


def boost_input(s: BoarderState, held: bool):
    """Boost handler: strength by meter tier while held, drain unless infinite."""
    r = s.rider
    if not held or r.boost_meter <= 0.0:
        r.boost_strength = 0.0
        return
    m = r.boost_meter
    # 0x11D038: +0x130 = 1.0 / 0x3F19F24E / 0x3E8003B9 (not 0.6 / 0.25)
    r.boost_strength = 1.0 if m > 0.66599226 else (0.60135353 if m > 0.33362994 else 0.2500284)
    if not s.infinite_boost:
        # no clamp here (0x11D038); the passive drain 0x11B1F8 zeroes a negative meter
        r.boost_meter = m - BOOST_DRAIN_PER_FRAME


GROUND_CONTROLS = {C.CRUISE, C.FAKIE_CRUISE, C.CHANGE_FAKIE, C.BUMP, C.PREWIND, C.JUMP, C.LAND_NORMAL,
                   C.LAND_HARD, C.GATE_TO_BASE}


def control_step(s: BoarderState, word: int, world):
    r = s.rider
    ci = pad.decode_word(int(s.control), word)
    s.control_time += DT
    if s.control in GROUND_CONTROLS and s.motion == M.GROUND:
        boost_input(s, ci.boost_held)
        if s.control == C.PREWIND and ci.jump_held:
            # 0x105A60: exponential charge, the flip target negated unless the stance bit is goofy
            goofy = getattr(s, "profile_goofy", False) != r.riding_switch
            s.prewind_a.target, s.prewind_a.rate = air.prewind_charge(s.prewind_a.cur, ci.prewind_spin, s.stats, goofy, False)
            s.prewind_b.target, s.prewind_b.rate = air.prewind_charge(s.prewind_b.cur, ci.prewind_flip, s.stats, goofy, True)
        jump = ci.jump_pressed if s.control == C.CRUISE else ci.jump_held
        sample = gnd.InputSample(turn=ci.turn, crouch=max(ci.crouch, 0.0), brake=max(ci.brake, 0.0),
                                 jump_held=jump, boosting=r.boost_strength > 0.0)
        before = r.control
        if s.control == C.LAND_HARD:
            try:
                hold = gnd.anim_control_seconds(s.land_hard_anim, r.board_class)
            except KeyError:
                hold = LAND_HARD_ANIM_SECONDS
            if s.control_time >= hold:
                set_control(s, C.PREWIND if ci.jump_held else C.CRUISE, "T18/T19 (LandHard animation done)")
        elif s.control == C.LAND_NORMAL:
            gnd.control_step(r, sample, s.profile)
            r.crouch.target = max(s.absorb, max(ci.crouch, 0.0))
            r.crouch.rate = LAND_ABSORB_RATE
            if r.crouch.cur >= s.absorb:
                set_control(s, C.PREWIND if ci.jump_held else C.CRUISE, "T21/T22")
        else:
            if s.control in (C.BUMP, C.GATE_TO_BASE):
                r.control = "cruise"
            gnd.control_step(r, sample, s.profile)
            new = CONTROL_FROM_B.get(r.control, s.control)
            if r.control != before or new != s.control:
                set_control(s, new, "B control")
        if r.motion == "air" and s.motion == M.GROUND:     # T25 jump launch inside the control step
            takeoff(s, world, from_jump=True)
    elif s.control == C.NATURAL_AIR:
        for flt in (r.steer, r.brake):
            flt.target, flt.rate = 0.0, NATURAL_AIR_RELEASE_RATE
        settled = r.steer.cur == 0.0 and r.brake.cur == 0.0
        if settled and (ci.turn != 0.0 or ci.crouch < 0.5 or ci.trick_pressed) and s.body is not None:
            s.spin = air.spin_enter(s.body, air.SpinState(), 0.0, 0.0, False, s.n_pred)
            set_control(s, C.SPIN, "T50")
    elif s.control == C.SPIN and s.body is not None:
        inp = air.SpinInput(spin=ci.spin, flip=ci.flip, late_spin=ci.late_spin, late_flip=ci.late_flip,
                            grab=ci.trick_id, tweak=ci.tweak, air_turn=int(ci.air_turn), air_tilt=int(ci.air_tilt))
        s.spin = air.spin_update(s.body, s.spin or air.SpinState(), inp, s.stats, s.scorer, s.n_pred,
                                 s.tricky_timer)
    elif s.control == C.LESSON_WAIT:
        set_control(s, C.CRUISE, "T07 (recording evidence)")
    elif s.control == C.RESET_WAIT:
        s.reset_timer += DT
        if s.reset_timer >= RESET_WAIT_TIME:
            respawn(s, world)
    fires, auto = sm.reset_rule(s.control, ci.reset_request, r.surface_id, s.finished)
    if fires:
        forced_reset(s, auto, "T05 reset rule")


def step(s: BoarderState, input_source, world) -> BoarderState:
    """Advance one 60 Hz frame (mutates and returns `s`).

    input_source: an int control word (already packed for the current control), or an
    input.MapperState, in which case the word is packed for the control state reached after the
    motion phase, which is what the game does.
    """
    r = s.rider
    # 1. approach filters and timers
    for flt in (r.steer, r.crouch, r.brake, s.prewind_a, s.prewind_b):
        flt.tick(DT)
    r.burst_timer = max(0.0, r.burst_timer - DT)
    # 2. motion
    if s.motion == M.GROUND:
        motion_ground(s, world)
    elif s.motion == M.AIR:
        motion_air(s, world)
    elif s.motion in (M.WIPE_OUT, M.WIPE_OUT_RECOVER):
        motion_wipeout(s, world)
    # 3. speed cap
    speed_cap(s)
    # 4. input and control
    word = input_source if isinstance(input_source, int) else pad.encode_word(int(s.control), input_source)
    s.last_word = word
    control_step(s, word, world)
    # 5. decays and forced resets
    s.stuck_a *= STUCK_A_DECAY
    s.stuck_b *= STUCK_B_DECAY
    if s.motion != M.STATIC and (s.stuck_a > STUCK_A_LIMIT or s.stuck_b > STUCK_B_LIMIT
                                 or r.pos[2] < world.floor_z - FLOOR_MARGIN):
        forced_reset(s, True, "T05 stuck/floor")
    # 6. race handler: Tricky timer (held at one frame while airborne) and passive meter drain
    if s.motion == M.AIR and 0.0 < s.tricky_timer <= DT:
        s.tricky_timer = DT
    else:
        s.tricky_timer = max(0.0, s.tricky_timer - DT)
    if s.infinite_boost:
        s.tricky_timer, r.boost_meter = 1.0, 1.0
    elif not s.finished and r.boost_meter != 0.0:
        a, b, k = PASSIVE_DRAIN
        r.boost_meter = max(0.0, r.boost_meter - (a + b * s.boost_stat / 255.0) * k)
    # 7. lean filter (degrees), faded below 1.44 cm/s, zero on rails
    sp = float(np.linalg.norm(r.vel))
    target = LEAN_PER_STEER * r.steer.cur * min(1.0, sp / 1.44)
    s.lean.target = 0.0 if s.motion == M.RAIL_SLIDE else target
    s.lean.tick(DT)
    r.lean_deg = s.lean.cur
    s.frame += 1
    return s


LOG_FIELDS = ["frame", "t", "x", "y", "z", "vx", "vy", "vz", "speed", "kmh", "motion", "control",
              "boost_meter", "tricky_timer", "steer", "crouch", "surface", "word_model", "word_recorded",
              "word_match"]


def replay(inp_path, world, out_csv=None, spawn_distance=0.0, spawn_speed=0.0, max_frames=None):
    """Drive the model from a tutorial .inp.

    The pad block of each frame goes through the tutorial mapper (BTNMAP0 + the rider's grab slots);
    the word is packed for the model's own control state, and compared with the recorded word.
    """
    name = os.path.basename(inp_path).lower()[:-6]
    mapper = pad.tutorial_mapper(inp_path)
    frames = pad.load_inp(inp_path)
    s = spawn(world, spawn_distance, spawn_speed, pad.TUTORIAL_BOARD_CLASS.get(name, 0))
    if name in pad.CHARACTER_PREFIX:
        s.character = pad.CHARACTER_PREFIX.index(name)
    s.trick_table = air.load_trick_table(s.rider.board_class, s.character)
    rows = []
    n = len(frames) if max_frames is None else min(max_frames, len(frames))
    for i in range(n):
        ms = mapper.step(frames[i], use_recorded_masks=False)
        step(s, ms, world)
        r = s.rider
        sp = float(np.linalg.norm(r.vel))
        rows.append([i, round(i * DT, 4), *[round(float(x), 3) for x in r.pos], *[round(float(x), 3) for x in r.vel],
                     round(sp, 3), round(sp * 0.036, 2), s.motion.name, s.control.name, round(r.boost_meter, 5),
                     round(s.tricky_timer, 3), round(r.steer.cur, 4), round(r.crouch.cur, 4), r.surface_id,
                     s.last_word, frames[i].recorded_word, int(s.last_word == frames[i].recorded_word)])
    if out_csv:
        os.makedirs(os.path.dirname(out_csv), exist_ok=True)
        with open(out_csv, "w", newline="", encoding="utf-8") as fh:
            w = csv.writer(fh)
            w.writerow(LOG_FIELDS)
            w.writerows(rows)
    return s, rows


def summarize(s: BoarderState, rows):
    speeds = [row[8] for row in rows]
    vmax = max(speeds) if speeds else 0.0
    t95 = next((row[1] for row in rows if row[8] >= 0.95 * vmax), None)
    t_cap = next((row[1] for row in rows if row[8] >= SPEED_TIERS[0] - 1.0), None)
    jumps = sum(1 for e in s.events if e[1] == "motion" and e[3] == "AIR")
    landings = sum(1 for e in s.events if e[1] == "touchdown" and e[2] != "WipeOut")
    bails = sum(1 for e in s.events if e[1] == "touchdown" and e[2] == "WipeOut")
    hist = Counter((row[10], row[11]) for row in rows)
    match = sum(row[19] for row in rows)
    return {"frames": len(rows), "max_speed_cms": vmax, "max_speed_kmh": round(vmax * 0.036, 1),
            "time_to_95pct_max_s": t95, "time_to_base_cap_s": t_cap, "jumps": jumps, "landings": landings,
            "bails": bails, "word_match_frames": match, "state_histogram": dict(hist.most_common())}


if __name__ == "__main__":
    default = os.path.join(os.path.dirname(os.path.dirname(ROOT)), "unpacked", "TUTORIAL", "BROD", "data",
                           "tutorial", "brod01.inp")
    path = sys.argv[1] if len(sys.argv) > 1 else default
    world = SlopeWorld(15.0, surface_id=1)
    out = os.path.basename(path)[:-4] + "_slope15.csv"
    st, log = replay(path, world, out)
    summ = summarize(st, log)
    print(f"replayed {os.path.basename(path)} on a 15 deg slope (surface 1 from the exe table) -> {out}")
    for k, v in summ.items():
        if k != "state_histogram":
            print(f"  {k:22s} {v}")
    print("  state histogram (motion, control): frames")
    for (mot, ctl), cnt in summ["state_histogram"].items():
        print(f"    {mot:16s} {ctl:16s} {cnt}")
    print("  first events:")
    for e in st.events[:16]:
        print("   ", e)
