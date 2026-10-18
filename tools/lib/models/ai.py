"""AI racer model: per-frame control word for a computer rider.

Named quantities only. The output word uses the same layouts as
models/input.py (`decode_word` reads it), so the integrated model (models/boarder.py) can drive AI
riders exactly like the player.

Frame order inside the boarder update:
  * track scan: `follow_line` (race path projection, look-ahead 800 cm, path heading, line re-pick)
  * input: `ai_word` -> catch-up time scale step, control-state handler, skill re-selection.

Everything the AI reads from the rider is gathered in `RiderView`; everything it keeps between
frames is `AIState` (the computer handler's fields). The game RNG is injected (`rng.rand()` returns
a non-negative int, like the game's rand()).
"""
from __future__ import annotations

import math
import random
from dataclasses import dataclass, field
from typing import List, Optional

import numpy as np

import race as rc
from ee_math import atan2_ee
from fpu import fpu_add, fpu_sub
from input import GRAB_SLOT_TABLE, NO_TRICK

UP = np.array([0.0, 0.0, 1.0])

BUMP, CRUISE, CHANGE_FAKIE, FAKIE_CRUISE, GATE_TO_BASE = 2, 3, 4, 5, 7
GATE_ANTICIPATE, GATE_LAUNCH, JUMP, LAND_HARD, LAND_NORMAL = 8, 9, 10, 11, 12
NATURAL_AIR, PREWIND, RAIL_SLIDE, SPIN, SIT_AND_WAIT, GETUP_FROM_SIT = 13, 14, 15, 16, 17, 18
WIPE_OUT, WIPE_OUT_RECOVER = 19, 20
GROUND_LAYOUT = (BUMP, CRUISE, CHANGE_FAKIE, GATE_TO_BASE, LAND_HARD, LAND_NORMAL, GETUP_FROM_SIT)
MOTION_AIR, MOTION_RAIL, MOTION_WIPEOUT, MOTION_RECOVER = 1, 3, 5, 6

STEER_GAIN = 6.2897024            # steer per radian of bearing error
STEER_DEAD = 0.2                  # below this the steer is 0
STEER_MAX = 0.9705659
SKILL_STEER = 1.0241041           # skill divisors: value * (skill / divisor) when skill != 1
SKILL_PITCH = 7.8941293
SKILL_GATE = 3.2585914
SKILL_TUCK = 2.211699
SKILL_SPEED = 1.4103198
SKILL_STUCK = 31.410845
SKILL_HOLD = 14.454015
SPEED_BAND = 138.88889            # cm/s (5 km/h) around a requested speed
TUCK_SLOW = 833.3333              # below this speed the tuck is always full
JUMP_WINDOW = 300.0               # cm ahead of the path parameter searched for take-off zones
PREWIND_WINDOW = 50.0
SPEED_WINDOW = 600.0
JUMP_MAX_LATERAL = 153.10164      # cm from the line to commit to a jump
JUMP_MAX_STEER = 0.536165
GATE_PUSH_TIME, GATE_BACK_TIME = 30.0, 60.0    # countdown clock units (2 per frame) before GO
TRICK_TIME = (0.8, 0.8057834, 0.78212035)      # release/choose time = a / (stat/255 * b + c)
TRICK_SHORT_RELEASE = 0.3
TRICK_STAT_OFFSET = 16
SHORT_HOLD_LIMIT = 20.0
SHOVE_RANGE = 200.0
SHOVE_MIN, SHOVE_MAX = math.radians(30.0), math.radians(150.0)
TARGET_RANGE = 700.0              # cm: riders considered as overtake / attack targets
TARGET_DROP = 1050.0              # cm: current target dropped beyond this
TARGET_LATERAL_DROP = 600.0       # cm off the line: target dropped
TARGET_LATERAL_MAX = 540.0        # cm off the line: no new target
TARGET_PERIOD = 12                # frames between target searches
PASS_CLEARANCE = 150.0            # cm: passing cone half-width
ATTACK_LEAD = 300.0               # cm ahead of the target's velocity
ATTACK_WAIT = 200.0               # cm: brake for a target behind only beyond this
ATTACK_CONE, ATTACK_BOOST_CONE = math.radians(50.0), math.radians(15.0)
WRONG_WAY_ANGLE = math.radians(60.0)
WRONG_WAY_FRAMES = 60
OFFPATH_FRAMES = 222.0
OFFPATH_LATERAL, OFFPATH_LATERAL_KNOCKDOWN = 1000.0, 1500.0
AIR_TIMEOUT = 30.0                # s airborne -> reset request
SHOVE_PERIOD = 6
SHOVE_BLOCK_ANIM = 0x15           # channel-1 animation that blocks shoves
LOOKAHEAD = 800.0                 # cm
LINE_REPICK_LATERAL = 500.0
LINE_END_MARGIN = 200.0
LINE_CANDIDATES = 6
LINE_CLASS_WEIGHT = 23187.357     # cost bonus per point of line-class match
LINE_CLASS_DEFAULT = 50
BEHAVIOUR_JUMP, BEHAVIOUR_CRUISE, BEHAVIOUR_PASS, BEHAVIOUR_ATTACK = 0, 1, 2, 3


