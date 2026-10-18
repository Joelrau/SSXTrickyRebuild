"""Rail reference model: world-side rail query, attach test, RailSlide motion step.

The query, the catch, the RailSlide step and the
orientation run in the original's single precision (numpy float32, fpu.fpu_add for the EE FPU adds, ee_math for the EE
math library), mirroring Sim/Ground/RailCore.cpp and Sim/PatchWorldRails.cpp operation for operation. Units cm, s, rad; Z up. One call of `slide_step`
is one game frame of the RailSlide motion (motion 3); `attach_test` is the per-frame rail catch
check run by the Ground and Air motions.

Data comes from the track files through scripts/world_data.py (`build_world`), but the model only
needs plain arrays:
  * segments: cubic power-basis rows (t^3, t^2, t, 1) x xyz, an AABB and the owning spline;
  * splines: enabled flag (bit 0 of the active flag half) and surface id;
  * the .ltg grid: origin, cell size, nx, ny; per cell an AABB and 16 sub-cells, each with an
    AABB, an "empty" flag and the list of segment indices that touch it.
"""
from __future__ import annotations

import math
from dataclasses import dataclass, field
from typing import List, Optional, Tuple

import numpy as np

import ee_math as em
from fpu import f32, fpu_add, fpu_sub

QUERY_MARGIN = 300.0           # cm, AABB growth for cell, sub-cell and segment culling
COARSE_STEPS = 4               # chords t = 0, .25, .5, .75, 1
GOLDEN_LO = 0.381966
GOLDEN_HI = 0.618034
GOLDEN_MAX_ITER = 24
GOLDEN_TOL = 0.0005
# bracket [lo, hi] for the golden search, by coarse result (0 = the t=0 point, i = chord i)
BRACKET_LO = (0.0, 0.0, 0.0, 0.25, 0.25)
BRACKET_HI = (0.75, 0.75, 1.0, 1.0, 1.0)
DEFAULT_RAIL_SURFACE = 13

BOX_FULL = (150.0, 50.0, 50.0)     # half extents along board long / up / across, filter = 1
BOX_SMALL = (80.0, 50.0, 30.0)     # filter = 0
ATTACH_MARGIN = 0.9
CATCH_UP_OFFSET = 30.0             # cm, rail point is compared 30 cm below the board origin
ATTACH_BEHIND = -0.2               # cm, (offset point . v_hat) must be >= this
RAIL_MIN_SPEED = 555.55554         # cm/s (20 km/h)
ATTACH_VZ_KEEP = 0.1
GRIND_BACK = math.radians(150.0)
GRIND_SIDE = math.radians(30.0)

RAIL_GRAVITY = np.array([0.0, 0.0, -980.0])
RAIL_BOOST = 2450.8232             # cm/s^2 along the rail per unit boost strength
TANGENT_PULL = 30.0                # 1/s: fraction 30 dt of off-rail velocity removed per frame
HEIGHT_SNAP = 8.333333             # cm per frame
FALL_PUSH = 277.77777              # cm/s (0x438AE38E)
SETTLE_TIME = 0.6                  # s
STEEP_TZ = 0.92
BAL_K_SLOW = 0.5
BAL_K_PER_SPEED = 0.00090000004   # 0x3A6BEDFB
BAL_STEER = 145.07674              # cm/s per unit steer
BAL_CENTRE = -5.0
BAL_UNSTABLE = 1.8153523
BAL_NEAR = -1.0
BAL_NEAR_DIST = 2.5000005
BAL_CLAMP = 20.0
BAL_UPSIDE_MIN = 290.15347
ROLL_SLIDE = (math.radians(40.0), 0.0125)      # grind type 3/4 (board sideways)
ROLL_GRIND = (math.radians(20.0), 1.0 / 30.0)  # grind type 1/2
ROLL_SLEW = 5.0                    # rad per frame limit (effectively none)
ORIENT_BLEND = 0.083333336       # 0x3DAAAAAB

FILTER_RATE = 1.6666666            # rail box filter rate, per s (approach filter)
FILTER_EXIT_VALUE = -0.25
# (cur, rate, target) of the rail box filter +0x284 after the reset (cBoarder vfunc 9, 0x116AB8). The spawn routine
# 0x11CAE8 zeroes the filters +0x1F0..+0x280 only, so a respawn keeps the current value; every live rider reads
# (1, 0, 1) until its first rail.
RESET_FILTER = (1.0, 0.0, 1.0)


def catch_limits(r: float):
    """Catch window of the catch routine 0x125CB8 for box filter value r, in the board frame (cm).
    Returns (along, up_lo, up_hi, across, behind): catch iff, with c = point - O - 30 R1,
      |R0.c| < along, up_lo < R1.(point - O) < up_hi, |R2.c| < across and c.v_hat >= behind.
    r = 1: along 135, rail from 15 cm below to 75 cm above the board origin, across 45.
    r = 0: along 72, across 27 (height unchanged). r = -0.25 (just after a rail exit): along 63, across 22.5."""
    hx, hy, hz = _box_halves(r)
    return (hx * ATTACH_MARGIN, CATCH_UP_OFFSET - hy * ATTACH_MARGIN, CATCH_UP_OFFSET + hy * ATTACH_MARGIN,
            hz * ATTACH_MARGIN, ATTACH_BEHIND)

