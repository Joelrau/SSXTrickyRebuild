"""Grounded riding model.

One call of `step()` is one 60 Hz frame for a grounded rider, in the game's order:
approach filters -> ground motion -> speed cap -> control (input) handling.
Units: cm, s, rad; Z up. Positive steer turns the board to its left (counter-clockwise from above).

The surface parameters are PLACEHOLDERS (the real table is filled at run time and has not been
recovered). Structure and hard-coded constants are from the code.
"""
from __future__ import annotations

import math
from dataclasses import dataclass, field, replace
from typing import Callable, Optional

import numpy as np

from ee_math import (asin_ee, atan2_ee, cos_ee, quat_from_basis, rotate_body_ee, sin_ee, sincos_ee, sqrt32, v3, vcross,
                     vdot, vlength, vnormalize)
from fpu import f32, fpu_add, fpu_sub

DT = 1.0 / 60.0
Z = np.array([0.0, 0.0, 1.0])
KMH = 1000.0 / 36.0          # cm/s per km/h


# Animations are authored at 30 fps and play at 0.5 animation frames per game frame (speed 1). A
# one-shot animation of n frames is "done" after 2 (n - 1) game frames; the control that waits for it
# (by seeing the follow-on loop on channel 0) leaves on that frame or the next. Frame counts per
# board class (0 bx, 1 fr, 2 ex animation set; None = that set has no such animation), from the .afl
# files via scripts/afl.py and checked against the live traces.
ANIM_FPS = 30.0
ANIM_FRAMES = {
    0x1FD: (21, 21, 14),          # gate push (Gate2Base)
    0x1FE: (56, 56, 61),          # start-line sit-up (GetupFromSit)
    0x200: (21, 15, 20),          # ChangeFakie (R_SWITCH)
    0x201: (None, None, 20),      # fakie un-switch
    0x203: (None, None, 20),      # FakieCruise turn-around (ex only)
    0x204: (None, None, 20),
    0x221: (36, 36, 30),          # skating push / 2GO
    0x23E: (31, 31, 31),          # LandHard, random of 3 variants: ex also has 25-frame variants
    0x23F: (35, 31, None),
    0x240: (None, None, 25),
    0x241: (16, 19, 9),
    0x242: (26, 26, 25),
    0x243: (26, 26, None),
    0x244: (30, 16, 20),          # Bump
    0x245: (28, 16, 20),
    0x246: (27, 17, 25),
    0x247: (25, 19, 25),
    0x248: (36, 24, 40),
    0x249: (36, 23, 40),
}
LAND_HARD_ATOMIC_VARIANTS = {0: (31, 31, 31), 1: (31, 31, 31), 2: (31, 25, 25)}   # 0x23E, picked at random
CONTROL_EXIT_LATENCY_FRAMES = 1   # the waiting control usually sees the follow-on loop one frame later


def anim_seconds(anim_id: int, board_class: int = 0) -> float:
    """Time from start to 'done' of a one-shot animation: (n - 1) / 30 s."""
    n = ANIM_FRAMES[anim_id][board_class]
    if n is None:
        raise KeyError(f"animation {anim_id:#x} does not exist for board class {board_class}")
    return (n - 1) / ANIM_FPS


def anim_control_seconds(anim_id: int, board_class: int = 0) -> float:
    """How long a control that waits for this animation stays in place (done + exit latency)."""
    return anim_seconds(anim_id, board_class) + CONTROL_EXIT_LATENCY_FRAMES * DT


@dataclass
class SurfaceParams:
    """25-float surface record. Defaults are placeholders, not game data."""
    gravity: float = 980.0          # [0]  rail code hard-codes 980, assumed equal
    drag_lin: float = 0.02          # [1]  1/s                       PLACEHOLDER
    drag_quad: float = 0.01         # [2]  1/s per 1000 cm/s         PLACEHOLDER
    drag_cubic: float = 0.004       # [3]                            PLACEHOLDER
    grip: float = 8.0               # [4]  1/s                       PLACEHOLDER
    edge_deg: float = 30.0          # [5]  deg at steer 1            PLACEHOLDER
    max_height: float = 30.0        # [6]  cm above ground before take-off  PLACEHOLDER
    spring_depth: float = 2.0       # [7]  cm                        PLACEHOLDER
    spring_max: float = 10.0        # [8]  cm                        PLACEHOLDER
    normal_damping: float = 8.0     # [9]  1/s                       PLACEHOLDER
    cruise_kmh: float = 30.0        # [11] km/h                      PLACEHOLDER
    propulsion_scale: float = 1.0   # [12]                           PLACEHOLDER


ICE_SURFACES = (3, 4)          # drag factor, steer-rate cut
DRAG_LOAD_K = 1.0   # DAT_0031D308: the drag load factor k (1.0 in every live RAM dump; once read as 0)
WIPEOUT_SURFACES = (6, 10)


@dataclass
class Profile:
    """Rider stats as 0..1 fractions (byte/255). Names are by use."""
    stat_speed: float = 0.5      # profile+0x0E: linear drag down, skating gain up
    stat_brake: float = 0.5      # profile+0x11: brake drag
    stat_grip: float = 0.5       # profile+0x13: lateral grip
    stat_gate: float = 0.5       # profile+0x15: gate push
    stat_landing: float = 0.5    # profile+0x16: landing pitch tolerance
    stat_jump: float = 0.5       # profile+0x17: jump impulse
    stat_aero: float = 0.5       # profile+0x19: quadratic drag down
    lean_offset: float = 0.0     # profile float +0x60: probe side offset per lean degree