def scaled(value: float, skill: float, divisor: float) -> float:
    return value if skill == 1.0 else value * (skill / divisor)


def q6(v: float) -> int:
    return int(v * 31.0) & 63


def s2(v: float) -> int:
    return 3 if v < -0.5 else (1 if v > 0.5 else 0)


def set_field(word: int, lo: int, bits: int, value: int) -> int:
    mask = ((1 << bits) - 1) << lo
    return (word & ~mask) | ((value << lo) & mask)


@dataclass
class OtherRider:
    index: int
    pos: np.ndarray
    vel: np.ndarray
    rank: int = 0
    is_human: bool = False


@dataclass
class RiderView:
    """What the AI reads from its own cBoarder (cBoarder offsets)."""
    index: int
    pos: np.ndarray
    vel: np.ndarray
    side: np.ndarray                    # render matrix row 0 (right)
    fwd: np.ndarray                     # row 1 (nose)
    up: np.ndarray                      # row 2
    yaw: float
    motion: int
    control: int
    path: rc.Path                       # current AI racing line
    path_param: float
    path_param_prev: float
    path_lateral: float
    lookahead: np.ndarray
    path_heading: float
    boost_meter: float
    rank: int = 0
    board_class: int = 0                # cBoarder+0x420 (spin step 360 deg for class 2)
    profile_board_class: int = 0        # profile+0x44 (grab table)
    character: int = 0                  # profile+0x5C (grab table)
    stat_air: int = 128                 # profile+0x14
    stat_trick: int = 128               # profile+0x1D
    gate_cur: float = 0.0               # gate filter current / target
    gate_target: float = 0.0
    pred_time_left: float = 0.0
    air_elapsed: float = 0.0
    pred_land_normal: np.ndarray = field(default_factory=lambda: UP.copy())
    spin_angle: float = 0.0             # rad
    flip_angle: float = 0.0
    spin_rate: float = 1.0              # rad/s of the Spin control (handler data)
    flip_rate: float = 1.0
    channel1_anim: int = 0
    rival_dist: Optional[np.ndarray] = None       # per racer
    rival_bearing: Optional[np.ndarray] = None
    catchup_rival: int = -1
    catchup_near: float = 0.0
    catchup_far: float = 0.0
    grudge: rc.Grudge = field(default_factory=rc.Grudge)
    finished: bool = False


@dataclass
class RaceView:
    frame: int
    clock: float
    mode: rc.EventMode
    riders: List[OtherRider]
    human_rank: int = 0


@dataclass
class AIState:
    """Computer handler fields."""
    target_speed: float = 0.0         # requested speed (also the gate rock direction while gating)
    skill: float = 1.0
    skill_behind: float = 1.0         # used while the human is ahead (or no rival)
    skill_ahead: float = 1.0          # used while this AI leads the human
    trick: int = NO_TRICK
    spin_flip_on: bool = False
    short_hold: bool = False
    trick_allowed: bool = False
    spin_dir: float = 0.0
    flip_dir: float = 0.0
    target: int = -1                  # rider being passed or attacked
    offpath_frames: int = 0
    wrongway_frames: int = 0
    behaviour: int = BEHAVIOUR_CRUISE
    time_scale: float = 1.0


