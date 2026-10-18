"""Reference model of SSX Tricky airborne physics, trick rates, scoring and landing.

Units: cm, s, rad; Z up; 60 Hz.
Quaternions are (w, x, y, z), Hamilton product. Board axes in body space: side +X, forward +Y, up +Z.

Simplifications (open items):
  * the world is a single ground plane (point + normal) instead of the collision world;
  * the board contact point is the boarder origin (per-rider pivot offset taken as zero);
  * touchdown threshold uses board_height < 0 (surface field 6 unknown);
  * animation-driven pieces (auto-180, shoves) are omitted; the half-spin play-outs 0x23C / 0x23D are modelled
    (spin_clip_* fields) but not the other channel-0 clips;
  * trick-record internals that are not exported (grab base value, repeat counter) are parameters.
"""
from __future__ import annotations

import math
from dataclasses import dataclass, field

import numpy as np

from ee_math import sincos_ee
from fpu import f32, fpu_add, fpu_sub

DT = 1.0 / 60.0
G_UP = -850.2414          # rising gravity
G_DOWN = -1900.8359       # falling gravity
K_DRAG = 0.20002478       # horizontal drag
AIR_SPEED_CAP = 3347.2222

K_PRE = 11.5172           # prewind seed gain
K_PRE_FLIP = 2.0 / 3.0
K_LATE = 5.2530804
LAUNCH_BOOST_MULT = 1.6
FAKIE_MULT = 0.8
LO_FRACTION = 0.3
RATE_FLOOR_MAX = math.radians(80.0)
SLEW = 75.0               # rad/s^2
SETTLE_GAIN = 150.53665   # (times dt and G_spin)
BAND_NEAR = math.radians(40.0)
BAND_FAR = math.radians(140.0)
LATE_RATE_MAX = 0.7
AIR_TURN_RANGE = math.radians(120.0)
ALIGN_RATE_MAX = math.radians(80.0)
NORMAL_BIAS = 0.5

LAND_PITCH_MIN = -2.967825   # (scaled)
LAND_PITCH_MAX = 2.6840878
NOSE_OK = -math.pi / 4.0
HARD_VN = -2777.1052
BAIL_SURFACES = (0, 6, 10)

V_SPIN_STEP = 0.07
V_FLIP_STEP = 0.25
V_CHAIN_RAIL_PREV = 0.2
V_FROM_RAIL = 0.18
METER_SCALE = 0.6786545
BAIL_METER = -0.1
UBER_WINDOW = 20.0

# Grab / tweak / uber values.  Animation ids are channel-0 trick animations:
# grabs 0x278..0x291, tweaks 0x292..0x2AB, ubers 0x2AC..0x2DB.
V_GRAB_REGISTER = 0.04249751      # x (grabs already finished this air + 1), at the hold point
V_HOLD_PER_FRAME = 0.00075006403  # x hold rate, every frame from registration to finish
V_UBER_PER_SECOND = 0.04500384    # x hold rate x uber animation length (s), at the uber's flag 1
_RATE_GROUPS = (
    (2.0, (0x278, 0x279, 0x27A, 0x27D, 0x281, 0x287, 0x289, 0x28A, 0x28B, 0x28D, 0x28F,
           0x298, 0x29C, 0x29D, 0x2A6)),
    (2.5, (0x27B, 0x291, 0x296, 0x2A8)),
    (1.25, (0x27C, 0x28E)),
    (1.0, (0x27E, 0x282, 0x283, 0x28C)),
    (1.5, (0x27F, 0x280, 0x285, 0x286, 0x288)),
    (1.75, (0x284, 0x290)),
    (4.0, (0x292, 0x293, 0x294, 0x297, 0x29B, 0x2A1, 0x2A3, 0x2A4, 0x2A5, 0x2A7, 0x2A9)),
    (5.0, (0x295, 0x2AB)),
    (3.0, (0x299, 0x29A, 0x29F, 0x2A0, 0x2A2)),
    (3.5, (0x29E, 0x2AA)),
    (18.0, tuple(range(0x2AC, 0x2D0))),
    (20.0, tuple(range(0x2D0, 0x2DC))),
)
HOLD_RATE = {anim: rate for rate, ids in _RATE_GROUPS for anim in ids}
# trick-name index recorded for a finished grab/tweak/uber (the record's grab bytes)
_GRAB_NAMES = (1, 2, 3, 4, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16, 17, 18, 19, 20, 21, 23, 24, 22, 25, 26, 5)
_TWEAK_NAMES = (27, 28, 29, 30, 32, 33, 34, 35, 36, 37, 38, 39, 40, 41, 42, 43, 44, 45, 46, 47, 49, 50, 48, 51, 52, 31)
TRICK_NAME = {**{0x278 + k: n for k, n in enumerate(_GRAB_NAMES)},
              **{0x292 + k: n for k, n in enumerate(_TWEAK_NAMES)},
              **{0x2AC + k: 53 + k for k in range(48)}}
REPEAT_HISTORY = 5                # trick records remembered for the repeat count


def lerp_stat(a: float, b: float, stat: int) -> float:
    """Rider stats are bytes blended linearly."""
    return a + (b - a) * (stat / 255.0)


@dataclass
class RiderStats:
    spin: int = 128       # profile spin byte: G_spin, decay
    land: int = 128       # landing tolerance
    boost: int = 128      # boost gain
    air_turn: int = 128   # air turn rate
    grab: int = 128       # profile+0x14: grab/tweak animation play speed
    prewind: int = 128    # profile+0x18: prewind charge rate scale (0x105A60)

    @property
    def g_spin(self) -> float:
        return lerp_stat(0.5449139, 0.5449139 + 0.6741781, self.spin)

    @property
    def decay(self) -> float:
        return lerp_stat(0.9930117, 0.9930117 + 0.002014339, self.spin)

    @property
    def k_land(self) -> float:
        return lerp_stat(2.040804, 2.040804 - 1.0374061, self.land)

    @property
    def boost_gain(self) -> float:
        return lerp_stat(0.9787758, 0.9787758 + 0.35966223, self.boost)

    @property
    def air_turn_rate(self) -> float:
        return lerp_stat(0.54741764, 0.54741764 + 0.6545031, self.air_turn) * 1.3750083


def qmul(a, b):
    aw, ax, ay, az = a
    bw, bx, by, bz = b
    return np.array([aw * bw - ax * bx - ay * by - az * bz,
                     aw * bx + ax * bw + ay * bz - az * by,
                     aw * by + ay * bw + az * bx - ax * bz,
                     aw * bz + az * bw + ax * by - ay * bx])


def qconj(q):
    return np.array([q[0], -q[1], -q[2], -q[3]])


def qnormalize(q):
    n = np.linalg.norm(q)
    return np.array([1.0, 0, 0, 0]) if n == 0 else q / n


def qaxis(axis, angle):
    axis = np.asarray(axis, float)
    s = math.sin(angle * 0.5)
    return np.array([math.cos(angle * 0.5), axis[0] * s, axis[1] * s, axis[2] * s])


def qrot(q, v):
    u = q[1:]
    v = np.asarray(v, float)
    t = 2.0 * np.cross(u, v)
    return v + q[0] * t + np.cross(u, t)


def basis(q):
    """Returns (side, forward, up) = q*X, q*Y, q*Z."""
    return qrot(q, (1, 0, 0)), qrot(q, (0, 1, 0)), qrot(q, (0, 0, 1))