@dataclass
class Filter:
    """A's approach filter: cur moves toward target by at most rate*dt per frame."""
    cur: float = 0.0
    rate: float = 0.0
    target: float = 0.0

    def tick(self, dt: float = DT) -> None:
        # 0x117198 step 4 on the EE FPU (FApproachFilter::Tick): s = rate x dt in float; target + s < cur -> cur - s,
        # cur < target - s -> cur + s, else the target as stored
        c, t, s = f32(self.cur), f32(self.target), f32(self.rate) * f32(dt)
        if fpu_add(t, s) < c:
            self.cur = float(fpu_sub(c, s))
        elif c < fpu_sub(t, s):
            self.cur = float(fpu_add(c, s))
        else:
            self.cur = self.target


@dataclass
class InputSample:
    turn: float = 0.0        # -1..1, already decoded from the input word
    crouch: float = 0.0      # 0..1
    brake: float = 0.0       # 0..1 (magnitude; the game stores a signed value, only brake^2 matters)
    jump_held: bool = False
    boosting: bool = False   # boost button held with meter available (strength from A's tiers)


@dataclass
class ProbeHit:
    hit: bool
    point: np.ndarray
    normal: np.ndarray
    surface_id: int


ProbeFn = Callable[[np.ndarray, np.ndarray], ProbeHit]   # (origin, old normal) -> hit


@dataclass
class RiderState:
    pos: np.ndarray
    vel: np.ndarray
    fwd: np.ndarray                       # board forward axis
    up: np.ndarray                        # board up axis
    normal: np.ndarray = field(default_factory=lambda: Z.copy())
    slope_fwd: np.ndarray = field(default_factory=lambda: np.array([0.0, 1.0, 0.0]))
    slope_left: np.ndarray = field(default_factory=lambda: np.array([-1.0, 0.0, 0.0]))
    fall_line: np.ndarray = field(default_factory=lambda: np.array([0.0, 1.0, 0.0]))
    height: float = 0.0                   # ground_height (signed, cm)
    surface_id: int = 1
    steer: Filter = field(default_factory=Filter)
    crouch: Filter = field(default_factory=Filter)
    brake: Filter = field(default_factory=Filter)
    lean_deg: float = 0.0                 # A's lean filter output (-60 * steer), used by the probe
    boost_strength: float = 0.0
    burst_timer: float = 0.0
    boost_meter: float = 0.5
    speed_cap: float = 2788.84
    motion: str = "ground"                # ground / air / rail
    control: str = "cruise"               # cruise / prewind / jump / change_fakie / land_normal / land_hard / natural_air / spin
    launch_charge: float = 0.0
    jump_latch: bool = False
    board_class: int = 0
    riding_switch: bool = False
    path_heading: float = math.pi / 2     # race path heading (yaw convention atan2(fwd.y, fwd.x))
    push_anim: bool = False               # skating push animation playing
    anim_left: float = 0.0                # s left on the current one-shot ground animation (push / turn-around)
    time_scale: float = 1.0
    last_launch: Optional[float] = None   # J of the last take-off (for the demo)
    quat: Optional[tuple] = None          # body quaternion (x, y, z, w) float32, the master of fwd / up
    qkey: Optional[tuple] = None

    def copy(self) -> "RiderState":
        return replace(self, pos=self.pos.copy(), vel=self.vel.copy(), fwd=self.fwd.copy(),
                       up=self.up.copy(), normal=self.normal.copy(), slope_fwd=self.slope_fwd.copy(),
                       slope_left=self.slope_left.copy(), fall_line=self.fall_line.copy(),
                       steer=replace(self.steer), crouch=replace(self.crouch), brake=replace(self.brake))


def norm(v: np.ndarray) -> float:
    return float(np.linalg.norm(v))


def unit(v: np.ndarray, fallback: Optional[np.ndarray] = None) -> np.ndarray:
    n = norm(v)
    if n < 1e-9:
        return fallback.copy() if fallback is not None else v.copy()
    return v / n


def rotate(v: np.ndarray, axis: np.ndarray, ang: float) -> np.ndarray:
    """Right-handed world rotation (Rodrigues), as the quaternion helper does."""
    a = unit(axis)
    c, s = math.cos(ang), math.sin(ang)
    return v * c + np.cross(a, v) * s + a * np.dot(a, v) * (1.0 - c)


def enter_change_fakie(st: RiderState) -> None:
    """ChangeFakie Enter: the rider is turned to face its motion at once.
    Stance flips, the board turns by pi about its own up axis, steer (and so the lean) is negated, and the
    R_SWITCH animation plays; the control then waits for its follow-on loop. vf is positive afterwards,
    so the fakie check does not fire again."""
    st.control = "change_fakie"
    st.riding_switch = not st.riding_switch
    rotate_board(st, st.up, math.pi)
    st.steer.cur = -st.steer.cur
    st.lean_deg = -st.lean_deg
    st.anim_left = anim_control_seconds(0x200, st.board_class)   # R_SWITCH plays out


def wrap_pi(a: float) -> float:
    return (a + math.pi) % (2.0 * math.pi) - math.pi


def switch_scale(st: RiderState) -> float:
    """Switch-stance multiplier shared by drag and grip."""
    if not st.riding_switch or st.board_class == 1:
        return 1.0
    return 0.84998363 if st.board_class == 0 else 0.69987833


# Single-precision mirror of Sim/Ground/GroundPhysics.cpp + GroundMotion.cpp (0x10A0D8 and its callees). Every
# product / sum is rounded to float32 in the order the C++ issues it; EE FPU adds go through fpu_add / fpu_sub
# (guard-bit adder), VU0 sums stay IEEE. State vectors are float64 arrays holding float32 values.
DEPTH_FLOOR = -10.0


def _c(x):
    return f32(x)


def _sp(S: SurfaceParams):
    return (_c(S.gravity), _c(S.drag_lin), _c(S.drag_quad), _c(S.drag_cubic), _c(S.grip), _c(S.edge_deg),
            _c(S.max_height), _c(S.spring_depth), _c(S.spring_max), _c(S.normal_damping), _c(S.cruise_kmh),
            _c(S.propulsion_scale))