def reset_ai(st: AIState, levels):
    """Computer reset: clear trick/target state, behaviour = cruise, skill = 'human ahead' level."""
    st.spin_flip_on = st.short_hold = st.trick_allowed = False
    st.spin_dir = st.flip_dir = 0.0
    st.target_speed = 0.0
    st.offpath_frames = st.wrongway_frames = 0
    st.trick = NO_TRICK
    st.target = -1
    st.skill_ahead, st.skill_behind = levels
    st.skill = st.skill_behind
    st.behaviour = BEHAVIOUR_CRUISE


class GameRNG:
    def __init__(self, seed=0):
        self.r = random.Random(seed)

    def rand(self) -> int:
        return self.r.randrange(0, 1 << 31)


def speed(v) -> float:
    return float(np.linalg.norm(v))


def steer_toward(r: RiderView, target: np.ndarray, skill: float) -> float:
    """Steer (+ = left) toward a point: bearing in the board plane, gain, dead band, clamp."""
    d = target - r.pos
    n = np.linalg.norm(d)
    if n == 0.0:
        return 0.0
    d = d / n
    d = d - np.dot(d, r.up) * r.up
    n = np.linalg.norm(d)
    if n == 0.0:
        return 0.0
    d = d / n
    ang = -math.atan2(float(np.dot(d, r.side)), float(np.dot(d, r.fwd)))
    mag = scaled(abs(ang) * STEER_GAIN, skill, SKILL_STEER)
    if mag < STEER_DEAD:
        return 0.0
    return math.copysign(min(mag, STEER_MAX), ang)


def pitch_toward(r: RiderView, normal: np.ndarray, skill: float) -> float:
    """Forward component of a normal after removing its side component (air tilt cue)."""
    v = normal - np.dot(normal, r.side) * r.side
    n = np.linalg.norm(v)
    if n == 0.0:
        return 0.0
    return scaled(float(np.dot(v / n, r.fwd)), skill, SKILL_PITCH)


def trick_time(stat_air: int) -> float:
    a, b, c = TRICK_TIME
    return a / (stat_air / 255.0 * b + c)


def random_grab(r: RiderView, rng) -> int:
    return GRAB_SLOT_TABLE[r.character][r.profile_board_class][rng.rand() % 15]


def lead_over_rival(r: RiderView) -> Optional[float]:
    if r.catchup_rival < 0 or r.rival_dist is None:
        return None
    return rc.catchup_lead(float(r.rival_dist[r.catchup_rival]), float(r.rival_bearing[r.catchup_rival]),
                           r.path_heading)


def mask_of(hits, ev_type):
    found, edge = None, False
    for e, e_edge in hits:
        if e.type == ev_type:
            if found is None:
                found = e
            edge = edge or e_edge
    return found, edge


def jump_zone(st: AIState, r: RiderView):
    """Take-off zone in [prev, param + 300]: stores the requested speed, returns
    (inside, spin, flip) or None."""
    e, _ = mask_of(r.path.events_in(r.path_param_prev, r.path_param + JUMP_WINDOW), rc.Ev.AI_JUMP)
    if e is None:
        return None
    st.target_speed = (e.param >> 2) * rc.KMH_TO_CMS
    return e.start <= r.path_param, bool(e.param >> 1 & 1), bool(e.param & 1)


def speed_request(r: RiderView):
    """Speed request in [prev, param + 600]: (speed cm/s, inside) or None."""
    e, _ = mask_of(r.path.events_in(r.path_param_prev, r.path_param + SPEED_WINDOW), rc.Ev.AI_SPEED)
    if e is None:
        return None
    return e.param * rc.KMH_TO_CMS, e.start <= r.path_param