MOTION_AIR, MOTION_RAIL = 1, 3
CONTROL_RAIL, CONTROL_SPIN = 15, 16


@dataclass
class SubCell:
    bmin: np.ndarray
    bmax: np.ndarray
    empty: bool
    segs: List[int]


@dataclass
class Cell:
    bmin: np.ndarray
    bmax: np.ndarray
    subs: List[SubCell]


@dataclass
class RailWorld:
    coef: np.ndarray            # (nseg, 4, 3)
    seg_min: np.ndarray         # (nseg, 3)
    seg_max: np.ndarray
    seg_spline: np.ndarray      # (nseg,)
    spline_enabled: np.ndarray  # (nspl,) bool
    spline_surface: np.ndarray  # (nspl,) int
    origin: Tuple[float, float]
    cell: float
    nx: int
    ny: int
    cells: List[Optional[Cell]]


def _inside(p, bmin, bmax, m):
    """The 300 cm grown AABB test of the query: add.s / sub.s of the point and the margin (float32)."""
    m = f32(m)
    return (f32(bmin[0]) <= fpu_add(p[0], m) and fpu_sub(p[0], m) <= f32(bmax[0]) and f32(bmin[1]) <= fpu_add(p[1], m) and
            fpu_sub(p[1], m) <= f32(bmax[1]) and f32(bmin[2]) <= fpu_add(p[2], m) and fpu_sub(p[2], m) <= f32(bmax[2]))


def _rows(coef):
    return np.asarray(coef, dtype=np.float32)[:, :3]


def eval_seg(coef, t):
    """P(t) as the query evaluates it (float32): t^2 = t t, t^3 = t^2 t (mul.s), then the VU0 ACC chain
    ((A t^3 + B t^2) + C t) + D, each product and sum rounded."""
    c = _rows(coef)
    t = f32(t)
    t2 = t * t
    t3 = t2 * t
    return ((c[0] * t3 + c[1] * t2) + c[2] * t) + c[3]


def eval_tangent(coef, t):
    """dP/dt as the query builds it: (t x 3) t and t + t (mul.s, add.s) through the ACC chain (the D x 0 term adds 0)."""
    c = _rows(coef)
    t = f32(t)
    return (c[0] * ((t * f32(3.0)) * t) + c[1] * fpu_add(t, t)) + c[2]


def _dist(a, b):
    return em.vlength((a - b).astype(np.float32))


def _closest_on_segment(coef, p):
    """Coarse chords then golden-section search, in the original's precision: returns
    (distance, t, point) of the last evaluated parameter. Before the loop the kept point is P(b) with parameter x2."""
    p = np.asarray(p, dtype=np.float32)
    prev = eval_seg(coef, 0.0)
    best_d = _dist(p, prev)
    best_i = 0
    for i in range(1, COARSE_STEPS + 1):
        cur = eval_seg(coef, f32(i) * f32(0.25))
        chord = (cur - prev).astype(np.float32)
        with np.errstate(all="ignore"):
            u = f32(em.vdot((p - prev).astype(np.float32), chord) / em.vdot(chord, chord))
        if u < 0:
            u = f32(0.0)
        if f32(1.0) < u:
            u = f32(1.0)
        d = _dist(p, prev + u * chord)
        if not (best_d <= d):
            best_d, best_i = d, i
        prev = cur
    a, b = f32(BRACKET_LO[best_i]), f32(BRACKET_HI[best_i])
    lo, hi = f32(GOLDEN_LO), f32(GOLDEN_HI)
    x1 = fpu_add(a, fpu_sub(b, a) * lo)
    x2 = fpu_add(a, fpu_sub(b, a) * hi)
    f1 = _dist(eval_seg(coef, x1), p)
    f2 = _dist(eval_seg(coef, x2), p)
    last_t, last_p, last_d = x2, eval_seg(coef, b), f2
    it = 0
    while it < GOLDEN_MAX_ITER and f32(GOLDEN_TOL) < abs(fpu_sub(b, a)):
        if f2 < f1:
            xn = fpu_add(x2 * hi, b * lo)
            last_p = eval_seg(coef, xn)
            last_d = _dist(last_p, p)
            a, x1, f1, x2, f2 = x1, x2, f2, xn, last_d
        else:
            xn = fpu_add(x1 * hi, a * lo)
            last_p = eval_seg(coef, xn)
            last_d = _dist(last_p, p)
            b, x2, f2, x1, f1 = x2, x1, f1, xn, last_d
        last_t = xn
        it += 1
    return last_d, last_t, last_p


@dataclass
class RailHit:
    point: np.ndarray
    tangent: np.ndarray         # unit, NOT yet oriented along the velocity
    surface: int
    segment: int
    t: float
    distance: float