def _stat_lerp(byte, k, c):
    """(float)byte x k x (1/255) + c: separate mul.s, then add.s."""
    return fpu_add((byte * _c(k)) * _c(0.003921569), _c(c))


def _byte(stat):
    return _c(stat * 255.0)


def _ensure_quat(st: RiderState) -> None:
    """The body quaternion is the master state (RotateBodyEE); fwd / up are its basis. Code that sets fwd / up directly
    (the older boarder.py paths) re-derives it."""
    key = (st.fwd.tobytes(), st.up.tobytes())
    if st.quat is None or st.qkey != key:
        fwd, up = np.asarray(st.fwd, float), np.asarray(st.up, float)
        st.quat = quat_from_basis(np.cross(fwd, up), fwd, up)
        st.qkey = None


def _set_basis(st: RiderState, res) -> None:
    st.quat, _side, fwd, up = res
    st.fwd, st.up = fwd.astype(np.float64), up.astype(np.float64)
    st.qkey = (st.fwd.tobytes(), st.up.tobytes())


def rotate_body(st: RiderState, axis: np.ndarray, ang: float) -> None:
    """FRiderSim::RotateBody: the axis normalised in double, then RotateBodyEE in float."""
    _ensure_quat(st)
    a = unit(np.asarray(axis, float), Z)
    _set_basis(st, rotate_body_ee(st.quat, v3(a), f32(ang)))


def rotate_board(st: RiderState, axis: np.ndarray, ang: float) -> None:
    rotate_body(st, axis, ang)


def normal_support(h, vn, S: SurfaceParams) -> np.float32:
    """0x109878."""
    g, damp, p7, p8 = _sp(S)[0], _sp(S)[9], _sp(S)[7], _sp(S)[8]
    h, vn = _c(h), _c(vn)
    zero = _c(0.0)
    if zero < h:
        a = -(g / _c(30.0)) * h
        if zero < vn:
            a = fpu_add(a, (-damp) * vn)
        return a
    if -p7 < h:
        a = ((-g) * h) / p7
    else:
        hc = h if -p8 < h else -p8
        t = fpu_add(hc, p7)
        a = g * fpu_sub(_c(1.0), fpu_add(t, t) / fpu_sub(p8, p7))
    return fpu_add(a, (-damp) * vn)


def path_alignment(yaw, path_heading) -> np.float32:
    """0x109950: w = min((pi/3 - |wrap(yaw - path)|) / (pi/6), 1)."""
    d = fpu_sub(yaw, path_heading)
    t = fpu_add(d * _c(0.15915494), _c(0.5))
    fl = f32(int(t))
    if t < fl:
        fl = fpu_sub(fl, _c(1.0))
    w = fpu_sub(_c(1.0471976), abs(fpu_sub(d, fl * _c(6.2831855)))) / _c(0.5235988)
    return w if w < _c(1.0) else _c(1.0)


def propulsion(st: RiderState, S: SurfaceParams, P: Profile, speed, w, fwd_z) -> np.float32:
    """0x109950 (single precision)."""
    zero = _c(0.0)
    brake = _c(st.brake.cur)
    if zero < brake:
        return zero
    sp = _sp(S)
    b = _c(st.boost_strength)
    if zero < _c(st.burst_timer) and b < w * _c(5.5):
        b = w * _c(5.5)
    acc = zero
    if zero < b:
        gate = fpu_sub(_c(0.07983315), abs(_c(st.steer.cur)))
        if zero < gate:
            bg = b * gate
            acc = fpu_add(zero, bg * _c(2350.3096) if fwd_z < zero else bg * fpu_add(fwd_z * _c(1053.4484), _c(2350.3096)))
    deficit = fpu_sub(sp[10] * _c(27.777777), speed)
    if not (deficit < _c(1111.1111)):
        deficit = _c(1111.1111)
    if zero < deficit:
        byte = _byte(P.stat_speed)
        ks = _stat_lerp(byte, 0.3049984, 1.2049915) if st.board_class == 2 else _stat_lerp(byte, 0.27690744, 0.7380923)
        aligned = fpu_add((w * ks) * deficit, zero) if zero <= w else zero
        pushing = fpu_add((ks * _c(0.2)) * deficit, zero) if st.push_anim else zero
        acc = fpu_add(acc, aligned if pushing < aligned else pushing)
    return sp[11] * acc


def longitudinal_drag(st: RiderState, vf, S: SurfaceParams, P: Profile, load) -> np.float32:
    """0x109CB8; `load` = (k N) / S[0] in float (the support before the n.z < 0 clamp)."""
    sp = _sp(S)
    one = _c(1.0)
    L = load if one <= load else one
    c_quad = _stat_lerp(_byte(P.stat_aero), -0.29035342, 1.2848105)
    c_brk = _stat_lerp(_byte(P.stat_brake), 0.9181778, 0.6150995)
    if st.board_class == 2:
        c_lin = _stat_lerp(_byte(P.stat_speed), -0.30197912, 0.70764244)
    else:
        c_lin = _stat_lerp(_byte(P.stat_speed), -0.30845523, 1.0649822)
    if st.riding_switch and st.board_class != 1:
        sw = _c(switch_scale(st))
        c_brk, c_quad, c_lin = c_brk * sw, c_quad * sw, c_lin * sw
    ice = fpu_sub(_c(0.5), _c(st.height) / sp[8]) if st.surface_id in ICE_SURFACES else one
    x = abs(vf) * _c(0.001)
    lin = (L * sp[1]) * c_lin
    stand = fpu_sub(one, _c(st.crouch.cur)) * _c(0.10572864)
    brake = _c(st.brake.cur)
    brk = (((fpu_add(_c(st.boost_strength) * _c(1.2153687), one) * _c(1.7513076)) * brake) * brake) * c_brk
    quad = x * fpu_add(L * sp[2], x * ((L * sp[3]) * c_quad))
    return ((-vf) * ice) * fpu_add(fpu_add(fpu_add(lin, stand), brk), quad)