def tuck(st: AIState, r: RiderView, w: int, boost: bool = False) -> int:
    c = scaled(1.0, st.skill, SKILL_TUCK)
    if speed(r.vel) < TUCK_SLOW:
        c = 1.0
    w = set_field(w, 11, 6, q6(c))
    return w | 8 if boost else w


def brake(w: int) -> int:
    return set_field(w, 17, 6, 31)


def set_turn(w: int, steer: float) -> int:
    return set_field(w, 5, 6, q6(steer))


def choose_trick(st: AIState, r: RiderView, spin: bool, flip: bool, rng):
    """Trick plan at an AI take-off (spin/flip flags from the zone)."""
    st.trick = NO_TRICK
    st.spin_flip_on = (spin or flip) and (rng.rand() % 100 - TRICK_STAT_OFFSET < r.stat_trick)
    roll = rng.rand() % 100
    hold = 0.0 if st.skill == 1.0 else 1.0 - st.skill / SKILL_HOLD
    st.short_hold = SHORT_HOLD_LIMIT < roll * hold
    st.trick_allowed = False
    st.trick = random_grab(r, rng) if rng.rand() % 100 - TRICK_STAT_OFFSET < r.stat_trick else NO_TRICK
    if st.spin_flip_on:
        st.spin_dir = 1.0 if spin else 0.0
        st.flip_dir = 1.0 if flip else 0.0
        if rng.rand() & 1:
            st.spin_dir = -st.spin_dir
        if rng.rand() & 1:
            st.flip_dir = -st.flip_dir


def reset_request(st: AIState, r: RiderView, race: RaceView) -> bool:
    err = rc.wrap_angle(r.path_heading - r.yaw)
    if st.target < 0:
        st.wrongway_frames = st.wrongway_frames + 1 if abs(err) > WRONG_WAY_ANGLE else 0
    else:
        st.wrongway_frames = 0
    wrong_way = st.wrongway_frames > WRONG_WAY_FRAMES
    if r.motion == MOTION_WIPEOUT:
        st.offpath_frames += 1
        wrong_way = False
    else:
        if r.motion == MOTION_RAIL and st.wrongway_frames > 0:
            return True
        if race.mode == rc.EventMode.PRACTICE_KNOCKDOWN:
            if abs(r.path_lateral) >= OFFPATH_LATERAL_KNOCKDOWN:
                st.offpath_frames += 1
            else:
                st.offpath_frames = 0
        elif r.motion in (MOTION_AIR, MOTION_RECOVER):
            wrong_way = False
            st.offpath_frames = 0
            if r.motion == MOTION_AIR and r.air_elapsed > AIR_TIMEOUT:
                return True
        elif abs(r.path_lateral) >= OFFPATH_LATERAL:
            st.offpath_frames += 1
        else:
            wrong_way = False
            st.offpath_frames = 0
    want = wrong_way or scaled(float(st.offpath_frames), st.skill, SKILL_STUCK) > OFFPATH_FRAMES
    if r.control in (WIPE_OUT, WIPE_OUT_RECOVER):
        lead = lead_over_rival(r)
        far_behind = lead is not None and lead < r.catchup_near
        if not want and not far_behind:
            return False
        return not (lead is not None and lead > r.catchup_far)
    return want and st.target < 0


def shove_side(r: RiderView, race: RaceView) -> Optional[int]:
    """Shove the grudge rival when he is alongside (30..150 deg off the nose) within 2 m."""
    if r.channel1_anim == SHOVE_BLOCK_ANIM or race.frame % SHOVE_PERIOD or r.rival_dist is None:
        return None
    g = r.grudge
    for o in race.riders:
        j = o.index
        if j == r.index or r.rival_dist[j] >= SHOVE_RANGE:
            continue
        diff = rc.wrap_angle(r.yaw - r.rival_bearing[j])
        if SHOVE_MIN < abs(diff) < SHOVE_MAX and g.active and g.rival == j and g.kind in (1, 2):
            return 3 if diff > 0.0 else 1
    return None