def query_rail(world: RailWorld, p) -> Optional[RailHit]:
    """Nearest point on any enabled rail segment near p (3x3 grid cells around p), float32 as 0x259330."""
    p = np.asarray(p, dtype=np.float32)
    ix = int((p[0] - world.origin[0] + world.cell) / world.cell)   # truncation toward zero
    iy = int((p[1] - world.origin[1] + world.cell) / world.cell)
    x_lo, x_hi = max(ix - 2, 0), min(max(ix, 0), world.nx - 1)
    y_lo, y_hi = max(iy - 2, 0), min(max(iy, 0), world.ny - 1)
    best: Optional[RailHit] = None
    for cx in range(x_lo, x_hi + 1):
        for cy in range(y_lo, y_hi + 1):
            c = world.cells[cy * world.nx + cx]
            if c is None or not _inside(p, c.bmin, c.bmax, QUERY_MARGIN):
                continue
            for sc in c.subs:
                if sc.empty or not _inside(p, sc.bmin, sc.bmax, QUERY_MARGIN):
                    continue
                for s in sc.segs:
                    if not world.spline_enabled[world.seg_spline[s]]:
                        continue
                    if not _inside(p, world.seg_min[s], world.seg_max[s], QUERY_MARGIN):
                        continue
                    d, t, pt = _closest_on_segment(world.coef[s], p)
                    if best is None or d < best.distance:
                        tan = em.vnormalize(eval_tangent(world.coef[s], t))
                        best = RailHit(pt, tan, int(world.spline_surface[world.seg_spline[s]]), int(s), t, d)
    return best


@dataclass
class RailRider:
    pos: np.ndarray                 # body position (moved by the rail motion)
    vel: np.ndarray
    board_rows: np.ndarray          # (3, 3): long axis, up, across (board matrix rows)
    board_origin: np.ndarray        # board matrix origin (query point)
    up: np.ndarray                  # body up (basis_up)
    side: np.ndarray                # body side (basis_side = fwd x up)
    fwd: Optional[np.ndarray] = None  # body forward (+0x180); when set, slide_step also runs the
                                      # orientation step and updates fwd/side/up
    rail_up: Optional[np.ndarray] = None  # cBoarder+0x2A0 (unrolled rail up)
    steer: float = 0.0
    boost: float = 0.0              # boost strength (0 when not boosting)
    time_scale: float = 1.0
    box_filter: float = 1.0         # rail box filter value (approach filter)
    box_target: float = 1.0
    rail_time: float = 0.0
    roll: float = 0.0
    grind_type: int = 0
    surface: int = 0
    rail_point: Optional[np.ndarray] = None
    motion: int = 2
    control: int = 0
    events: list = field(default_factory=list)
    q: Optional[tuple] = None       # body quaternion (x, y, z, w) +0x170; from the basis when None


def _f3(v):
    return np.asarray(v, dtype=np.float32)


def _box_halves(r):
    """(r 150 + (1 - r) 80, r 50 + (1 - r) 50, r 50 + (1 - r) 30): 1 - r by sub.s, the sums by add.s (float32)."""
    r = f32(r)
    om = fpu_sub(f32(1.0), r)
    return tuple(fpu_add(r * f32(full), om * f32(small)) for full, small in zip(BOX_FULL, BOX_SMALL))


def attach_test(rd: RailRider, world: RailWorld, trick_blocks: bool = False) -> bool:
    """Rail catch check (Ground and Air motions), 0x125CB8 in float32. trick_blocks: channel-0 animation
    is a grab, tweak or uber that has not reached its landable flag.

    No button, no speed or approach-angle gate besides
    `c . v_hat >= -0.2`; c includes the -30 R1 offset, so a board moving out of its own plane upward
    (v . R1 > 0) with the rail below its origin can fail it. Window: catch_limits(). No AI / player
    difference and no cooldown other than the box filter (exit value -0.25, back to 1 after 0.75 s)."""
    v = _f3(rd.vel)
    v2 = em.vdot(v, v)
    if v2 == 0:
        return False
    v_hat = em.rsqrt32(v2) * v
    hit = query_rail(world, _f3(rd.board_origin))
    if hit is None:
        return False
    t = hit.tangent if not (em.vdot(v_hat, hit.tangent) < 0) else f32(-1.0) * hit.tangent
    rows = _f3(rd.board_rows)
    c = (hit.point - _f3(rd.board_origin)) + f32(-CATCH_UP_OFFSET) * rows[1]
    if em.vdot(c, v_hat) < f32(ATTACH_BEHIND) or trick_blocks:
        return False
    hx, hy, hz = _box_halves(rd.box_filter)
    m = f32(ATTACH_MARGIN)
    if not (abs(em.vdot(rows[0], c)) < hx * m and abs(em.vdot(rows[1], c)) < hy * m and abs(em.vdot(rows[2], c)) < hz * m):
        return False
    vt = em.vdot(v, t)
    floor = vt if f32(RAIL_MIN_SPEED) < vt else f32(RAIL_MIN_SPEED)
    ln = em.sqrt32(v2)
    new_speed = floor if floor < ln else ln
    nv = new_speed * t
    nv[2] = nv[2] * f32(ATTACH_VZ_KEEP)
    rd.vel = nv
    a = em.atan2_ee(em.vdot(rows[2], t), em.vdot(rows[0], t))
    if not (abs(a) <= f32(2.6179938)):
        rd.grind_type = 2
    elif a < f32(-0.5235988):
        rd.grind_type = 4
    elif f32(0.5235988) < a:
        rd.grind_type = 3
    else:
        rd.grind_type = 1
    rd.surface = hit.surface
    rd.events.append(("trick_rail_start", rd.grind_type))
    _enter(rd)
    return True