def grip_curve(x) -> np.float32:
    # 0x109EF8 literals, single precision
    x = _c(x)
    if x < _c(555.55554):
        return fpu_add(fpu_sub(x, _c(0.0)) * _c(0.0008899726), _c(0.20103703))
    if x < _c(1388.8888):
        return fpu_add(fpu_sub(x, _c(555.55554)) * _c(0.00036129795), _c(0.6954663))
    return fpu_add(fpu_sub(x, _c(1388.8888)) * _c(-0.00010441317), _c(0.9965479))


def lateral_grip(st: RiderState, vs, vf, S: SurfaceParams, P: Profile) -> np.float32:
    sp = _sp(S)
    x = vs * grip_curve(abs(vf))
    e = fpu_add(abs(_c(st.steer.cur)) * _c(1.0001484), _c(0.0))
    if not (e < _c(1.0)):
        x = x * e
    g_stat = _stat_lerp(_byte(P.stat_grip), 1.1412175, 0.001) * _c(switch_scale(st))
    a = (x * -sp[4]) * g_stat
    if _c(0.0) < _c(st.boost_strength):
        a = a * (_c(1.0) / fpu_add(_c(st.boost_strength) * _c(3.4999945), _c(1.0)))
    return a


YAW_LEAD = {0: (0.3001892, 0.34918553), 1: (0.4, 0.5239824), 2: (0.20833896, 0.2622103)}


def yaw_increment(v, n, f, fall, steer, crouch, board_class, dt) -> np.float32:
    """0x10A0D8 step 9: the board yaw controller (GroundPhysics.cpp YawIncrement)."""
    one = _c(1.0)
    speed = sqrt32(vdot(v, v))
    if speed == 0:
        return _c(0.0)
    q = one / speed
    sb = vdot(q * vcross(v, f), n)
    sb = _c(-0.99999) if sb < _c(-0.99999) else (_c(0.99999) if _c(0.99999) < sb else sb)
    beta = asin_ee(sb)
    ka, kb = (_c(c) for c in YAW_LEAD[board_class])
    err = fpu_add(-beta, ((fpu_add((ka * _c(0.5)) * fpu_sub(one, steer * steer), one) * steer) * kb) / fpu_add(crouch * _c(0.01), one))
    if vdot(v, f) < _c(0.0):
        err = -err
    gain = ((speed * _c(2.0029847e-05)) * speed) * dt
    if one < gain:
        gain = one
    vfall = vdot(v, fall)
    e1 = speed / _c(555.55554)
    if one < e1:
        e1 = one
    e = (e1 * fpu_sub(_c(0.5), abs(vfall) / speed)) * _c(40.0)
    if e < _c(0.0):
        e = _c(0.0)
    e = fpu_add(e, _c(0.01004205))
    if one < e:
        e = one
    steer_dir = -steer if err <= _c(0.0) else steer
    gain = gain * e if steer_dir < e else gain * steer_dir
    a = err * gain
    lim = _c(0.10471976)
    return -lim if a < -lim else (lim if lim < a else a)


def run_probe(st: RiderState, probe: ProbeFn, P: Profile) -> bool:
    origin = st.pos + P.lean_offset * st.lean_deg * st.slope_left
    hit = probe(origin, st.normal)
    if not hit.hit:
        st.normal = Z.copy()
        st.slope_fwd = unit(st.fwd - np.dot(st.fwd, Z) * Z, st.fwd)
        st.slope_left = np.cross(st.normal, st.slope_fwd)
        st.fall_line = st.fwd.copy()
        return False
    st.surface_id = hit.surface_id
    st.normal = np.asarray(hit.normal, float)
    n = st.normal
    st.slope_fwd = unit(st.fwd - np.dot(st.fwd, n) * n, st.fwd)
    st.slope_left = np.cross(n, st.slope_fwd)
    st.height = float(np.dot(origin - hit.point, n))
    across = np.cross(n, Z)
    st.fall_line = np.cross(n, unit(across)) if norm(across) > 1e-4 else st.fwd.copy()
    return True