def quat_from_to(a, b):
    a = np.asarray(a, float) / np.linalg.norm(a)
    b = np.asarray(b, float) / np.linalg.norm(b)
    c = np.cross(a, b)
    d = float(np.dot(a, b))
    if d < -0.999999:
        perp = np.cross(a, (1, 0, 0))
        if np.linalg.norm(perp) < 1e-6:
            perp = np.cross(a, (0, 1, 0))
        return qaxis(perp / np.linalg.norm(perp), math.pi)
    return qnormalize(np.array([1.0 + d, c[0], c[1], c[2]]))


def wrap_pi(a: float) -> float:
    """Wrap to [-pi, pi) the way the game does (floor(a/2pi + 0.5))."""
    return a - 2.0 * math.pi * math.floor(a / (2.0 * math.pi) + 0.5)


def unit(v):
    n = np.linalg.norm(v)
    return v / n if n > 0 else v


def air_accel(vel):
    g = G_UP if vel[2] > 0.0 else G_DOWN
    return np.array([-K_DRAG * vel[0], -K_DRAG * vel[1], g])


def ballistic_step(pos, vel, dt=DT):
    """One RK4 step with velocity-dependent gravity, then the air speed cap."""
    k1x, k1v = vel * dt, air_accel(vel) * dt
    v2 = vel + k1v / 2.0
    k2x, k2v = v2 * dt, air_accel(v2) * dt
    v3 = vel + k2v / 2.0
    k3x, k3v = v3 * dt, air_accel(v3) * dt
    v4 = vel + k3v
    k4x, k4v = v4 * dt, air_accel(v4) * dt
    pos = pos + (k1x + k4x) / 6.0 + (k2x + k3x) / 3.0
    vel = vel + (k1v + k4v) / 6.0 + (k2v + k3v) / 3.0
    speed = np.linalg.norm(vel)
    if speed > AIR_SPEED_CAP:
        vel = vel * (AIR_SPEED_CAP / speed)
    return pos, vel


@dataclass
class GroundPlane:
    point: np.ndarray = field(default_factory=lambda: np.zeros(3))
    normal: np.ndarray = field(default_factory=lambda: np.array([0.0, 0.0, 1.0]))
    surface_id: int = 1

    def height(self, pos) -> float:
        return float(np.dot(pos - self.point, self.normal))


def predict_touchdown(pos, vel, ground: GroundPlane, max_t=8.0):
    """Stand-in for the landing predictor: march the same integrator to the plane."""
    t = 0.0
    p, v = pos.copy(), vel.copy()
    while t < max_t:
        p, v = ballistic_step(p, v)
        t += DT
        if ground.height(p) < 0.0 and np.dot(v, ground.normal) < 0.0:
            return t, ground.normal.copy()
    return max_t, ground.normal.copy()


def auto_align_solve(q, n_pred, board_class=0, takeoff_type=0):
    """Rotation (angle, body axis) from q to the predicted landing frame."""
    n1 = unit(np.asarray(n_pred, float) + np.array([0.0, 0.0, NORMAL_BIAS * n_pred[2]]))
    q_up = quat_from_to((0, 0, 1), n1)
    _, fwd, up = basis(q)
    f_t = unit(fwd - np.dot(fwd, n1) * n1)
    y_up = qrot(q_up, (0, 1, 0))
    axis = np.cross(y_up, f_t)
    s = np.linalg.norm(axis)
    if s > 1e-4:
        ang = math.asin(min(s, 0.99999))
        if np.dot(y_up, f_t) < 0.0:
            ang = math.pi - ang
        q_tw = qaxis(axis / s, ang)
    elif np.dot(y_up, f_t) < 0.0:
        q_tw = qaxis(n1, math.pi)
    else:
        q_tw = np.array([1.0, 0, 0, 0])
    targets = [qmul(q_tw, q_up)]
    if board_class != 2:
        targets.append(qmul(qaxis(n1, -math.pi), targets[0]))   # forward reversed alternative
    best = None
    for tgt in targets:
        rel = qmul(qconj(q), tgt)
        w = rel[0]
        if w > 0.0:
            angle, sgn = 2.0 * math.acos(min(w, 1.0)), 0.5
        else:
            angle, sgn = 2.0 * math.acos(min(-w, 1.0)), -0.5
        sn = math.sin(angle * sgn)
        ax = rel[1:] / sn if abs(sn) > 1e-9 else np.array([0.0, 0.0, 1.0])
        if best is None or abs(wrap_pi(angle)) < abs(wrap_pi(best[0])):
            best = (angle, ax)
    angle, ax = best
    if takeoff_type == 0 or up[2] > 0.0:
        limit = max(math.radians(80.0), math.radians(120.0) * n1[2])
        if abs(angle) > limit:
            angle = 0.0
    return angle, ax


@dataclass
class SpinInput:
    spin: int = 0          # -1/0/+1 Spin field
    flip: int = 0          # -1/0/+1 Flip field
    late_spin: int | None = None   # defaults to spin
    late_flip: int | None = None   # defaults to flip
    late_override: bool = False    # action 0x21 held: use Spin/Flip fields in late mode
    grab: int = 26         # grab id 0..25, 26 = none
    tweak: bool = False    # action 0x20
    air_turn: int = 0
    air_tilt: int = 0

    def dirs(self, late_mode: bool):
        if late_mode and not self.late_override:
            ls = self.spin if self.late_spin is None else self.late_spin
            lf = self.flip if self.late_flip is None else self.late_flip
            return ls, lf
        return self.spin, self.flip

    def late_dirs(self):
        if self.late_override:
            return self.spin, self.flip
        return self.dirs(True)


@dataclass
class SpinState:
    phase: int = 2
    spin_hi: float = 0.0
    spin_lo: float = 0.0
    spin_floor: float = 0.0
    flip_hi: float = 0.0
    flip_lo: float = 0.0
    flip_floor: float = 0.0
    spin_rate: float = 0.0
    flip_rate: float = 0.0
    align_angle: float = 0.0
    align_axis: np.ndarray = field(default_factory=lambda: np.array([0.0, 0.0, 1.0]))
    play_progress: float = 0.0   # +0x0C: play-out progress last applied (animated half-spins, 5.5)
    settle_offset: float = 0.0   # +0x10: settle offset, added to spin_angle in the settle error
    cap_spin: float = 2.0 * math.pi
    cap_flip: float = 2.0 * math.pi
    late_mode: bool = False
    air_turn_mode: bool = False
    air_tilt_invert: bool = False