def _enter(rd: RailRider):
    rd.motion, rd.control = MOTION_RAIL, CONTROL_RAIL
    rd.roll = f32(0.0)
    rd.rail_time = f32(0.0)
    rd.box_target = 0.0


def _exit(rd: RailRider):
    rd.steer = 0.0
    rd.events.append(("trick_rail_end", rd.grind_type))
    rd.motion, rd.control = MOTION_AIR, CONTROL_SPIN
    rd.box_filter = FILTER_EXIT_VALUE
    rd.box_target = 1.0


def filter_step(rd: RailRider, dt_frame: float = 1.0 / 60.0):
    """The approach filter for the rail box (runs before the motion every frame), FApproachFilter::Tick in float32:
    s = rate x dt; target + s < cur -> cur - s, cur < target - s -> cur + s, else the target (FPU adds)."""
    c, t, s = f32(rd.box_filter), f32(rd.box_target), f32(FILTER_RATE) * f32(dt_frame)
    if fpu_add(t, s) < c:
        rd.box_filter = fpu_sub(c, s)
    elif c < fpu_sub(t, s):
        rd.box_filter = fpu_add(c, s)
    else:
        rd.box_filter = t


def slide_step(rd: RailRider, world: RailWorld) -> bool:
    """One RailSlide frame (0x10B0D0 in float32). Returns True while still on the rail."""
    dt = f32(rd.time_scale) * f32(0.016666668)
    v0 = _f3(rd.vel)
    v2 = em.vdot(v0, v0)
    v_hat = em.rsqrt32(v2) * v0 if v2 > 0 else np.zeros(3, dtype=np.float32)
    origin = _f3(rd.board_origin)
    hit = query_rail(world, origin)
    if hit is None:
        _exit(rd)
        return False
    rd.surface = hit.surface
    rd.rail_point = hit.point.copy()
    t = hit.tangent if not (em.vdot(v_hat, hit.tangent) < 0) else f32(-1.0) * hit.tangent
    speed = em.sqrt32(v2)
    k = f32(BAL_K_SLOW) if speed < f32(RAIL_MIN_SPEED) else speed * f32(BAL_K_PER_SPEED)
    kk = k * k
    rows = _f3(rd.board_rows)
    d = (hit.point - origin).astype(np.float32)
    c = d + f32(-CATCH_UP_OFFSET) * rows[1]
    a0, a1, a2 = em.vdot(rows[0], c), em.vdot(rows[1], c), em.vdot(rows[2], c)
    if f32(rd.rail_time) < f32(SETTLE_TIME):
        rd.box_filter = f32(1.0)
    hx, hy, hz = _box_halves(rd.box_filter)
    if not (abs(a0) < hx) or not (abs(a1) < hy) or not (abs(a2) < hz):
        # fell off: push away from the rail point (perpendicular to the rail and to v)
        u = em.vnormalize(d)
        if f32(0.5) < abs(em.vdot(u, t)):
            u = t.copy()
        if em.vdot(v0, u) < 0:
            vn = em.vnormalize(v0)
            u = em.vnormalize((u - em.vdot(u, vn) * vn).astype(np.float32))
        rd.vel = (v0 - f32(FALL_PUSH) * u).astype(np.float32)
        _exit(rd)
        return False

    side, up = _f3(rd.side), _f3(rd.up)
    lat = em.vdot(side, d)
    rd.rail_time = fpu_add(f32(rd.rail_time), dt)
    hs = em.vdot(d, up)
    hs = f32(-HEIGHT_SNAP) if hs < f32(-HEIGHT_SNAP) else (f32(HEIGHT_SNAP) if f32(HEIGHT_SNAP) < hs else hs)
    pos = _f3(rd.pos) + hs * up
    vt = em.vdot(v0, t)
    pull = dt * f32(TANGENT_PULL)
    vel = pull * (vt * t) + fpu_sub(f32(1.0), pull) * v0
    acc = em.vdot(_f3(RAIL_GRAVITY), t) * t
    if f32(0.0) < f32(rd.boost):
        acc = acc + (f32(rd.boost) * (f32(RAIL_BOOST) if f32(0.0) < vt else f32(-RAIL_BOOST))) * t
    vel = vel + dt * acc
    pos = pos + dt * vel

    # balance (lateral drift across the rail): mul.s chains, the final sub.s through the FPU adder
    k2 = f32(0.5) if k < f32(0.5) else (f32(2.0) if f32(2.0) < k else k)
    upside_down = up[2] < 0
    steer = f32(rd.steer)
    steer_side = steer if upside_down else -steer
    centring = f32(BAL_CENTRE) if (f32(STEEP_TZ) < abs(t[2]) or rd.rail_time < f32(SETTLE_TIME)) else f32(BAL_UNSTABLE)
    if abs(lat) < f32(BAL_NEAR_DIST) and f32(BAL_NEAR) < centring:
        centring = f32(BAL_NEAR)
    gain = f32(1.0) / kk
    gain = f32(1.0) if gain < f32(1.0) else (f32(4.0) if f32(4.0) < gain else gain)
    lat_c = lat
    if f32(0.0) <= centring:
        lat_c = f32(-BAL_CLAMP) if lat < f32(-BAL_CLAMP) else (f32(BAL_CLAMP) if f32(BAL_CLAMP) < lat else lat)
    lateral = fpu_sub(steer_side * (k2 * f32(BAL_STEER)), lat_c * (centring * gain))
    if upside_down and abs(lat) < f32(BAL_CLAMP):
        if f32(0.0) < lat:
            lateral = f32(-BAL_UPSIDE_MIN) if f32(-BAL_UPSIDE_MIN) < lateral else lateral
        else:
            lateral = f32(BAL_UPSIDE_MIN) if lateral < f32(BAL_UPSIDE_MIN) else lateral
    pos = pos + (lateral * dt) * side
    rd.pos, rd.vel = pos.astype(np.float32), vel.astype(np.float32)

    # visual roll (orientation itself is slerped toward the rail frame by ORIENT_BLEND per frame)
    amp, per = ROLL_SLIDE if rd.grind_type in (3, 4) else ROLL_GRIND
    roll = (lat * f32(amp)) * f32(per)
    if upside_down:
        roll = -roll
    prev = f32(rd.roll)
    if fpu_add(roll, f32(ROLL_SLEW)) < prev:
        roll = fpu_sub(prev, f32(ROLL_SLEW))
    elif prev < fpu_sub(roll, f32(ROLL_SLEW)):
        roll = fpu_add(prev, f32(ROLL_SLEW))
    rd.roll = roll
    if rd.fwd is not None:
        orient_step(rd, t)
    return True