def ground_motion(st: RiderState, S: SurfaceParams, P: Profile, probe: ProbeFn,
                  wall_hit: bool = False) -> None:
    sp = _sp(S)
    zero, one = _c(0.0), _c(1.0)
    _ensure_quat(st)
    nf, ff, lf = v3(st.normal), v3(st.slope_fwd), v3(st.slope_left)
    pf, vf3 = v3(st.pos), v3(st.vel)
    vn, vf, vs = vdot(vf3, nf), vdot(vf3, ff), vdot(vf3, lf)
    dtf = _c(st.time_scale) * _c(0.016666668)
    steer = _c(st.steer.cur)
    g = sp[0]

    # 2. the four scalar accelerations
    support = normal_support(st.height, vn, S)
    yaw = atan2_ee(st.fwd[1], st.fwd[0])
    w = path_alignment(yaw, _c(st.path_heading))
    prop = propulsion(st, S, P, vlength(vf3), w, ff[2])
    drag = longitudinal_drag(st, vf, S, P, (_c(DRAG_LOAD_K) * support) / g)
    grip = lateral_grip(st, vs, vf, S, P)
    if nf[2] < zero and support < zero:
        support = zero

    # 3. edge angle
    theta = (sp[5] * _c(0.017453292)) * steer

    # 4. fakie check (T27)
    if st.brake.cur == 0.0 and vf < _c(-111.11111) and st.control in ("cruise", "fakie_cruise", "land_normal"):
        enter_change_fakie(st)

    # 5. acceleration
    sin_t, cos_t = sincos_ee(theta)
    g2 = g + g
    m_over_c = (support if support <= g2 else g2) / cos_t
    tilted = cos_t * nf + sin_t * lf
    af = fpu_sub(support, m_over_c) * nf + m_over_c * tilted
    af = af + fpu_add(drag, prop) * ff
    af = af + grip * lf
    af = af + v3([0.0, 0.0, -g])

    # 6. penetration clamp, 7. explicit Euler (position first)
    d = fpu_add(sp[8], _c(st.height))
    d = d if d >= _c(DEPTH_FLOOR) else _c(DEPTH_FLOOR)
    if d < zero:
        pf = pf - d * nf
        st.height = float(fpu_sub(_c(st.height), d))
        vf3 = vf3 - vn * nf
        an = vdot(af, nf)
        if an < zero:
            af = af - an * nf
    st.pos = (pf + dtf * vf3).astype(np.float64)
    st.vel = (vf3 + dtf * af).astype(np.float64)

    # 9. steering yaw, skipped only in Prewind with zero steer
    if not (st.control == "prewind" and st.steer.cur == 0.0):
        dyaw = yaw_increment(v3(st.vel), nf, ff, v3(st.fall_line), steer, _c(st.crouch.cur), st.board_class, dtf)
        _set_basis(st, rotate_body_ee(st.quat, nf, dyaw))

    on_ground = run_probe(st, probe, P)

    # 12. slope-change redirect (speed kept, 40 % of the normal component removed), not on deep snow
    if st.surface_id not in ICE_SURFACES:
        nn = v3(st.normal)
        v = v3(st.vel)
        vnn = vdot(v, nn)
        speed = vlength(v)
        v = v - (vnn * _c(0.4)) * nn
        speed2 = vlength(v)
        if speed2 != 0:
            v = (speed / speed2) * v
        st.vel = v.astype(np.float64)

    if not wall_hit and (not on_ground or st.height > sp[6]):
        natural_takeoff(st, P)
        return
    if st.surface_id in WIPEOUT_SURFACES:
        st.motion, st.control = "wipeout", "wipeout"
        return

    # 15. tilt the board up toward normalize(n + (0, 0, 0.5 n.z)) by 23.33 % of the angle
    nn = v3(st.normal)
    t = vnormalize(nn + v3([0.0, 0.0, nn[2] * _c(0.5)]))
    cr = vcross(t, v3(st.up))
    sc = vlength(cr)
    if _c(1e-05) < sc:
        axis = (one / sc) * cr
        if _c(0.99999) < sc:
            sc = _c(0.99999)
        _set_basis(st, rotate_body_ee(st.quat, axis, -asin_ee(sc) * _c(0.23333861)))


def apply_speed_cap(st: RiderState) -> None:
    b = st.boost_strength
    if st.burst_timer > 0.0 or (b > 0.0 and st.boost_meter > 0.666):
        tier = 3347.22
    elif b > 0.0 and st.boost_meter > 0.3336:
        tier = 3072.14
    elif b > 0.0:
        tier = 2932.43
    else:
        tier = 2788.84
    st.speed_cap = tier if tier >= st.speed_cap else max(tier, st.speed_cap - 3.4722)
    sp = norm(st.vel)
    if st.motion != "air" and sp > st.speed_cap:
        st.vel = st.vel * (st.speed_cap / sp)


JUMP_MIN = 630.8786010742188  # 0x441DB83B (float)


def launch_impulse(speed: float, charge: float, P: Profile, j_min: float) -> float:
    # 0x1284E0 in float: f_v = |v| 0.88093346 + 24.678448 below 993.4801, else 899.86835; J = (c (c s)) f_v with
    # s = (float)byte17 x 0.78700274 x (1/255) + 0.09851468; at least JMin
    sp, c = _c(speed), _c(charge)
    fv = fpu_add(sp * _c(0.88093346), _c(24.678448)) if sp < _c(993.4801) else _c(899.86835)
    s = _stat_lerp(_byte(P.stat_jump), 0.78700274, 0.09851468)
    j = (c * (c * s)) * fv
    return float(_c(j_min) if j < _c(j_min) else j)


def launch(st: RiderState, P: Profile, j_min: float) -> float:
    """Apply the take-off impulse and return J."""
    cf = fpu_sub(_c(st.launch_charge), _c(st.crouch.cur))
    charge = _c(0.0) if cf < _c(0.0) else cf
    vf3 = v3(st.vel)
    J = launch_impulse(vlength(vf3), charge, P, j_min)
    jf = _c(J)
    nf, ff = v3(st.normal), v3(st.slope_fwd)
    one, zero = _c(1.0), _c(0.0)
    if st.motion == "rail":
        st.vel = (vf3 + jf * v3(st.up)).astype(np.float64)
        return J
    c50, c70 = cos_ee(_c(0.8726646)), cos_ee(_c(1.2217305))
    steep = fpu_sub(nf[2], c50) * (one / fpu_sub(c70, c50))
    if ff[2] < zero or steep < zero or vf3[2] < zero:
        d = vnormalize(nf + _c(0.2) * ff)
        st.vel = (vf3 + jf * d).astype(np.float64)
    else:
        hn = vnormalize(v3([nf[0], nf[1], 0.0]))
        q = vnormalize(ff - vdot(ff, hn) * hn)
        s0, s20 = sin_ee(_c(0.0)), sin_ee(_c(0.34906584))
        bt = steep * fpu_sub(q[2], s0) * (one / fpu_sub(s20, s0))
        b = zero if bt < zero else (_c(0.9) if _c(0.9) < bt else bt)
        om = fpu_sub(one, b)
        d = vnormalize(om * nf + b * q)
        mix = om * vf3 + (b * vlength(vf3)) * q
        st.vel = (_c(0.5) * mix + jf * d).astype(np.float64)
    return J


def natural_takeoff(st: RiderState, P: Profile) -> None:
    was_jump = st.control == "jump"
    st.motion = "air"
    if not was_jump:
        st.launch_charge = st.crouch.cur
        st.last_launch = launch(st, P, 0.0)
        st.control = "natural_air"
    else:
        st.last_launch = launch(st, P, 0.0)
        st.control = "spin"