def select_target(st: AIState, r: RiderView, race: RaceView):
    """Keep / drop / pick the rider to pass or attack. Returns (has_target, attack_flag or None)."""
    if st.target >= 0:
        if r.rival_dist[st.target] > TARGET_DROP or abs(r.path_lateral) > TARGET_LATERAL_DROP:
            st.target = -1
        else:
            return True, None
    attack = None
    if abs(r.path_lateral) < TARGET_LATERAL_MAX and race.frame % TARGET_PERIOD == 0:
        best = 1e10
        for o in race.riders:
            j = o.index
            if j == r.index or r.rival_dist[j] > TARGET_RANGE:
                continue
            k = rc.grudge_wants_attack(r.grudge, j, race.mode)
            c = math.cos(rc.wrap_angle(r.rival_bearing[j] - r.yaw))
            score = r.rival_dist[j] * ((1.0 + abs(c)) if k else (1.0 - c))
            if score <= best:
                best, attack, st.target = score, k, j
    return st.target >= 0, attack


def next_behaviour(st: AIState, r: RiderView, race: RaceView, attack_default: bool):
    if jump_zone(st, r) is not None:
        st.target = -1
        st.behaviour = BEHAVIOUR_JUMP
        return
    has, attack = select_target(st, r, race)
    if not has:
        st.behaviour = BEHAVIOUR_CRUISE
        return
    if attack is None:
        attack = attack_default
    st.behaviour = BEHAVIOUR_ATTACK if attack else BEHAVIOUR_PASS


def behaviour_cruise(st, r, race, w, rng):
    w = set_turn(w, steer_toward(r, r.lookahead, st.skill))
    req = speed_request(r)
    v = speed(r.vel)
    if req is not None:
        st.target_speed, inside = req
        if v > st.target_speed + SPEED_BAND:
            if inside:
                w = brake(w)
        elif v < scaled(st.target_speed - SPEED_BAND, st.skill, SKILL_SPEED):
            w = tuck(st, r, w, boost=inside)
    else:
        boost = scaled(r.boost_meter, st.skill, SKILL_SPEED) >= 1.0
        w = tuck(st, r, w, boost=boost)
    if jump_zone(st, r) is not None:
        st.target = -1
        st.behaviour = BEHAVIOUR_JUMP
        return w
    has, attack = select_target(st, r, race)
    if has and attack is not None:
        st.behaviour = BEHAVIOUR_ATTACK if attack else BEHAVIOUR_PASS
    return w


def behaviour_jump(st, r, race, w, rng):
    z = jump_zone(st, r)
    if z is None:
        st.behaviour = BEHAVIOUR_CRUISE
        return behaviour_cruise(st, r, race, w, rng)
    inside, spin, flip = z
    steer = steer_toward(r, r.lookahead, st.skill)
    w = set_turn(w, steer)
    v = speed(r.vel)
    if inside and abs(r.path_lateral) < JUMP_MAX_LATERAL and abs(steer) < JUMP_MAX_STEER:
        choose_trick(st, r, spin, flip, rng)
        return w | 2
    if v < st.target_speed - SPEED_BAND:
        return tuck(st, r, w, boost=True)
    if v > st.target_speed + SPEED_BAND:
        return brake(w)
    return w


def _target_frame(st, r, race):
    o = race.riders[st.target]
    rel = rc.wrap_angle(r.rival_bearing[st.target] - r.path_heading)
    return o, rel, speed(r.vel), speed(o.vel)


def behaviour_attack(st, r, race, w, rng):
    o, rel, mine, theirs = _target_frame(st, r, race)
    if abs(rel) > math.pi / 2:
        steer = steer_toward(r, r.lookahead, st.skill)
        if r.rival_dist[st.target] > ATTACK_WAIT and mine > theirs:
            w = brake(w)
    else:
        sp = speed(o.vel)
        aim = o.pos + (o.vel / sp * ATTACK_LEAD if sp > 0.0 else 0.0)
        if abs(rel) < ATTACK_CONE:
            w = tuck(st, r, w, boost=abs(rel) < ATTACK_BOOST_CONE)
        elif mine < theirs:
            w = tuck(st, r, w)
        elif mine > theirs:
            w = brake(w)
        steer = steer_toward(r, aim, st.skill)
    w = set_turn(w, steer)
    if jump_zone(st, r) is not None:
        st.target, st.behaviour = -1, BEHAVIOUR_JUMP
        return w
    has, attack = select_target(st, r, race)
    if not has:
        st.behaviour = BEHAVIOUR_CRUISE
    elif attack is not None and not attack:
        st.behaviour = BEHAVIOUR_PASS
    return w