def _qmul(a, b):
    """0x102E80 a (x) b on (w, x, y, z), the FPU sums in the code's order."""
    aw, ax, ay, az = a
    bw, bx, by, bz = b
    return (fpu_sub(fpu_sub(fpu_sub(aw * bw, ax * bx), ay * by), az * bz),
            fpu_sub(fpu_add(fpu_add(aw * bx, ax * bw), ay * bz), az * by),
            fpu_sub(fpu_add(fpu_add(aw * by, ay * bw), az * bx), ax * bz),
            fpu_sub(fpu_add(fpu_add(aw * bz, az * bw), ax * by), ay * bx))


def _qaxis(axis, angle):
    s, c = em.sincos_ee(f32(angle) * f32(0.5))
    return (c, s * axis[0], s * axis[1], s * axis[2])


def _qrot(q, e):
    w, x, y, z = q
    ex, ey, ez = (f32(v) for v in e)
    cx, cy, cz = fpu_sub(y * ez, z * ey), fpu_sub(z * ex, x * ez), fpu_sub(x * ey, y * ex)
    dx, dy, dz = fpu_sub(y * cz, z * cy), fpu_sub(z * cx, x * cz), fpu_sub(x * cy, y * cx)
    w2 = fpu_add(w, w)
    two = f32(2.0)
    return np.array([fpu_add(fpu_add(ex, w2 * cx), two * dx), fpu_add(fpu_add(ey, w2 * cy), two * dy),
                     fpu_add(fpu_add(ez, w2 * cz), two * dz)], dtype=np.float32)


def _qfromto(q, a, b):
    """0x2508B8(q, a, b): q = r (x) q, r taking a onto b (asin of min(|a x b|, 0.99999), pi - that when a . b < 0)."""
    c = em.vcross(_f3(a), _f3(b))
    s = em.vlength(c)
    if s == 0:
        return q
    c = (f32(1.0) / s) * c
    s = f32(0.99999) if f32(0.99999) < s else s
    ang = fpu_sub(f32(3.1415927), em.asin_ee(s)) if em.vdot(_f3(a), _f3(b)) < 0 else em.asin_ee(s)
    return _qmul(_qaxis(c, ang), q)