def classify_landing(yaw_err: float, pitch: float, impact_vn: float, P: Profile,
                     board_class: int = 0, riding_switch: bool = False) -> dict:
    """yaw_err in [0, pi] (board vs velocity), pitch (rad, already sign-corrected), impact_vn < 0."""
    scaled = pitch * (2.0408 - 1.0374 * P.stat_landing)
    if not (-2.968 <= scaled <= 2.684):
        return {"kind": "wipeout", "speed_factor": 1.0, "absorb": 0.0}
    ok_pitch = pitch >= -math.pi / 4
    forward = abs(yaw_err) <= math.pi / 2
    if board_class == 2:
        if forward and not riding_switch:
            kind = "land_normal" if (impact_vn >= -2777.1 and ok_pitch) else "land_hard"
        else:
            kind = "land_hard"
    else:
        kind = "land_normal" if (ok_pitch and impact_vn >= -2777.1) else "land_hard"
    p = min(max(abs(pitch), math.radians(15)), math.radians(50))
    y = min(max(abs(yaw_err), math.radians(25)), math.radians(80))
    factor = (1.06425 - 0.24542 * p) * (1.11359 - 0.26032 * y)
    absorb = min(max(-0.00045 * impact_vn, 0.25), 1.0)
    return {"kind": kind, "speed_factor": factor, "absorb": absorb}


# exact constants of 0x126948 / 0x126C00 / 0x126C78 (bit patterns from the asm)
STEER_CLAMP = f32(0.90518558)       # 0x3F67BA3E
STEER_RATE_K = f32(7.0173593)       # 0x40E08E35
STEER_RATE_MIN = f32(0.1)           # 0x3DCCCCCD
STEER_RATE_MAX = f32(8.0183487)     # 0x41004B28
DEEP_SNOW_STEER_RATE = f32(0.5999726)   # 0x3F1997CE
CROUCH_RATE_K = f32(5.0024881)      # 0x40A01462
BRAKE_RATE_K = f32(5.0353317)       # 0x40A12170
PREWIND_RELEASE_RATE = f32(6.66666651)  # 0x40D55555
CROUCH_UNWIND_RATE = f32(13.333333)     # 0x41555555
STEER_KNEE = {0: f32(1134.9739), 1: f32(1119.0095), 2: f32(1138.2727)}   # 0x448DDF2A / 0x448BE04E / 0x448E48BA


def _vu_length(v) -> np.float32:
    """VU0 |v|: VMUL, the sums in order, VSQRT (all float)."""
    x, y, z = f32(v[0]), f32(v[1]), f32(v[2])
    return f32(np.sqrt(np.float64(max((x * x + y * y) + z * z, f32(0.0)))))


def set_steer_target(st: RiderState, turn: float) -> None:
    """A's steer input rule (0x126948), single precision like the port."""
    t = f32(turn)
    if st.boost_strength == 0.0:
        t = min(max(t, -STEER_CLAMP), STEER_CLAMP)
    ratio = _vu_length(st.vel) / STEER_KNEE[st.board_class]
    if ratio < f32(1.0):
        t = t * ratio
    rate = min(max(abs(fpu_sub(t, st.steer.cur)) * STEER_RATE_K, STEER_RATE_MIN), STEER_RATE_MAX)
    if st.surface_id in ICE_SURFACES:
        rate = rate * DEEP_SNOW_STEER_RATE
    st.steer.target, st.steer.rate = float(t), float(rate)


def set_crouch_target(st: RiderState, value: float) -> None:
    if value > 0.0 and st.brake.cur != 0.0:
        value = 0.0
    st.crouch.target = value
    d = abs(fpu_sub(value, st.crouch.cur))
    st.crouch.rate = float((f32(0.1) if d < f32(0.1) else d) * CROUCH_RATE_K)


def set_brake_target(st: RiderState, value: float) -> None:
    if value != 0.0 and st.crouch.cur > 0.0:
        value = 0.0
    d = abs(fpu_sub(value, st.brake.cur))
    d = f32(0.1) if d < f32(0.1) else d
    if value == 0.0:
        d = fpu_sub(1.1, d)
    st.brake.target, st.brake.rate = float(f32(value)), float(d * BRAKE_RATE_K)


def control_step(st: RiderState, inp: InputSample, P: Profile) -> None:
    if st.motion != "ground":
        return
    if st.control in ("cruise", "change_fakie", "fakie_cruise"):
        set_steer_target(st, inp.turn)
        if inp.crouch > 0.0 and st.brake.cur != 0.0:
            st.brake.target, st.brake.rate = 0.0, float(BRAKE_RATE_K)
        elif inp.brake != 0.0 and st.crouch.cur > 0.0:
            st.crouch.target, st.crouch.rate = 0.0, float(CROUCH_RATE_K)
        else:
            set_crouch_target(st, inp.crouch)
            set_brake_target(st, inp.brake)
        st.anim_left = max(0.0, st.anim_left - DT)
        if st.control == "change_fakie":
            if st.anim_left > 0.0:   # ChangeFakie waits for the R_SWITCH animation
                return
            st.control = "cruise"    # R_SWITCH's follow-on is the riding loop (all 8 live segments)
            st.brake = Filter()      # ChangeFakie Exit zeroes the brake filter
        # skating push: a one-shot 0x221 animation; once started it plays to its end
        if st.push_anim and st.anim_left <= 0.0:
            st.push_anim = False
        if (not st.push_anim and inp.crouch > 0.0 and abs(st.steer.cur) < 0.2 and st.brake.cur == 0.0
                and norm(st.vel) < 833.33):
            st.push_anim = True
            st.anim_left = anim_seconds(0x221, st.board_class)
        st.jump_latch = inp.jump_held     # latch arms on hold, clears on release
        if st.jump_latch and st.brake.cur == 0.0:
            st.control = "prewind"
            st.steer.target, st.steer.rate = 0.0, float(PREWIND_RELEASE_RATE)
            st.brake.target, st.brake.rate = 0.0, float(PREWIND_RELEASE_RATE)
    elif st.control == "prewind":
        if inp.jump_held:
            set_crouch_target(st, 1.0)   # 0x126C00(1)
            if np.dot(st.vel, st.fwd) > 0.0:
                set_steer_target(st, inp.turn)
        else:
            st.launch_charge = st.crouch.cur
            st.crouch.target, st.crouch.rate = 0.0, float(CROUCH_UNWIND_RATE)  # 0x41555555
            st.control = "jump"
            st.brake.target = st.brake.cur = 0.0
    elif st.control == "jump":
        if st.crouch.cur == 0.0:
            st.last_launch = launch(st, P, JUMP_MIN)
            st.motion, st.control = "air", "spin"
        else:
            set_steer_target(st, inp.turn)
    elif st.control == "land_normal":
        if np.dot(st.vel, st.fwd) < 0.0:
            set_steer_target(st, -inp.turn)
        else:
            set_steer_target(st, inp.turn)