def behaviour_pass(st, r, race, w, rng):
    o, rel, mine, theirs = _target_frame(st, r, race)
    if abs(rel) > math.pi / 2:
        steer = steer_toward(r, r.lookahead, st.skill)
        w = tuck(st, r, w, boost=mine < theirs)
    else:
        dist = float(r.rival_dist[st.target])
        cone = 0.0
        if dist != 0.0:
            cone = math.atan(PASS_CLEARANCE / dist) + (math.pi if dist < 0.0 else 0.0)
        if cone < abs(rel) or mine <= theirs:
            steer = steer_toward(r, r.lookahead, st.skill)
            w = tuck(st, r, w)
        else:
            ang = rel - cone if rel > 0.0 else cone - rel
            s, c = math.sin(ang), math.cos(ang)
            dx, dy = r.lookahead[0] - r.pos[0], r.lookahead[1] - r.pos[1]
            aim = np.array([r.pos[0] + dx * c - dy * s, r.pos[1] + dx * s + dy * c, r.lookahead[2]])
            steer = steer_toward(r, aim, st.skill)
            w = tuck(st, r, w, boost=True)
    w = set_turn(w, steer)
    if jump_zone(st, r) is not None:
        st.target, st.behaviour = -1, BEHAVIOUR_JUMP
        return w
    has, attack = select_target(st, r, race)
    if not has:
        st.behaviour = BEHAVIOUR_CRUISE
    elif attack:
        st.behaviour = BEHAVIOUR_ATTACK
    return w


BEHAVIOURS = {BEHAVIOUR_JUMP: behaviour_jump, BEHAVIOUR_CRUISE: behaviour_cruise,
              BEHAVIOUR_PASS: behaviour_pass, BEHAVIOUR_ATTACK: behaviour_attack}


def ground_word(st, r, race, rng) -> int:
    w = int(reset_request(st, r, race))
    side = shove_side(r, race)
    if side is not None:
        w = set_field(w, 23, 2, side)
    return BEHAVIOURS[st.behaviour](st, r, race, w, rng)


def gate_word(st, r, race) -> int:
    to_go = -race.clock
    if to_go < scaled(GATE_PUSH_TIME, st.skill, SKILL_GATE):
        return 31
    if to_go < scaled(GATE_BACK_TIME, st.skill, SKILL_GATE):
        return 33                                   # -31 in 6 bits
    if r.gate_cur == r.gate_target or st.target_speed == 0.0:
        st.target_speed = -1.0 if r.gate_cur > 0.0 else 1.0
    return q6(st.target_speed)


def prewind_word(st, r, race) -> int:
    w = 0
    e, edge = mask_of(r.path.events_in(r.path_param_prev, r.path_param + PREWIND_WINDOW), rc.Ev.AI_JUMP)
    if e is not None and edge:
        if speed(r.vel) < st.target_speed - SPEED_BAND:
            w |= 8
        w |= 2
    w = set_field(w, 17, 6, q6(steer_toward(r, r.lookahead, st.skill)))
    w = set_field(w, 5, 6, q6(st.spin_dir))
    return set_field(w, 11, 6, q6(st.flip_dir))


def natural_air_word(st, r) -> int:
    st.trick_allowed, st.spin_flip_on, st.short_hold = True, False, False
    w = 0
    if r.pred_time_left < 0.5:
        w = set_field(w, 8, 6, q6(-steer_toward(r, r.lookahead, st.skill)))
        st.trick = NO_TRICK
    return w