def game_slerp(a, b, f):
    """0x250A48 on (w, x, y, z): FPU dot ((x x' + y y') + z z') + w w', b negated when negative; angle pi/2 - asin(dot)
    and sine weights when 1 - dot > 0.001, else 1 - f and f."""
    aw, ax, ay, az = (f32(v) for v in a)
    bw, bx, by, bz = (f32(v) for v in b)
    f = f32(f)
    d = fpu_add(fpu_add(fpu_add(ax * bx, ay * by), az * bz), aw * bw)
    if d < 0:
        bw, bx, by, bz, d = -bw, -bx, -by, -bz, -d
    kb = f
    if f32(0.001) < fpu_sub(f32(1.0), d):
        ang = fpu_sub(f32(1.5707964), em.asin_ee(d))
        sa = em.sin_ee(ang)
        ka = em.sin_ee(fpu_sub(f32(1.0), f) * ang) / sa
        kb = em.sin_ee(f * ang) / sa
    else:
        ka = fpu_sub(f32(1.0), f)
    return (fpu_add(ka * aw, kb * bw), fpu_add(ka * ax, kb * bx), fpu_add(ka * ay, kb * by), fpu_add(ka * az, kb * bz))


def rail_target_up(t, body_up, roll):
    """(stored rail up cBoarder+0x2A0, rolled target up w) for the oriented tangent t, float32."""
    t, body_up = _f3(t), _f3(body_up)
    s, c = em.sincos_ee(f32(roll))
    if abs(t[2]) <= f32(STEEP_TZ):
        u = em.vnormalize((_f3([0.0, 0.0, 1.0]) - t[2] * t).astype(np.float32))
        if em.vdot(u, body_up) < 0:          # keep the rider's side of the rail (upside down stays)
            u = f32(-1.0) * u
    else:                                    # steep: built from the current body up
        sx = em.vnormalize(em.vcross(body_up, t))
        u0 = em.vcross(t, sx)
        u = c * u0 - s * sx
    final = c * u + s * em.vcross(u, t)
    return u.astype(np.float32), final.astype(np.float32)


def rail_frame(t, body_up, roll):
    """(stored rail up, target quaternion (w, x, y, z)): identity, 0x2508B8 z -> w, then the rotated y -> t."""
    stored, w = rail_target_up(t, body_up, roll)
    q = (f32(1.0), f32(0.0), f32(0.0), f32(0.0))
    q = _qfromto(q, [0.0, 0.0, 1.0], w)
    q = _qfromto(q, _qrot(q, (0.0, 1.0, 0.0)), _f3(t))
    return stored, q


def orient_step(rd, t):
    """Body quaternion slerped by 1/12 (0x3DAAAAAB) toward the rail frame (local y = t, local z = rolled up), stored as
    is, then 0x129008 (normalise, basis). Body columns: side (+0x190) = x, forward (+0x180) = y, up (+0x1A0) = z."""
    stored, q_tgt = rail_frame(t, rd.up, rd.roll)
    rd.rail_up = stored
    if rd.q is None:
        rd.q = em.quat_from_basis(rd.side, rd.fwd, rd.up)
    x, y, z, w = rd.q
    s = game_slerp((w, x, y, z), q_tgt, ORIENT_BLEND)
    q, rd.side, rd.fwd, rd.up = em.update_basis((s[1], s[2], s[3], s[0]))
    rd.q = q


def rotate_about_up(rd, angle):
    """the body-yaw helper: rotate the body by angle about the body up axis (positive = counter-clockwise  # 0x128E90
    seen from above, i.e. forward turns toward -side)."""
    c, s = math.cos(angle), math.sin(angle)
    f = rd.fwd
    rd.fwd = c * f + s * np.cross(rd.up, f)
    rd.side = np.cross(rd.fwd, rd.up)


HALF_PI = math.pi / 2
CLIP_CYCLE = {1: 0x254, 2: 0x255, 3: 0x256, 4: 0x257}      # cycle / start clip per grind type
# (direction, grind type) -> (transition clip, resulting grind type, anim yaw +0x46E0 default,
#  anim yaw for board class 2 with a goofy profile). Direction 0 = rail-spin axis < 0 (d-pad
#  Right): layer yaw +pi/2, rail spin +90. Direction 1 = axis > 0 (d-pad Left): -pi/2, -90.
#  Board class 2 with a goofy profile swaps the two directions.
RAIL_SWITCH = {
    (0, 2): (0x259, 3, -HALF_PI, HALF_PI),     # RSR_FAKIE2FS
    (0, 1): (0x25A, 4, HALF_PI, -HALF_PI),     # RSR_REG2BS
    (0, 3): (0x25C, 1, 0.0, -math.pi),         # RSR_FS2REG
    (0, 4): (0x25F, 2, -math.pi, 0.0),         # RSR_BS2FAKIE
    (1, 2): (0x25B, 4, HALF_PI, -HALF_PI),     # RSR_FAKIE2BS
    (1, 1): (0x258, 3, -HALF_PI, HALF_PI),     # RSR_REG2FS
    (1, 4): (0x25E, 1, 0.0, -math.pi),         # RSR_BS2REG
    (1, 3): (0x25D, 2, -math.pi, 0.0),         # RSR_FS2FAKIE
}
PENDING_OF = {4: 1, 3: 2, 1: 3, 2: 4}          # handler +8 value for a resulting grind type
GT_OF_PENDING = {v: k for k, v in PENDING_OF.items()}
SPIN_STEP_DEG = 90
CLIP_RATE = 0.5          # clip frames per game frame at play speed 1 (scripts/afl.py)