def step(st: RiderState, inp: InputSample, S: SurfaceParams, P: Profile, probe: ProbeFn) -> RiderState:
    """One 60 Hz frame for a grounded rider. Returns a new state (input is not modified)."""
    st = st.copy()
    for flt in (st.brake, st.crouch, st.steer):
        flt.tick(DT)
    b = fpu_sub(st.burst_timer, 0.016666668)   # sub.s, then <= 0 -> 0
    st.burst_timer = 0.0 if b <= 0.0 else float(b)
    st.boost_strength = 1.0 if inp.boosting else 0.0      # A's tiers simplified (full meter)
    if st.motion == "ground":
        ground_motion(st, S, P, probe)
        apply_speed_cap(st)
    control_step(st, inp, P)
    st.lean_deg = -60.0 * st.steer.cur
    return st


def rail_attach(st: RiderState, rail_point: np.ndarray, rail_dir: np.ndarray,
                box_filter: float = 1.0) -> bool:
    """Attach test and velocity snap. The board frame is approximated by (fwd, up, fwd x up)."""
    speed = norm(st.vel)
    if speed < 1e-6:
        return False
    vh = st.vel / speed
    t = rail_dir if np.dot(rail_dir, vh) >= 0.0 else -rail_dir
    across = np.cross(st.fwd, st.up)
    d = rail_point - st.pos - 30.0 * st.up
    if np.dot(d, vh) < -0.2:
        return False
    r = box_filter
    if not (abs(np.dot(d, st.fwd)) < 0.9 * (150 * r + 80 * (1 - r))
            and abs(np.dot(d, st.up)) < 0.9 * 50
            and abs(np.dot(d, across)) < 0.9 * (50 * r + 30 * (1 - r))):
        return False
    st.vel = min(speed, max(float(np.dot(st.vel, t)), 555.56)) * t
    st.vel[2] *= 0.1
    st.motion, st.control = "rail", "rail_slide"
    return True


def rail_step(st: RiderState, rail_point: np.ndarray, rail_dir: np.ndarray, rail_time: float,
              dt: float = DT) -> float:
    """One on-rail frame. rail_point is the nearest rail point. Returns lateral offset."""
    t = rail_dir if np.dot(rail_dir, st.vel) >= 0.0 else -rail_dir
    speed = norm(st.vel)
    k = 0.5 if speed < 555.56 else 0.0009 * speed
    rel = rail_point - st.pos
    side = np.cross(st.up, st.fwd)
    lat = float(np.dot(side, rel))
    st.pos = st.pos + max(-8.3333, min(8.3333, float(np.dot(rel, st.up)))) * st.up
    blend = 30.0 * dt
    st.vel = blend * float(np.dot(st.vel, t)) * t + (1.0 - blend) * st.vel
    a = float(np.dot(np.array([0.0, 0.0, -980.0]), t)) * t
    if st.boost_strength > 0.0:
        a = a + st.boost_strength * math.copysign(2450.82, float(np.dot(st.vel, t))) * t
    st.vel = st.vel + a * dt
    st.pos = st.pos + st.vel * dt
    k2 = min(max(k, 0.5), 2.0)
    steer_side = st.steer.cur if st.up[2] < 0.0 else -st.steer.cur
    c = -5.0 if (rail_time < 0.6 or abs(t[2]) > 0.92) else 1.81535
    if abs(lat) < 2.5 and c > -1.0:
        c = -1.0
    m = min(max(1.0 / (k * k), 1.0), 4.0)
    lat_c = max(-20.0, min(20.0, lat)) if c >= 0.0 else lat
    lat_speed = steer_side * k2 * 145.077 - lat_c * c * m
    if st.up[2] < 0.0 and abs(lat) < 20.0:
        lat_speed = max(lat_speed, -290.15) if lat > 0.0 else min(lat_speed, 290.15)
    st.pos = st.pos + lat_speed * dt * side
    return lat


def plane_probe(slope_deg: float, surface_id: int = 1) -> ProbeFn:
    """Plane through the origin, falling toward +Y. Segment o-100n .. o+200n."""
    a = math.radians(slope_deg)
    n0 = np.array([0.0, math.sin(a), math.cos(a)])

    def probe(origin: np.ndarray, old_normal: np.ndarray) -> ProbeHit:
        dist = float(np.dot(origin, n0))           # signed height above the plane
        denom = float(np.dot(old_normal, n0))
        if abs(denom) < 1e-6:
            return ProbeHit(False, origin, n0, surface_id)
        t = -dist / denom                           # along old normal from origin to plane
        if not (-100.0 <= t <= 200.0):
            return ProbeHit(False, origin, n0, surface_id)
        return ProbeHit(True, origin + t * old_normal, n0.copy(), surface_id)
    return probe