@dataclass
class AirBody:
    pos: np.ndarray
    vel: np.ndarray
    q: np.ndarray = field(default_factory=lambda: np.array([1.0, 0, 0, 0]))
    spin_angle: float = 0.0     # cBoarder spin accumulator
    flip_angle: float = 0.0     # cBoarder flip accumulator
    air_turn_offset: float = 0.0  # 5.7, -1..1
    air_tilt_offset: float = 0.0
    time_scale: float = 1.0
    board_class: int = 0
    riding_fakie: bool = False   # stance != profile stance
    profile_goofy: bool = False  # profile stance flag (sign of reported spin degrees, 8.2)
    launch_boost_timer: float = 0.0
    time_to_land: float = 0.0    # predictor output
    air_time: float = 0.0
    grab_id: int = 26
    grab_tweaked: bool = False
    trick_phase: int = 0         # 0 grab, 1 tweak, 4 uber
    grab_hold_frames: int = 0    # frames held at the hold point
    trick_anim: "TrickAnim | None" = None   # channel-0 grab/tweak/uber animation
    trick_table: dict | None = None         # grab id -> (grab, tweak, uber) TrickAnimData
    character: int = 0           # profile trick-set character (clip lengths)
    spin_clip_id: int = -1       # channel-0 spin clip: into / cycle / play-out id, -1 = none
    spin_clip_n: int = 0         # frames of the play-out clip
    spin_clip_frame: float = 0.0
    spin_clip_speed: float = 1.0

    @property
    def stance_goofy(self) -> bool:
        """Raw stance bit +0x1B4: profile stance XOR riding fakie."""
        return self.profile_goofy != self.riding_fakie

    @property
    def anim_category(self) -> int:
        """Category of the channel-0 trick animation (14 grab, 15 tweak, 16 uber), 0 otherwise."""
        return self.trick_anim.data.category if self.trick_anim is not None else 0

    @property
    def anim_gated(self) -> bool:
        """7.1 step 1 applies: channel 0 plays a grab, tweak or uber animation (held or not)."""
        return self.anim_category in (CAT_GRAB, CAT_TWEAK, CAT_UBER)

    @property
    def grab_landable(self) -> bool:
        """Animation flag 4 of the channel-0 trick animation."""
        return self.trick_anim is None or self.trick_anim.flag(4)


# Animations are authored at 30 fps: each game frame the player adds
# ANIM_ADVANCE * speed animation frames (independent of the AI catch-up time_scale, checked live). Flag k (1..4) is set iff marker[k-1] < frame after
# that advance (rebuilt every frame, not latched). Grabs and tweaks hold at marker 2 while the button is
# held; ubers never hold. A held grab therefore never reaches its landable marker (marker 4 > marker 2
# in every grab/tweak animation of the game).
ANIM_ADVANCE = 0.5                # animation frames per game frame at speed 1 (30 fps data, 60 Hz game)
CAT_GRAB, CAT_TWEAK, CAT_UBER = 14, 15, 16
GRAB_SPEED_MIN, GRAB_SPEED_SPAN = 0.78212035, 0.8057834   # play speed lerp over profile+0x14


def grab_speed(stat_byte) -> float:
    """0x1275E0: controller speed = (float(byte) x 0.8057834 x 1/255 (0x3B808081)) + 0.78212035 (mul.s, add.s)."""
    return float(fpu_add(f32(f32(f32(stat_byte) * f32(GRAB_SPEED_SPAN)) * f32(0.003921569)), f32(GRAB_SPEED_MIN)))


def clock_step(frame, speed):
    """0x15A030: frame + ((speed x 30) / 60) by add.s (C++ SSXAnim::ClockStep)."""
    return fpu_add(f32(frame), f32(f32(f32(speed) * f32(30.0)) / f32(60.0)))


@dataclass(frozen=True)
class TrickAnimData:
    n_frames: int
    markers: tuple                # flag markers 1..4 (animation frames), None = absent
    category: int                 # 14 grab, 15 tweak, 16 uber


# Fallbacks when scripts/afl.py (and the game files) are not available: the most common values of
# the ex/bx/fr grab, tweak and uber animations.
DEFAULT_GRAB = TrickAnimData(30, (10, 14, 20, 25), CAT_GRAB)
DEFAULT_TWEAK = TrickAnimData(25, (5, 9, 20, 18), CAT_TWEAK)
DEFAULT_UBER = TrickAnimData(61, (10, None, 50, 50), CAT_UBER)


@dataclass
class TrickAnim:
    data: TrickAnimData
    speed: float = 1.0
    frame: float = 0.0
    hold: float | None = None     # hold frame; None once released (or for ubers)
    flags: int = 0                # bit k-1 = flag k, as of the last advance

    def flag(self, k: int) -> bool:
        return bool((self.flags >> (k - 1)) & 1)

    def advance(self):
        new = float(clock_step(self.frame, self.speed))
        self.flags = 0
        for k, m in enumerate(self.data.markers):
            if m is not None and m < new:
                self.flags |= 1 << k
        if self.hold is not None and new > self.hold:
            new = self.hold
        self.frame = min(new, float(self.data.n_frames - 1))

    @property
    def done(self) -> bool:
        return self.frame >= self.data.n_frames - 1


def load_trick_table(board_class: int, character: int):
    """Grab id -> (grab, tweak, uber) animation data for one rider, from the game files via scripts/afl.py.

    Returns None when the files are not available (the model then uses DEFAULT_*)."""
    try:
        import os
        import sys
        here = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
        sys.path.insert(0, os.path.join(here, "scripts"))
        import afl
        bank = afl.Bank()
        out = {}
        for grab_id, ids in bank.grab_table(board_class, character).items():
            row = []
            for aid in ids:
                recs = bank.resolve(aid, board_class, character) if aid is not None else []
                row.append(TrickAnimData(recs[0].n_frames, recs[0].markers, bank.props(aid)[0]) if recs else None)
            out[grab_id] = tuple(row)
        return out
    except Exception:            # noqa: BLE001  (missing files or RAM dump: fall back to defaults)
        return None


def _trick_row(body: AirBody, grab_id: int):
    if body.trick_table is not None and grab_id in body.trick_table:
        return body.trick_table[grab_id]
    return DEFAULT_GRAB, DEFAULT_TWEAK, DEFAULT_UBER


def grab_update(body: AirBody, inp: SpinInput, stats: "RiderStats", scorer: "TrickScorer",
                uber_window: float = 0.0):
    """8.1 grab / tweak / uber selection with the channel-0 animation."""
    a = body.trick_anim
    cat = body.anim_category
    gspeed = grab_speed(stats.grab)
    if body.trick_phase == 3:                          # take-off type: no grabs until rising
        if body.vel[2] <= 0.0:
            return
        body.trick_phase = 0
    if body.grab_id == 26:
        if inp.grab != 26:
            if cat in (CAT_GRAB, CAT_TWEAK):
                free = a.flag(3) or not a.flag(1)
            elif cat == CAT_UBER:
                free = a.flag(3)
            else:
                free = True
            if free:
                grab, _, _ = _trick_row(body, inp.grab)
                if grab is not None:
                    body.trick_anim = TrickAnim(grab, gspeed, hold=grab.markers[1])
                    body.spin_clip_id = -1
                body.grab_id, body.grab_tweaked = inp.grab, False
                body.trick_phase, body.grab_hold_frames = 0, 0
        return
    if a is not None and a.flag(2):                    # at the hold point: first frame registers the grab
        if body.grab_hold_frames == 0:
            scorer.register_grab(body.grab_id, False)
        body.grab_hold_frames += 1
    if body.trick_phase == 4 and cat == CAT_UBER and not a.flag(3):
        return                                         # an uber ignores the pad until its flag 3
    tweak_now = bool(inp.tweak)
    if inp.grab == body.grab_id and tweak_now == (body.trick_phase != 0):
        return                                         # same grab, tweak state unchanged
    if inp.grab == body.grab_id and body.trick_phase == 0:
        if cat == CAT_GRAB and a.flag(2):              # tweak pressed while holding: uber or tweak
            _, tweak, uber = _trick_row(body, body.grab_id)
            if uber_window > 0.0 and uber is not None:
                body.trick_anim = TrickAnim(uber, 1.0, hold=None)
                body.spin_clip_id = -1
                body.trick_phase = 4
            elif tweak is not None:
                body.trick_anim = TrickAnim(tweak, gspeed, hold=tweak.markers[1])
                body.spin_clip_id = -1
                body.trick_phase = 1
            body.grab_tweaked = True
            scorer.register_grab(body.grab_id, True)
        return
    # grab released or changed (or the tweak button released): end the trick
    if cat == CAT_TWEAK:
        a.hold = None
    elif cat == CAT_GRAB:
        if not a.flag(2):
            if not a.flag(1):
                body.trick_anim = None                 # replaced by the air idle animation: landable
            else:
                scorer.register_grab(body.grab_id, False)
        if body.trick_anim is not None:
            a.hold = None
    body.trick_phase, body.grab_hold_frames = 0, 0
    body.grab_id, body.grab_tweaked = 26, False