@dataclass
class RailAnimState:
    """The rider fields the rail control touches."""
    grind_type: int = 1
    stance: int = 0              # +0x1B4 (1 = goofy flag)
    profile_goofy: int = 0       # rider_profile+0x40
    board_class: int = 0         # +0x420 (2 = Alpine)
    mirror: int = 0              # +0x46D4
    anim_yaw: float = 0.0        # +0x46E0
    layer_yaw_add: float = 0.0   # sum of 0x15EEF8 additions to the playing layers' yaw
    clip: int = 0
    clip_frame: float = 0.0
    clip_frames: int = 15        # length of the playing transition (bx 15, fr 16, ex 11/12 frames)
    crouch: float = 0.0          # +0x208 current
    crouch_target: float = 0.0
    steer_target: float = 0.0
    body_yaw_add: float = 0.0    # sum of 0x128E90 body rotations (radians about up)


def _alpine_goofy(a: RailAnimState):
    return a.board_class == 2 and a.profile_goofy != 0


def _stance_flip(a: RailAnimState, layer_pi: bool):
    """the stance flip (+ the layer yaw += pi that most enter paths add)."""  # 0x107FC8
    a.stance ^= 1
    a.body_yaw_add += math.pi
    a.steer_target = -a.steer_target
    a.mirror = a.stance
    if layer_pi:
        a.layer_yaw_add += math.pi


def rail_control_enter(a: RailAnimState):
    """RailSlide control Enter (control enter): stance, body yaw, anim yaw and start clip per grind type."""  # 0x106DC8
    ag = _alpine_goofy(a)
    if ag and a.grind_type in (1, 2):
        a.grind_type = 3 - a.grind_type
    gt = a.grind_type
    if gt in (3, 4):
        sgn = 1.0 if gt == 4 else -1.0
        if a.board_class == 2:
            want = 1 if ag else 0
            if a.stance != want:
                _stance_flip(a, True)
            body = -sgn * HALF_PI if ag else sgn * HALF_PI
        else:
            if a.stance != 0:
                _stance_flip(a, False)
            body = sgn * HALF_PI
        a.body_yaw_add += body
        a.layer_yaw_add += sgn * HALF_PI
        a.anim_yaw = body
    elif gt == 2:
        if ag:
            if a.stance != 0:
                _stance_flip(a, False)
                a.mirror = 1
                a.layer_yaw_add += math.pi
                a.anim_yaw = 0.0
        elif a.stance == 0:
            _stance_flip(a, False)
            if a.board_class == 2:
                a.mirror = 0
            a.layer_yaw_add += math.pi
            a.anim_yaw = -math.pi
    else:
        if ag:
            if a.stance == 0:
                _stance_flip(a, True)
                a.anim_yaw = -math.pi
        elif a.stance != 0:
            _stance_flip(a, True)
            a.anim_yaw = 0.0
    a.clip, a.clip_frame = CLIP_CYCLE.get(gt, 0x254), 0.0