def spawn_on_plane(slope_deg: float, speed: float, S: SurfaceParams) -> RiderState:
    """FRiderSim::Spawn on the analytic plane + RestInSnow: identity body turned by -pi/2 then the heading yaw about Z
    (RotateBodyEE), tilted onto the plane normal (c = n x up, -asin(min(|c|, 0.99999)) about c / |c|), resting where the
    normal support balances gravity; the probe fills the contact frame."""
    a = math.radians(slope_deg)
    n0 = np.array([0.0, math.sin(a), math.cos(a)])
    fwd = np.array([0.0, math.cos(a), -math.sin(a)])
    q = (f32(0), f32(0), f32(0), f32(1))
    zaxis = v3([0.0, 0.0, 1.0])
    q, _s, bf, bu = rotate_body_ee(q, zaxis, f32(-1.5707964))
    q, _s, bf, bu = rotate_body_ee(q, zaxis, atan2_ee(f32(1.0), f32(0.0)))
    nf = v3(n0)
    c = vcross(nf, bu)
    ln = vlength(c)
    res = (q, _s, bf, bu)
    if ln > f32(1e-5):
        res = rotate_body_ee(q, c * (f32(1.0) / ln), -asin_ee(min(ln, f32(0.99999))))
    rest = -S.spring_depth * math.cos(a)
    st = RiderState(pos=rest * n0, vel=speed * fwd, fwd=fwd.copy(), up=n0.copy(), normal=n0.copy(),
                    slope_fwd=fwd.copy(), slope_left=np.cross(n0, fwd), fall_line=fwd.copy(),
                    height=rest, path_heading=math.atan2(fwd[1], fwd[0]))
    _set_basis(st, res)
    run_probe(st, plane_probe(slope_deg), Profile())
    return st


def demo() -> None:
    S, P = SurfaceParams(), Profile()
    slope = 20.0
    probe = plane_probe(slope)

    # 1) straight tucked run from rest
    st = spawn_on_plane(slope, 0.0, S)
    tuck = InputSample(crouch=1.0)
    top, t95 = 0.0, None
    for i in range(60 * 60):
        st = step(st, tuck, S, P, probe)
        sp = norm(st.vel)
        top = max(top, sp)
        if t95 is None and sp >= 0.95 * 2788.84:
            t95 = (i + 1) * DT
    print(f"[run]   slope {slope:.0f} deg, tucked, 60 s: top speed {top:.1f} cm/s "
          f"({top / KMH:.1f} km/h), motion={st.motion}, 95% of cap at "
          f"{'n/a' if t95 is None else f'{t95:.2f} s'}")

    # 2) steady carve on flat snow at a fixed stick
    flat = plane_probe(0.0)
    for stick in (0.5, 1.0):
        st = spawn_on_plane(0.0, 1500.0, SurfaceParams(drag_lin=0.0, drag_quad=0.0, drag_cubic=0.0))
        Sc = SurfaceParams(drag_lin=0.0, drag_quad=0.0, drag_cubic=0.0, cruise_kmh=0.0)
        inp = InputSample(turn=stick, crouch=1.0)
        heads = []
        for i in range(150):
            st = step(st, inp, Sc, P, flat)
            heads.append((math.atan2(st.vel[1], st.vel[0]), norm(st.vel)))
        (h0, _), (h1, v1) = heads[-31], heads[-1]
        omega = wrap_pi(h1 - h0) / (30 * DT)
        radius = v1 / abs(omega) if abs(omega) > 1e-9 else float("inf")
        print(f"[carve] flat, stick {stick:.1f} (steer {st.steer.cur:.3f}), no drag: speed {v1:.0f} cm/s, "
              f"yaw rate {math.degrees(omega):.1f} deg/s, radius {radius / 100:.1f} m")

    # 3) jump launch velocity for charge 0 / 0.5 / 1 (deliberate jump, J_min applies)
    v0 = 2500.0
    for stat in (0.5, 1.0):
        Pj = replace(P, stat_jump=stat)
        for charge in (0.0, 0.5, 1.0):
            st = spawn_on_plane(slope, v0, S)
            st.launch_charge, st.crouch.cur = charge, 0.0
            vin = st.vel.copy()
            J = launch(st, Pj, JUMP_MIN)
            dv = st.vel - vin
            print(f"[jump]  v0 {v0:.0f} cm/s, jump stat {stat:.1f}, charge {charge:.1f}: J {J:.1f} cm/s, "
                  f"launch v ({st.vel[0]:.0f}, {st.vel[1]:.0f}, {st.vel[2]:.0f}), "
                  f"dv along normal {float(np.dot(dv, st.normal)):.0f} cm/s")

    # 3b) full control path: hold jump 0.5 s, release, unwind, launch
    st = spawn_on_plane(slope, 1500.0, S)
    for i in range(30):
        st = step(st, InputSample(jump_held=True), S, P, probe)
    charge = st.crouch.cur
    frames = 0
    while st.motion == "ground" and frames < 30:
        st = step(st, InputSample(), S, P, probe)
        frames += 1
    print(f"[jump]  prewind 0.5 s at 1500 cm/s: crouch charge {charge:.3f}, airborne after {frames} "
          f"frames, J {st.last_launch:.1f} cm/s, control={st.control}")

    # 4) landing classification examples
    for args in ((0.1, 0.1, -900.0), (0.6, 0.4, -1500.0), (0.2, 0.2, -3000.0), (0.2, -1.0, -900.0)):
        r = classify_landing(*args, P)
        print(f"[land]  yaw {math.degrees(args[0]):.0f} deg, pitch {math.degrees(args[1]):.0f} deg, "
              f"vn {args[2]:.0f}: {r['kind']}, speed x{r['speed_factor']:.3f}, absorb {r['absorb']:.2f}")


if __name__ == "__main__":
    demo()