def trick_anim_step(body: AirBody):
    """Advance the channel-0 trick animation one game frame; a finished one gives way to the air idle."""
    if body.trick_anim is None:
        return
    body.trick_anim.advance()
    if body.trick_anim.done and body.trick_anim.hold is None:
        body.trick_anim = None


# 0x102800: the play-out of the channel-0 into / cycle / rail-to-air-into clip, played when the settle starts.
_PLAYOUT_GROUPS = (
    (0x236, (0x226, 0x22E, 0x273, 0x277)), (0x237, (0x227, 0x22F, 0x272, 0x276)),
    (0x238, (0x228, 0x230)), (0x239, (0x229, 0x231)), (0x23A, (0x22A, 0x232)), (0x23B, (0x22B, 0x233)),
    (0x23C, (0x22C, 0x234, 0x271, 0x275)), (0x23D, (0x22D, 0x235, 0x270, 0x274)),
)
SPIN_PLAYOUT = {i: out for out, ids in _PLAYOUT_GROUPS for i in ids}
HALF_SPIN_CLIPS = (0x23C, 0x23D)
# 0x126FC0: prewind take-off clip -> its into clip
_INTO_FROM_TAKEOFF = {0x219: 0x226, 0x265: 0x226, 0x21A: 0x227, 0x264: 0x227, 0x21B: 0x228, 0x21C: 0x229,
                      0x21D: 0x22A, 0x21E: 0x22B, 0x21F: 0x22C, 0x263: 0x22C, 0x220: 0x22D, 0x262: 0x22D,
                      0x266: 0x272, 0x267: 0x273, 0x268: 0x270, 0x269: 0x271, 0x26A: 0x276, 0x26B: 0x277,
                      0x26C: 0x274, 0x26D: 0x275}
# .data 0x35FED0 [row][take-off type 0..4]
_TAKEOFF_TABLE = ((0x218, 0x218, 0x218, 0x261, 0x260), (0x220, 0x262, 0x262, 0x268, 0x26C),
                  (0x21C, 0x21C, 0x21C, 0x268, 0x26C), (0x21A, 0x264, 0x264, 0x266, 0x26B),
                  (0x21B, 0x21B, 0x21B, 0x269, 0x26D), (0x21E, 0x21E, 0x21E, 0x268, 0x26C),
                  (0x219, 0x265, 0x265, 0x267, 0x26A), (0x21D, 0x21D, 0x21D, 0x269, 0x26D),
                  (0x21F, 0x263, 0x263, 0x269, 0x26D))
# play-out clip lengths by board class when scripts/afl.py (and the game files) are not available
_PLAYOUT_FRAMES = {0x236: (16, 30, 16), 0x237: (16, 15, 16), 0x238: (16, 16, 16), 0x239: (15, 16, 16),
                   0x23A: (16, 16, 16), 0x23B: (16, 16, 16), 0x23C: (16, 18, 17), 0x23D: (17, 17, 17)}
_afl_bank = []


def spin_clip_frames(clip_id: int, board_class: int, character: int) -> int:
    """Frame count of a play-out clip (game files via scripts/afl.py, else the table above)."""
    try:
        if not _afl_bank:
            import os
            import sys
            sys.path.insert(0, os.path.join(os.path.dirname(os.path.dirname(os.path.abspath(__file__))), "scripts"))
            import afl
            _afl_bank.append(afl.Bank())
        recs = _afl_bank[0].resolve(clip_id, board_class, character)
        if recs:
            return recs[0].n_frames
    except Exception:            # noqa: BLE001
        pass
    return _PLAYOUT_FRAMES.get(clip_id, (0, 0, 0))[min(max(board_class, 0), 2)]


def takeoff_row(a: float, b: float, goofy: bool) -> int:
    """Snap then sector: row of the take-off table from the prewind (spin, flip) charges."""
    a = 0.0 if abs(a) < 0.2 else a
    b = 0.0 if abs(b) < 0.2 else b
    if a != 0.0 and b != 0.0:
        phi = math.atan2(abs(b), abs(a))
        if phi > 1.3963:
            a = 0.0
        elif phi < 0.1745:
            b = 0.0
    if a == 0.0 and b == 0.0:
        return 0
    t = math.atan2(-b, -a) if goofy else math.atan2(b, a)
    for row, ok in ((1, abs(t) > 2.7489), (2, t > 1.9635), (3, t > 1.1781), (4, t > 0.3927),
                    (5, t < -1.9635), (6, t < -1.1781), (7, t < -0.3927)):
        if ok:
            return row
    return 8


def prewind_into_clip(prewind_a: float, prewind_b: float, stance_goofy: bool, takeoff_type: int = 0) -> int:
    """Into clip (0x226..0x22D, 0x270..0x277) of a prewind take-off, -1 for the plain take-off."""
    row = takeoff_row(prewind_a, prewind_b, stance_goofy)
    return _INTO_FROM_TAKEOFF.get(_TAKEOFF_TABLE[row][min(max(takeoff_type, 0), 4)], -1)


def directional_into_clip(spin_in: int, flip_in: int, stance_goofy: bool) -> int:
    """The into clip of a late trick by 45 degree sector (flip input negated on the raw stance bit)."""
    if stance_goofy:
        t = math.degrees(math.atan2(-flip_in, -spin_in))
    else:
        t = math.degrees(math.atan2(-flip_in, spin_in))
    if abs(t) > 157.5:
        return 0x22D
    if t > 112.5:
        return 0x229
    if t > 67.5:
        return 0x227
    if t > 22.5:
        return 0x228
    if t < -112.5:
        return 0x22B
    if t < -67.5:
        return 0x226
    if t < -22.5:
        return 0x22A
    return 0x22C


def spin_clip_step(body: AirBody):
    """One controller clock of a channel-0 play-out clip, at the top of the Spin update (after the previous
    frame). Into / cycle clips have no end; a finished play-out hands channel 0 to the air idle (id -1)."""
    if body.spin_clip_id not in range(0x236, 0x23E) or body.spin_clip_n <= 0:
        return
    # 0x15A030: "done" only by the end clamp of a clock that already stood at the last frame (the clock that first
    # reaches it only clamps), so the follow-on comes one clock after the clip reaches its end
    last = float(body.spin_clip_n - 1)
    old = body.spin_clip_frame
    new = float(clock_step(old, body.spin_clip_speed))
    body.spin_clip_frame = min(new, last)
    if last < new and last <= old:
        body.spin_clip_id = -1


def envelope_from_seed(p: float):
    """5.2: (hi, lo, floor) from a seed rate."""
    if p > 0.0:
        return p, LO_FRACTION * p, min(p, RATE_FLOOR_MAX)
    if p < 0.0:
        return LO_FRACTION * p, p, min(-p, RATE_FLOOR_MAX)
    return 0.0, 0.0, 0.0