def rail_word(st, r, race, rng) -> int:
    w = 0
    z = jump_zone(st, r)
    if z is None or not z[0]:
        bal = min(max(-2.0 * float(r.side[2]), -1.0), 1.0)
        if abs(bal) > 0.1:
            return set_turn(w, bal)
        if r.stat_trick <= rng.rand() % 100 - TRICK_STAT_OFFSET:
            return w
        w |= 8
        if st.spin_dir == 0.0:
            st.spin_dir = bal
        return set_turn(w, 1.0 if st.spin_dir < 0.0 else -1.0)
    choose_trick(st, r, z[1], z[2], rng)
    return w | 2


def spin_word(st, r, rng) -> int:
    t_air = trick_time(r.stat_air)
    pred = r.pred_time_left
    if st.spin_flip_on:
        if st.flip_dir != 0.0:
            a = abs(int(math.degrees(r.flip_angle))) % 360
            if not (5 <= a < 341) and pred < 2.0 * math.pi / max(abs(r.flip_rate), 1e-9):
                st.flip_dir = 0.0
        if st.spin_dir != 0.0:
            step = 360 if r.board_class == 2 else 180
            a = abs(int(math.degrees(r.spin_angle))) % step
            if (a > step - 20 or a < 5) and pred < math.radians(step) / max(abs(r.spin_rate), 1e-9):
                st.spin_dir = 0.0
    if st.trick == NO_TRICK:
        if st.trick_allowed and r.air_elapsed > t_air and pred > t_air:
            st.trick = random_grab(r, rng)
            st.trick_allowed = False
    else:
        if pred < (TRICK_SHORT_RELEASE if st.short_hold else t_air):
            st.trick = NO_TRICK
    w = set_field(0, 12, 2, s2(st.spin_dir))
    w = set_field(w, 14, 2, s2(st.flip_dir))
    w = set_field(w, 0, 8, st.trick)
    if st.trick == NO_TRICK and st.spin_dir == 0.0 and st.flip_dir == 0.0:
        w = set_field(w, 20, 2, s2(-steer_toward(r, r.lookahead, st.skill)))
        w = set_field(w, 22, 2, s2(-pitch_toward(r, r.pred_land_normal, st.skill)))
    return w


def ai_word(st: AIState, r: RiderView, race: RaceView, rng) -> int:
    """One frame of AI input. Also steps the catch-up time scale and re-selects the skill."""
    st.time_scale = rc.step_time_scale(st.time_scale, rc.catchup_factor_ai(lead_over_rival(r), r.catchup_near,
                                                                            r.catchup_far))
    c = r.control
    w = 0
    if c in GROUND_LAYOUT:
        w = ground_word(st, r, race, rng)
    elif c == FAKIE_CRUISE:
        w = set_turn(0, 1.0)
    elif c == GATE_ANTICIPATE:
        w = gate_word(st, r, race)
    elif c in (GATE_LAUNCH, SIT_AND_WAIT):
        w = 31
    elif c == NATURAL_AIR:
        w = natural_air_word(st, r)
    elif c == PREWIND:
        w = prewind_word(st, r, race)
    elif c == RAIL_SLIDE:
        w = rail_word(st, r, race, rng)
    elif c == SPIN:
        w = spin_word(st, r, rng)
    elif c == WIPE_OUT:
        w = int(reset_request(st, r, race))
    elif c == WIPE_OUT_RECOVER:
        w = int(reset_request(st, r, race))
    st.skill = rc.select_skill(st.skill, (st.skill_ahead, st.skill_behind), race.frame, r.catchup_rival >= 0,
                               r.rank, race.human_rank)
    return w


@dataclass
class LineFollow:
    path: rc.Path
    param: float = 0.0
    param_prev: float = 0.0
    lateral: float = 0.0
    point: np.ndarray = field(default_factory=lambda: np.zeros(3))
    lookahead: np.ndarray = field(default_factory=lambda: np.zeros(3))
    heading: float = 0.0
    cache: dict = field(default_factory=dict)