@dataclass
class RailControl:
    """Handler fields: +8 pending switch (0 none, 1..4), +0xC rail spin degrees, +0x10 jump latch."""
    pending: int = 0
    spin_deg: int = 0
    latch: int = 0
    events: list = field(default_factory=list)

    def enter(self, a: RailAnimState, stats: "RailStats"):
        self.pending, self.spin_deg, self.latch = 0, 0, 0
        stats.report_rail_spin(0)
        rail_control_enter(a)

    def _start_switch(self, a: RailAnimState, stats: "RailStats", axis_positive: bool):
        """the switch start."""  # 0x107730
        ag = _alpine_goofy(a)
        d = int(axis_positive) ^ int(ag)
        if (d, a.grind_type) not in RAIL_SWITCH:
            return
        clip, nxt, yaw, yaw_ag = RAIL_SWITCH[(d, a.grind_type)]
        a.layer_yaw_add += HALF_PI if d == 0 else -HALF_PI
        self.spin_deg += SPIN_STEP_DEG if d == 0 else -SPIN_STEP_DEG
        stats.report_rail_spin(self.spin_deg)
        if a.grind_type == 2:
            if ag:
                a.stance = 1
            else:
                a.stance, a.mirror = 0, 0
        a.anim_yaw = yaw_ag if ag else yaw
        a.clip, a.clip_frame = clip, 0.0
        self.pending = PENDING_OF[nxt]
        self.events.append(("rail_switch", clip, nxt))

    def complete(self, a: RailAnimState):
        """the switch completion: transition clip finished (or control exit): set the new grind type."""  # 0x107628
        if self.pending == 0:
            return
        nxt = GT_OF_PENDING[self.pending]
        if nxt == 2:
            if a.board_class == 2:
                a.stance = 0 if a.profile_goofy else 1
            else:
                a.mirror, a.stance = 1, 1
        a.grind_type = nxt
        a.clip, a.clip_frame = CLIP_CYCLE[nxt], 0.0
        self.pending = 0

    def step(self, a: RailAnimState, stats: "RailStats", *, turn=0.0, crouch=0.0, rail_spin=0.0,
             rail_spin_mode=False, jump_press=False, jump_held=False, reset=False):
        """One frame of control 15 (the rail control update). Analog values are the word's 6-bit fields / 31.  # 0x1073E0
        Returns 'prewind' / 'reset' / None. Boost (the boost handler, word bit 4) is the caller's."""  # 0x11D038
        self.latch = int(jump_press) if self.latch == 0 else int(jump_press or jump_held)
        out = None
        if self.pending == 0:
            if not rail_spin_mode and rail_spin == 0.0:
                a.steer_target = turn                    # 0x126AC8 (no clamp, no speed scale)
                a.crouch_target = crouch                 # 0x126C00
                if self.latch:
                    out = "prewind"
            else:
                d = rail_spin if rail_spin != 0.0 else turn
                a.crouch_target = 0.0
                if a.crouch == 0.0 and d != 0.0:
                    self._start_switch(a, stats, d > 0.0)
        else:
            a.clip_frame += CLIP_RATE                    # the animation system's clock
            if a.clip_frame >= a.clip_frames - 1:
                self.complete(a)
        if reset:
            out = "reset"
        return out

    def exit(self, a: RailAnimState):
        """Control Exit (control exit): boost off, then force-complete a pending switch."""  # 0x1075F0
        self.complete(a)


METER_SCALE = 0.6786545
SPIN_VALUE = 0.06995432
RAIL_HOLD = 0.0025
SWITCH_MULT = 1.3


def round180(d: int) -> int:
    """round180: nearest multiple of 180; an exact +-90 remainder goes toward zero."""  # 0x157198
    r = int(math.fmod(d, 180))
    if r < -90:
        return d - 180 - r
    if r <= 90:
        return d - r
    return d + 180 - r


def hold_speed_factor(speed):
    if speed >= 800.0:
        return 1.0
    if speed < 50.0:
        return 0.0
    return speed / (3200.0 - 3.0 * speed)


@dataclass
class RailStats:
    V: float = 0.0              # +0x24 trick value
    mult: float = 1.0           # +0x28 point multiplier
    seg_type: int = 0           # +0x20 (0 ground; 1..4 = grind type = rail kind)
    spin: int = 0               # +0x30 (rail spin degrees while on a rail)
    best_spin: int = 0          # +0x48
    rail_frames: int = -1       # +0x3C (-1 = not on a rail)
    switch: int = 0             # +0x10
    combo: int = 0              # +0xF4

    def catch(self, grind_type: int):
        """The tail of the landing routine landing routine when called by the catch (rail kind = grind  # 0x1569B0
        type): the rail frame count continues (+1), the segment type becomes the grind type."""
        frames = self.rail_frames
        self.reset()
        self.rail_frames = frames + 1
        self.seg_type = grind_type
        self.mult = 1.0

    def report_rail_spin(self, deg: int):
        """rail-spin report."""  # 0x155E00
        self.spin = deg
        r = round180(abs(deg))
        if self.best_spin < r:
            self.best_spin = r
            self.V += SPIN_VALUE

    def frame(self, speed):
        """the race-stats tick rail part, per frame."""  # 0x156E80
        if self.rail_frames >= 0:
            self.rail_frames += 1
            self.V += RAIL_HOLD * hold_speed_factor(speed)

    def reset(self):
        """the segment reset (the fields used here)."""  # 0x157008
        self.V, self.spin, self.best_spin, self.seg_type = 0.0, 0, 0, 0
        self.rail_frames, self.mult = -1, 1.0

    def exit_rail(self, switch: int, grind_type: int):
        """the segment start at rail exit (switch = stance != profile stance; grind type at that moment).  # 0x156618
        Returns (record fields or None, points, meter reward before the boost-meter stat scale)."""  # 0x11B018
        rec, pts, reward = None, 0, 0.0
        if self.seg_type != 0:
            s = round180(self.spin)
            secs = self.rail_frames / 60.0
            if secs > 1.0:
                self.combo += 1
            rec = {"rail_kind": grind_type & 0xF, "rail_spin_idx": (abs(s) // 180) & 0xF,
                   "rail_seconds": secs}
            reward = max(0.0, self.V * METER_SCALE)
            v = self.mult * self.V * METER_SCALE
            if v >= 0.0:
                i = int(v * 10000.0 + 5.0)
                pts = i - i % 10
            self.reset()
            self.mult = SWITCH_MULT if switch else 1.0
        self.switch = switch
        self.seg_type = grind_type
        return rec, pts, reward