PREWIND_CHARGE_BASE = 2.000505        # 0x40000846
PREWIND_CHARGE_BASE_RAIL = 4.00101    # 0x40800846 (grind type +0x1DC != 0)


def prewind_charge(cur: float, stick: float, stats: RiderStats, stance_goofy: bool, is_flip: bool, on_rail=False):
    """Prewind charge: (target, rate) of prewind_a (spin) / prewind_b (flip).

    target = the stick, the flip NEGATED unless goofy (flag +0x1B4 ^ 1); rate = base |target - cur|
    lerp(0.8124317, 1.1414471, profile+0x18): an exponential approach (rate / |target - cur| / scale =
    2.0005 on every Prewind frame of ride_session_1/2)."""
    target = -stick if (is_flip and not stance_goofy) else stick
    base = PREWIND_CHARGE_BASE_RAIL if on_rail else PREWIND_CHARGE_BASE
    return target, base * abs(target - cur) * lerp_stat(0.8124317, 0.8124317 + 0.32901537, stats.prewind)


def prewind_seeds(prewind_spin: float, prewind_flip: float, stats: RiderStats,
                  board_class=0, stance_goofy=False):
    """Take-off seeding from the Prewind filters (-1..1).

    The flip seed is negated on the RAW stance bit +0x1B4 (goofy), not on "riding fakie": with the charge's stance
    sign (prewind_charge) the seed is -K G (2/3) flip_in for every stance, the same direction as the in-air target
    -flip_in and the late seed."""
    g = stats.g_spin
    s = K_PRE * g * prewind_spin
    f = K_PRE * g * K_PRE_FLIP * prewind_flip
    if board_class == 2:
        s *= 2.0 / 3.0
    if stance_goofy:
        f = -f
    return s, f


def spin_enter(body: AirBody, st: SpinState, seed_spin: float, seed_flip: float,
               spin_capable_anim: bool, n_pred, takeoff_type=0, prewind=None, into=None) -> SpinState:
    """Spin Enter. Returns a fresh state keeping late_mode.

    prewind = (prewind_a, prewind_b): take-off from a Prewind charge, which plays its into clip;
    into: the into clip already played by a late trick. Neither: no spin clip on channel 0."""
    if body.launch_boost_timer > 0.0:
        seed_spin *= LAUNCH_BOOST_MULT
        seed_flip *= LAUNCH_BOOST_MULT
    if body.riding_fakie:
        seed_spin *= FAKIE_MULT
        seed_flip *= FAKIE_MULT
    new = SpinState(late_mode=st.late_mode)
    new.spin_hi, new.spin_lo, new.spin_floor = envelope_from_seed(seed_spin)
    new.flip_hi, new.flip_lo, new.flip_floor = envelope_from_seed(seed_flip)
    new.spin_rate, new.flip_rate = seed_spin, seed_flip
    new.phase = 0 if spin_capable_anim else 2
    if into is not None:
        body.spin_clip_id = into
    elif prewind is not None and spin_capable_anim:
        body.spin_clip_id = prewind_into_clip(prewind[0], prewind[1], body.stance_goofy, takeoff_type)
    else:
        body.spin_clip_id = -1
    new.align_angle, new.align_axis = auto_align_solve(body.q, n_pred, body.board_class, takeoff_type)
    body.air_turn_offset = body.air_tilt_offset = 0.0
    return new


def fold_trick(body: AirBody):
    """4.4: fold spin/flip into the quaternion (pivot offset taken as zero)."""
    q_trick = qmul(qaxis((0, 0, 1), -body.spin_angle), qaxis((1, 0, 0), -body.flip_angle))
    body.q = qnormalize(qmul(body.q, q_trick))
    body.spin_angle = body.flip_angle = 0.0


def round180(deg: float) -> int:
    """Assumed rounding of the not-exported helper: nearest multiple of 180 degrees."""
    return int(180 * math.floor(abs(deg) / 180.0 + 0.5))