def line_class_wanted(is_human: bool, board_class: int, rank: int, boost_meter: float, skill_prob: float, rng):
    """Preferred line class (0 / 50 / 100) for the re-pick; None for humans (no preference)."""
    if is_human:
        return None
    want = LINE_CLASS_DEFAULT
    if board_class == 2:
        want = 0 if rank == 0 else (100 if boost_meter < 0.0 else LINE_CLASS_DEFAULT)
    elif board_class == 0:
        want = (0 if 0 <= rank - 2 < 2 else LINE_CLASS_DEFAULT) if boost_meter >= 0.10337617 else 100
    else:
        want = (0 if 0 <= rank - 3 < 2 else LINE_CLASS_DEFAULT) if boost_meter >= 0.31186903 else 100
    if int(skill_prob * 100.0) < rng.rand() % 100:
        want = LINE_CLASS_DEFAULT
    return want


def choose_line(track: rc.Track, pos, vel, current: Optional[rc.Path], exclude_current: bool,
                want_class: Optional[int]) -> Optional[rc.Path]:
    sp = speed(vel)
    probe = pos + (vel / sp * LOOKAHEAD if sp > 1e-6 else 0.0)
    ranked = sorted(track.ai_paths, key=lambda p: p.bbox_distance(probe))[:LINE_CANDIDATES]
    best, best_cost = None, 1e37
    for p in ranked:
        if exclude_current and p is current:
            continue
        t, q, _ = p.project(probe)
        if t > p.length() - LINE_END_MARGIN:
            continue
        ahead = p.point_at(fpu_add(np.float32(t), np.float32(LOOKAHEAD)))
        cost = float(np.sum((probe - ahead) ** 2) + np.sum((pos - q) ** 2))
        if want_class is not None:
            cost -= (100 - abs(p.line_class - want_class)) * LINE_CLASS_WEIGHT
        if cost < best_cost:
            best, best_cost = p, cost
    return None if best is current else best


def follow_line(lf: LineFollow, track: rc.Track, pos, vel, frame: int, finished: bool, want_class=None):
    """Race-path projection: parameter, lateral, look-ahead 800 cm, heading; re-pick the line when far
    off it (checked every 60 frames), near its end, or when crossing a re-pick marker."""
    lf.param_prev = lf.param
    lf.param, lf.point, lf.lateral = lf.path.project(pos, lf.cache)
    lf.lookahead = lf.path.point_at(fpu_add(np.float32(lf.param), np.float32(LOOKAHEAD)))
    if not finished:
        new = None
        if lf.lateral > LINE_REPICK_LATERAL and frame % 60 == 0:
            new = choose_line(track, pos, vel, lf.path, False, want_class)
        elif lf.path.length() - LINE_END_MARGIN < lf.param:
            new = choose_line(track, pos, vel, lf.path, True, want_class)
        elif any(e.type == rc.Ev.MARK_300 for e, _ in lf.path.events_in(lf.param_prev, lf.param)):
            new = choose_line(track, pos, vel, lf.path, False, want_class)
        if new is not None:
            lf.path, lf.cache = new, {}
            lf.param, lf.point, lf.lateral = new.project(pos, lf.cache)
            lf.lookahead = new.point_at(fpu_add(np.float32(lf.param), np.float32(LOOKAHEAD)))
            lf.param_prev = lf.param
    # +0x370: 0x2510F8 (0x118090) on the FPU differences
    lf.heading = float(atan2_ee(fpu_sub(np.float32(lf.lookahead[1]), np.float32(lf.point[1])),
                                fpu_sub(np.float32(lf.lookahead[0]), np.float32(lf.point[0]))))
    return lf


def start_slot(racer_index: int, racer_count: int) -> int:
    return racer_index + (6 - racer_count) // 2


def start_pose(track: rc.Track, racer_index: int, racer_count: int, rider_size: float):
    """Start position and heading: the slot's AI line start, moved along its first tangent by
    (size * 90 - 90) cm. Velocity 0, motion Static, control Void (then the gate sequence)."""
    path = track.ai_paths[track.start_paths[start_slot(racer_index, racer_count)]]
    t = path.tangent_at(0.0)
    pos = path.start + t * (rider_size * 90.0 - 90.0)
    return pos, math.atan2(t[1], t[0]), path