@dataclass
class TrickScorer:
    value: float = 0.0          # V
    points_mult: float = 1.0
    best_spin: int = 0
    best_flip: int = 0
    spin_deg: float = 0.0
    flip_deg: float = 0.0
    total_spin_deg: float = 0.0
    total_flip_deg: float = 0.0
    grab_value: float = 0.0     # base value added by grab registration (not exported; parameter)
    log: list = field(default_factory=list)

    def update_angles(self, spin_deg: float, flip_deg: float):
        self.spin_deg, self.flip_deg = spin_deg, flip_deg
        r = round180(spin_deg)
        if r > self.best_spin:
            self.best_spin = r
            self.value += V_SPIN_STEP
        r = round180(flip_deg)
        if r > self.best_flip and r % 360 == 0:
            self.best_flip = r
            self.value += V_FLIP_STEP

    def register_grab(self, grab_id: int, tweaked: bool):
        self.value += self.grab_value
        self.log.append(("grab", grab_id, tweaked))

    hold_frames: int = -1        # -1 = no grab being held/counted
    hold_rate: float = 0.0
    grab_names: list = field(default_factory=list)   # up to 3 finished grab names this air
    grab_count: int = 0
    uber_count: int = 0
    hold_time_total: float = 0.0
    hold_time_best: float = 0.0

    def grab_reaches_hold(self, anim_id: int):
        """Grab animation reached its hold point (first frame of flag 2)."""
        self.value += V_GRAB_REGISTER * (min(len(self.grab_names), 3) + 1)
        self.hold_frames = 0
        self.hold_rate = HOLD_RATE.get(anim_id, self.hold_rate)

    def trick_upgraded(self, anim_id: int):
        """Tweak or uber started from the held grab: only the hold rate changes."""
        self.hold_rate = HOLD_RATE.get(anim_id, self.hold_rate)

    def tick_hold(self):
        """Once per frame (with the air-time and rail counters)."""
        if self.hold_frames >= 0:
            self.hold_frames += 1
            self.value += self.hold_rate * V_HOLD_PER_FRAME

    def finish_trick(self, anim_id: int, uber_seconds: float = 0.0):
        """Grab/tweak passes its flag-2 edge after release (uber_seconds 0), or an uber passes its
        flag-1 edge (uber_seconds = animation frames / (30 x speed))."""
        if self.hold_frames < 0:
            return
        name = TRICK_NAME.get(anim_id, 0)
        if not self.grab_names:
            self.grab_names.append(name)
        elif self.grab_names[-1] != name and len(self.grab_names) < 3:
            self.grab_names.append(name)
        if 0x2AC <= anim_id <= 0x2DB:
            self.uber_count += 1
        else:
            self.grab_count += 1
        held = self.hold_frames / 60.0
        self.hold_time_total += held
        self.hold_time_best = max(self.hold_time_best, held)
        self.hold_frames = -1
        self.value += uber_seconds * V_UBER_PER_SECOND * self.hold_rate


    def recognise(self):
        """Trick summary in TRICKDEF units."""
        spins = int(math.copysign(self.best_spin // 180, self.spin_deg)) if self.best_spin else 0
        flips = int(math.copysign(self.best_flip // 360, self.flip_deg)) if self.best_flip else 0
        return {"spins_180": spins, "flips": flips, "value": self.value}

    def bank_segment(self):
        """Keep totals across a late-trick restart; scoring of the restart itself is assumed."""
        self.total_spin_deg += abs(self.spin_deg)
        self.total_flip_deg += abs(self.flip_deg)

    def land(self, repeat_count=0, prev_was_rail=False, from_rail=False):
        """8.2: meter reward and points; resets the trick."""
        if prev_was_rail:
            self.value += V_CHAIN_RAIL_PREV
        if from_rail:
            self.value += V_FROM_RAIL
        meter = max(0.0, METER_SCALE * self.value) / (repeat_count + 1)
        raw = METER_SCALE * self.value * self.points_mult
        pts = int(raw * 10000.0 + 5.0)
        pts = (pts - pts % 10) // (repeat_count + 1) if raw > 0 else 0
        summary = self.recognise()
        self.bank_segment()
        self.value, self.best_spin, self.best_flip, self.points_mult = 0.0, 0, 0, 1.0
        return meter, pts, summary

    def bail(self):
        self.value, self.best_spin, self.best_flip = 0.0, 0, 0
        return BAIL_METER


def repeat_count(history: list, record, unrepeatable: bool = False) -> int:
    """Copies of this trick record among the last 5; appends it.
    Records with three or more grabs or an unnameable spin/flip combination never count."""
    n = 0 if unrepeatable else sum(1 for r in history[-REPEAT_HISTORY:] if r == record)
    history.append(record)
    del history[:-REPEAT_HISTORY]
    return n




def boost_add(meter: float, reward: float, stats: RiderStats, uber_window: float):
    """8.3."""
    meter = min(1.0, max(0.0, meter + reward * stats.boost_gain))
    if meter == 1.0 and reward > 0.0:
        uber_window = UBER_WINDOW
    return meter, uber_window


def spin_update(body: AirBody, st: SpinState, inp: SpinInput, stats: RiderStats,
                scorer: TrickScorer, n_pred, uber_window: float = 0.0) -> SpinState:
    dt = body.time_scale / 60.0
    spin_clip_step(body)    # the controller clocked the channel-0 clip after the previous frame
    # A. auto-align
    # 0x100908 in single precision (AirStates.cpp): dt = time_scale x 0.016666668, the angles by add.s
    dtf = f32(body.time_scale) * f32(0.016666668)
    t_land = f32(body.time_to_land)
    angle = f32(st.align_angle)
    if f32(0.0) < t_land and angle != 0.0:
        lo, hi = t_land * f32(-1.3962634), t_land * f32(1.3962634)
        cl = lo if angle < lo else (hi if hi < angle else angle)
        delta = cl * (dtf / dtf if t_land < dtf else dtf / t_land)
        st.align_angle = float(fpu_sub(angle, delta))
        sn, cs = sincos_ee(delta * f32(0.5))
        ax = np.asarray(st.align_axis, float).astype(np.float32)
        b = (cs, sn * ax[0], sn * ax[1], sn * ax[2])
        a = tuple(f32(c) for c in body.q)
        qw = fpu_sub(fpu_sub(fpu_sub(a[0] * b[0], a[1] * b[1]), a[2] * b[2]), a[3] * b[3])
        qx = fpu_sub(fpu_add(fpu_add(a[0] * b[1], a[1] * b[0]), a[2] * b[3]), a[3] * b[2])
        qy = fpu_sub(fpu_add(fpu_add(a[0] * b[2], a[2] * b[0]), a[3] * b[1]), a[1] * b[3])
        qz = fpu_sub(fpu_add(fpu_add(a[0] * b[3], a[3] * b[0]), a[1] * b[2]), a[2] * b[1])
        body.q = qnormalize(np.array([qw, qx, qy, qz], dtype=float))
    # B. integrate trick angles and report degrees: angle + rate x dt (add.s)
    body.spin_angle = float(fpu_add(body.spin_angle, f32(st.spin_rate) * dtf))
    body.flip_angle = float(fpu_add(body.flip_angle, f32(st.flip_rate) * dtf))
    spin_deg = math.degrees(-body.spin_angle if body.profile_goofy else body.spin_angle)
    scorer.update_angles(spin_deg, math.degrees(body.flip_angle))
    # C. rate update
    s_in, f_in = inp.dirs(st.late_mode)
    if (s_in == 0 and f_in == 0) or st.phase == 2:
        if st.phase == 2:
            # animated half-spins (0x100908): the play-out 0x23C / 0x23D adds pi to spin_angle over its length
            # while the settle offset takes it back, so the settle target does not move
            if body.spin_clip_id in HALF_SPIN_CLIPS:
                p = body.spin_clip_frame / (body.spin_clip_n - 1) if body.spin_clip_n > 1 else 1.0
                dp = (p - st.play_progress) * math.pi
                st.play_progress = p
                if body.stance_goofy:
                    dp = -dp
                if body.spin_clip_id == 0x23D:
                    dp = -dp
                body.spin_angle += dp
                st.settle_offset -= dp
        else:   # first settle frame
            po = SPIN_PLAYOUT.get(body.spin_clip_id, -1)
            if po >= 0:     # 0x102800: play the play-out at controller speed G_spin
                n = spin_clip_frames(po, body.board_class, body.character)
                body.spin_clip_id = po if n > 0 else -1
                body.spin_clip_n, body.spin_clip_frame, body.spin_clip_speed = n, 0.0, stats.g_spin
            st.play_progress = 0.0
            st.settle_offset = 0.0
            if body.spin_clip_id in HALF_SPIN_CLIPS:
                st.settle_offset = -math.pi if body.stance_goofy else math.pi
                if body.spin_clip_id == 0x23D:
                    st.settle_offset = -st.settle_offset
            st.phase = 2
            if st.spin_hi < 0.0:
                st.spin_hi = 0.0
            else:
                st.spin_lo = 0.0
            st.spin_floor = 0.0
            if st.flip_hi < 0.0:
                st.flip_hi = 0.0
            else:
                st.flip_lo = 0.0
            st.flip_floor = 0.0
            st.cap_spin = st.cap_flip = 2.0 * math.pi
        a = wrap_pi(body.spin_angle + st.settle_offset)
        b = abs(wrap_pi(body.spin_angle + st.settle_offset - body.air_turn_offset * AIR_TURN_RANGE))
        if b < BAND_NEAR:
            e_s = -a
        elif b > BAND_FAR:
            e_s = -wrap_pi(a - math.pi)
        elif st.spin_hi > 0.0:
            e_s = a + math.pi if a < 0.0 else a
        else:
            e_s = a - math.pi if a > 0.0 else a
        c = wrap_pi(body.flip_angle)
        d = abs(wrap_pi(body.flip_angle - body.air_tilt_offset * AIR_TURN_RANGE))
        if d < BAND_NEAR:
            e_f = -c
        elif st.flip_hi <= 0.0:
            e_f = c - 2.0 * math.pi if c > 0.0 else c
        else:
            e_f = c + 2.0 * math.pi if c < 0.0 else c
        st.cap_spin = min(st.cap_spin, abs(e_s))
        st.cap_flip = min(st.cap_flip, abs(e_f))
        gain = stats.g_spin * SETTLE_GAIN * dt
        st.spin_rate = gain * max(-st.cap_spin, min(st.cap_spin, e_s))
        st.flip_rate = gain * max(-st.cap_flip, min(st.cap_flip, e_f))
    else:   # active
        step = SLEW * dt
        tgt_s = s_in * abs(st.spin_lo + st.spin_hi)
        tgt_f = -f_in * abs(st.flip_lo + st.flip_hi)
        st.spin_rate = max(st.spin_rate - step, min(st.spin_rate + step, tgt_s))
        st.flip_rate = max(st.flip_rate - step, min(st.flip_rate + step, tgt_f))
    # D. envelope decay, floor and clamp
    k = stats.decay
    for axis in ("spin", "flip"):
        hi, lo, floor = getattr(st, axis + "_hi") * k, getattr(st, axis + "_lo") * k, getattr(st, axis + "_floor")
        if lo < 0.0:
            if -floor < hi:
                hi = -floor
                if -floor < lo:
                    lo = -floor
        elif lo < floor:
            lo = floor
            if hi < floor:
                hi = floor
        rate = getattr(st, axis + "_rate")
        rate = lo if rate < lo else (hi if rate > hi else rate)
        setattr(st, axis + "_hi", hi)
        setattr(st, axis + "_lo", lo)
        setattr(st, axis + "_rate", rate)
    # E. grabs, tweaks, ubers with the channel-0 animation clock
    grab_update(body, inp, stats, scorer, uber_window)
    trick_anim_step(body)
    # E. late trick restart and air turn/tilt; none of it runs on the half-spin play-outs
    if st.phase == 2 and body.grab_id == 26 and body.spin_clip_id not in HALF_SPIN_CLIPS:
        if abs(st.spin_rate) < LATE_RATE_MAX and abs(st.flip_rate) < LATE_RATE_MAX and not st.air_turn_mode:
            ls, lf = inp.late_dirs()
            if ls != 0 or lf != 0:
                theta = math.atan2(abs(lf), abs(ls))
                if theta > math.radians(80.0):
                    ls = 0
                elif theta < math.radians(10.0):
                    lf = 0
                seed_s = stats.g_spin * K_LATE * ls
                seed_f = -stats.g_spin * K_LATE * lf
                scorer.bank_segment()
                fold_trick(body)
                st.late_mode = True
                return spin_enter(body, st, seed_s, seed_f, True, n_pred,
                                  into=directional_into_clip(ls, lf, body.stance_goofy))
        # air turn/tilt entry is not gated by the late-trick rate test
        if not st.air_turn_mode:
            if inp.air_turn != 0 or inp.air_tilt != 0:
                st.air_turn_mode = True
                body.spin_clip_id = -1
                body.air_turn_offset = body.air_tilt_offset = 0.0
                st.air_tilt_invert = abs(wrap_pi(body.spin_angle)) > math.pi / 2.0
        if st.air_turn_mode:
            r = stats.air_turn_rate * dt
            if inp.air_turn != 0:
                old = body.air_turn_offset
                body.air_turn_offset = max(old - r, min(old + r, float(inp.air_turn)))
                body.spin_angle += (body.air_turn_offset - old) * AIR_TURN_RANGE
            if inp.air_tilt != 0:
                tilt = -inp.air_tilt if not st.air_tilt_invert else inp.air_tilt
                old = body.air_tilt_offset
                body.air_tilt_offset = max(old - r, min(old + r, float(tilt)))
                body.flip_angle += (body.air_tilt_offset - old) * AIR_TURN_RANGE
    return st


def landing_angles(q, vel, normal):
    """7.1: yaw error psi in [0, pi] and pitch error phi."""
    _, fwd, up = basis(q)
    vhat = unit(vel)
    v_b = unit(vhat - np.dot(vhat, up) * up)
    psi = math.pi / 2.0 - math.asin(max(-0.9999999, min(0.9999999, float(np.dot(fwd, v_b)))))
    phi = -math.atan2(float(np.dot(fwd, normal)), float(np.dot(up, normal)))
    return psi, phi


def landing_test(q, vel, normal, stats: RiderStats, surface_id=1, grab_active=False, grab_landable=True):
    """7.1: returns (ok, psi, phi)."""
    if grab_active and not grab_landable:
        return False, 0.0, 0.0
    if surface_id in BAIL_SURFACES:
        return False, 0.0, 0.0
    psi, phi = landing_angles(q, vel, normal)
    phi_eff = -phi if abs(psi) > math.pi / 2.0 else phi
    scaled = phi_eff * stats.k_land
    return (LAND_PITCH_MIN <= scaled <= LAND_PITCH_MAX), psi, phi


def landing_classify(psi, phi, v_n, board_class=0, regular_stance=True, grab_category=False):
    """7.2: 'LandNormal' or 'LandHard'."""
    nose_ok = phi >= NOSE_OK
    soft = v_n >= HARD_VN
    if grab_category:
        return "LandNormal"
    if board_class == 2:
        if abs(psi) <= math.pi / 2.0 and regular_stance and soft and nose_ok:
            return "LandNormal"
        return "LandHard"
    return "LandNormal" if (nose_ok and soft) else "LandHard"


def land_hard_anim(psi, phi, v_n, board_class=0, regular_stance=True) -> int:
    """7.2: which LandHard animation plays (its length gates the LandHard control)."""
    nose_ok = phi >= NOSE_OK
    backwards = abs(psi) > math.pi / 2.0
    if board_class == 2:
        if backwards or not regular_stance:
            return 0x240
        return 0x23E if v_n < HARD_VN else 0x242
    if not backwards:
        return 0x23E if nose_ok else 0x242
    return 0x23F if nose_ok else 0x243


def landing_speed_factor(psi, phi):
    """7.3 velocity multipliers."""
    pc = min(max(abs(phi), math.radians(15.0)), math.radians(50.0))
    yc = min(max(abs(psi), math.radians(25.0)), math.radians(80.0))
    f = 1.0
    if pc > math.radians(15.0):
        f *= 1.0642503 - 0.24541846 * pc
    if yc > math.radians(25.0):
        f *= 1.113588 - 0.26032454 * yc
    return f


def classify_touchdown(q, vel, normal, stats, board_class=0, regular_stance=True, surface_id=1,
                       grab_active=False, grab_landable=True, grab_category=None):
    """Landing classification: 'WipeOut' / 'LandNormal' / 'LandHard'.

    grab_active: channel 0 plays a grab/tweak/uber animation (AirBody.anim_gated), held or released;
    grab_landable: its flag 4 (AirBody.grab_landable); grab_category: the animation is a plain grab
    (category 14), which forces LandNormal. Defaults to grab_active for older callers."""
    ok, psi, phi = landing_test(q, vel, normal, stats, surface_id, grab_active, grab_landable)
    if not ok:
        return "WipeOut", psi, phi
    v_n = float(np.dot(normal, vel))
    cat14 = grab_active if grab_category is None else grab_category
    return landing_classify(psi, phi, v_n, board_class, regular_stance, cat14), psi, phi


def body_contact(q_rendered, vel, normal, depth):
    """1.5: glancing body hit. Returns (push, new_q, new_vel, bail)."""
    push = 1.1 * depth * normal
    v_n = float(np.dot(normal, vel))
    if v_n >= 0.0:
        return push, q_rendered, vel, False
    f = 0.6 + 0.3 * min(1.0, -0.00045 * v_n)
    up = qrot(q_rendered, (0, 0, 1))
    if np.dot(normal, up) <= f:
        return push, q_rendered, vel, True
    c = np.cross(up, normal)
    s = np.linalg.norm(c)
    q_new = q_rendered
    if s > 1e-5:
        ang = min(math.asin(min(s, 1.0)), math.radians(6.0))
        q_new = qnormalize(qmul(qaxis(c / s, ang), q_rendered))
    return push, q_new, vel - v_n * normal, False


@dataclass
class AirSim:
    body: AirBody
    stats: RiderStats = field(default_factory=RiderStats)
    ground: GroundPlane = field(default_factory=GroundPlane)
    spin: SpinState = field(default_factory=SpinState)
    scorer: TrickScorer = field(default_factory=TrickScorer)
    boost_meter: float = 0.0
    uber_window: float = 0.0
    landed: str | None = None
    apex_z: float = -1e30
    touchdown_vel: np.ndarray | None = None
    touchdown_q: np.ndarray | None = None
    n_pred: np.ndarray = field(default_factory=lambda: np.array([0.0, 0.0, 1.0]))

    def launch(self, seed_spin=0.0, seed_flip=0.0, spin_capable_anim=True, prewind=None):
        t, n = predict_touchdown(self.body.pos, self.body.vel, self.ground)
        self.body.time_to_land, self.n_pred = t, n
        self.spin = spin_enter(self.body, self.spin, seed_spin, seed_flip, spin_capable_anim, n, prewind=prewind)

    def step(self, inp: SpinInput):
        """One 60 Hz frame: motion first, then control (A's update order)."""
        b = self.body
        dt = b.time_scale / 60.0
        b.pos, b.vel = ballistic_step(b.pos, b.vel, dt)
        b.air_time += DT
        b.time_to_land -= DT
        self.apex_z = max(self.apex_z, b.pos[2])
        n = self.ground.normal
        if self.ground.height(b.pos) < 0.0 and np.dot(n, b.vel) < 0.0:
            self._touchdown()
            return
        self.spin = spin_update(b, self.spin, inp, self.stats, self.scorer, self.n_pred)

    def _touchdown(self):
        b = self.body
        n = self.ground.normal
        b.pos = b.pos - self.ground.height(b.pos) * n
        fold_trick(b)                          # step 3 (Void exits Spin)
        self.touchdown_vel, self.touchdown_q = b.vel.copy(), b.q.copy()
        cls, psi, phi = classify_touchdown(b.q, b.vel, n, self.stats, b.board_class, not b.riding_fakie,
                                           self.ground.surface_id, b.anim_gated, b.grab_landable,
                                           b.anim_category == CAT_GRAB)
        if cls == "WipeOut":
            reward = self.scorer.bail()
        else:
            b.vel = b.vel * landing_speed_factor(psi, phi)
            reward, pts, summary = self.scorer.land()
            self.last_summary, self.last_points = summary, pts
        self.last_reward = reward
        self.boost_meter, self.uber_window = boost_add(self.boost_meter, reward, self.stats, self.uber_window)
        self.landed = cls


def run_jump(launch_vel, prewind_spin=1.0, hold_frames=60, spin_dir=1, stats=None, tilt_q=None):
    b = AirBody(pos=np.array([0.0, 0.0, 0.01]), vel=np.array(launch_vel, float))
    if tilt_q is not None:
        b.q = tilt_q
    sim = AirSim(body=b, stats=stats or RiderStats())
    s, f = prewind_seeds(prewind_spin, 0.0, sim.stats)
    sim.launch(s, f, spin_capable_anim=prewind_spin != 0.0, prewind=(prewind_spin, 0.0))
    frame = 0
    while sim.landed is None and frame < 1200:
        held = spin_dir if frame < hold_frames else 0
        sim.step(SpinInput(spin=held))
        frame += 1
    return sim, frame


if __name__ == "__main__":
    np.set_printoptions(precision=1, suppress=True)
    stats = RiderStats()
    launch = (0.0, 1500.0, 900.0)
    print(f"rider stats {stats}; G_spin={stats.g_spin:.3f}, K_land={stats.k_land:.3f}")
    print(f"launch velocity {launch} cm/s, full prewind spin, flat ground\n")
    for hold in (0, 20, 45, 90):
        sim, frames = run_jump(launch, prewind_spin=1.0, hold_frames=hold, stats=stats)
        tot = sim.scorer.total_spin_deg
        print(f"hold spin {hold:3d} frames: airtime {frames} f = {frames / 60:.3f} s, apex {sim.apex_z:6.1f} cm, "
              f"total spin {tot:7.1f} deg, trick {sim.last_summary if sim.landed != 'WipeOut' else '-'}, "
              f"landing {sim.landed}, meter reward {sim.last_reward:+.3f}, meter {sim.boost_meter:.3f}")
    sim, frames = run_jump(launch, prewind_spin=0.0, hold_frames=0, stats=stats)
    sim, frames = run_jump(launch, prewind_spin=0.0, hold_frames=40, stats=stats)
    print(f"no prewind, stick held 40 f (late spin): total spin {sim.scorer.total_spin_deg:.1f} deg, "
          f"trick {sim.last_summary}, landing {sim.landed}")
    sim, frames = run_jump(launch, prewind_spin=0.0, hold_frames=0, stats=stats)
    print(f"plain ollie: airtime {frames / 60:.3f} s, apex {sim.apex_z:.1f} cm (analytic u^2/1700 = "
          f"{900.0 ** 2 / 1700:.1f}), landing {sim.landed}")

    # auto-align: take off pitched 30 deg nose-up, no spin
    tilt = qaxis((1, 0, 0), math.radians(30.0))
    sim, frames = run_jump(launch, prewind_spin=0.0, hold_frames=0, stats=stats, tilt_q=tilt)
    _, phi0 = landing_angles(tilt, sim.touchdown_vel, np.array([0.0, 0, 1]))
    _, phi1 = landing_angles(sim.touchdown_q, sim.touchdown_vel, np.array([0.0, 0, 1]))
    print(f"auto-align: takeoff pitch {math.degrees(phi0):.1f} deg -> touchdown pitch {math.degrees(phi1):.1f} deg, "
          f"landing {sim.landed}")

    print("\nlanding class (speed factor) vs extra board rotation at touchdown, flat ground, land stat 128:")
    sim, _ = run_jump(launch, prewind_spin=0.0, hold_frames=0, stats=stats)
    v = sim.touchdown_vel
    n = np.array([0.0, 0.0, 1.0])
    print(f"  touchdown velocity {v}, n.v = {np.dot(n, v):.1f} cm/s")
    for label, extra in [("pitch", (1, 0, 0)), ("roll", (0, 1, 0)), ("yaw", (0, 0, 1))]:
        row = []
        for deg in (0, 30, 50, 70, 100, 150):
            q = qmul(sim.touchdown_q, qaxis(extra, math.radians(deg)))
            cls, psi, phi = classify_touchdown(q, v, n, stats)
            f = landing_speed_factor(psi, phi) if cls != "WipeOut" else 0.0
            row.append(f"{deg:+4d}:{cls}({f:.2f})")
        print(f"  {label:5s} " + "  ".join(row))
    for deg in (-30, -50, -70, -90):
        q = qmul(sim.touchdown_q, qaxis((1, 0, 0), math.radians(deg)))
        cls, psi, phi = classify_touchdown(q, v, n, stats)
        print(f"  nose-down pitch {deg:+d}: {cls}  (phi={math.degrees(phi):+.0f} deg)")
    big = np.array([0.0, 1500.0, -3000.0])
    cls, _, _ = classify_touchdown(sim.touchdown_q, big, n, stats)
    print(f"  level board, n.v = -3000 cm/s: {cls}")
