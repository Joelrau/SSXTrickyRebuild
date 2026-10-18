"""In-race camera reference model (port reference for the UE rebuild).

CML records via scripts/cml_payload.py. Game space: Z up, cm, radians, 60 Hz frames, float64 here (the game is float32).

Modelled (cBxCamera::Update 0x177078 for one player camera, no replay, no script/path running):
  * rider direction filter (FUN_00173270), wipe-out freeze blend, mode -> chase definition
  * position orbit + lag + collision (FUN_0016e900, FUN_0016fe60, FUN_0016fb08)
  * look direction, landing dip, roll (FUN_001722e8)
  * smoothing / cut (FUN_00174238)
  * director scripts (reduced): CUT_TO, BLEND_TO (FUN_00175a08 / FUN_00174d28: from / to scratch views
    evaluated every frame with the stage area yaw), SET_MODE, RETURN_TO_GAME_CAM, HOLD, MARK_CUT, ROLL_FOLLOW,
    END; other ops are timed no-ops.
  * stage areas (FUN_001777d0): Camera.stage_areas = {area: 16 floats of the .pbd instance matrix, row vectors}
Not modelled: shake noise table (.data 0x337798), flash value curve, path cameras, CamAnimation, regions,
ORBIT_BLEND_TO (0x175450), bone-relative anchors (no skeleton: bone targets fall back to the anchor), world-line
radius semantics (zero-radius lines).

The clock +0x10 is not reset by 0x173A98 (static camera running since boot): a new camera starts at the cut value.

Validation: `python models/camera.py` checks the formulas against the camera object (0x7FF180) in the live RAM
snapshots and steps the full update once on the converged session-2 camera (must be a fixed point).
"""
from dataclasses import dataclass, field
from typing import Callable, Dict, List, Optional, Tuple
import math
import os
import struct
import sys

DT = 1.0 / 60.0
PI = math.pi
TWO_PI = 2.0 * math.pi

# 8 constants
DIR_KEEP, DIR_NEW = 0.95, 0.05
DIR_KEEP_WIPE, DIR_NEW_WIPE = 0.99, 0.01
DIR_SLOW = 10.0
POS_KEEP, POS_NEW = 0.8, 0.2
ROLL_DIV = 25.0
BLEND_RISE, BLEND_CAP, BLEND_DECAY = 0.029166667, 0.98, 0.003
DIP_WINDOW, DIP_LEAD = 0.083333336, 0.033333335
DIP_MIN, DIP_RANGE = 1000.0, 3000.0
CEIL_PROBE, CEIL_PUSH = 150.0, 20.0
CUT_CLOCK = 500.0
CTOR_FOV, CTOR_NEAR, RESET_FOV = 1.6, 100.0, 1.5
HALF_FOV_MIN, HALF_FOV_MAX = 0.054831136, 1.3962634

# 2 modes
MODE_NAMES = {1: 'chase reverse', 2: 'chase board', 3: 'chase eyes', 4: 'chase near', 5: 'chase far',
              6: 'chase over', 8: 'replay cam'}
VIEW_TO_MODE = {0: 2, 1: 4, 2: 5, 3: 6}

# motion / control ids
M_AIR, M_GROUND, M_RAIL, M_STATIC, M_WIPE, M_WIPE_RECOVER = 1, 2, 3, 4, 5, 6
C_GETUP_FROM_SIT = 0x12


def wrap(a):
    """FUN_001741d0: wrap to (-pi, pi]."""
    a = math.fmod(a, TWO_PI)
    if a <= -PI: a += TWO_PI
    elif a > PI: a -= TWO_PI
    return a


def v_add(a, b): return (a[0] + b[0], a[1] + b[1], a[2] + b[2])
def v_sub(a, b): return (a[0] - b[0], a[1] - b[1], a[2] - b[2])
def v_mul(a, s): return (a[0] * s, a[1] * s, a[2] * s)
def v_dot(a, b): return a[0] * b[0] + a[1] * b[1] + a[2] * b[2]
def v_cross(a, b): return (a[1] * b[2] - a[2] * b[1], a[2] * b[0] - a[0] * b[2], a[0] * b[1] - a[1] * b[0])
def v_len(a): return math.sqrt(v_dot(a, a))


def v_norm(a):
    n = v_len(a)
    return (0.0, 0.0, 0.0) if n == 0.0 else v_mul(a, 1.0 / n)


ZUP = (0.0, 0.0, 1.0)

# Definitions. Field names = cml_payload.CAMERA_FIELDS.
DEF_DEFAULTS = dict(
    pos_attach_rider=0, orbit_lag_enable=0, orbit_lag_div=0.0, pos_smooth_enable=0, pos_smooth_param=0,
    unk_view40_enable=0, unk_view44_value=0.0, anchor_use_bone=0, bone_index=0, stage_area=0,
    base_x=0.0, base_y=0.0, base_z=0.0, shake_time=0.0, shake_amp=0.0, orbit_enable=0, orbit_dist=0.0,
    orbit_dist_boost=0.0, yaw_from_heading=0, yaw_from_bone=0, yaw_offset=0.0, yaw_goofy_flip=0,
    elev_from_velocity=0, elev_from_bone=0, elev_base=0.0, elev_clamp_enable=0, elev_min=0.0, elev_max=0.0,
    elev_div_descending=1.0, elev_div_ascending=1.0, tgt_attach_rider=0, tgt_off_x=0.0, tgt_off_y=0.0,
    tgt_off_z=0.0, tgt_yaw=0.0, tgt_pitch=0.0, tgt_roll=0.0, fov=CTOR_FOV, angle_smooth_div=1.0,
    roll_div=1.0, roll_follow_slope=0, tgt_use_bone=0, tgt_bone_index=0, auto_fov=0, auto_fov_ref=0.0,
    tgt_reverse=0, near_clip=CTOR_NEAR, unk_16c=0.0, static_cut=0)


def make_def(fields: dict) -> dict:
    d = dict(DEF_DEFAULTS)
    d.update({k: v for k, v in fields.items() if k in DEF_DEFAULTS})
    return d


def load_cml(path) -> dict:
    """{'cameras': {name: def}, 'scripts': {name: events}} from one CML via scripts/cml_payload.py."""
    here = os.path.dirname(os.path.abspath(__file__))
    sys.path.insert(0, os.path.join(here, '..', 'scripts'))
    import cml_payload  # noqa: E402
    out = dict(cameras={}, scripts={})
    for r in cml_payload.decode(path):
        if r['type'] == 'Camera':
            out['cameras'][r['name']] = make_def(r['payload'])
        elif r['type'] == 'DirectorScript':
            out['scripts'][r['name']] = r['payload']['events']
    return out


@dataclass
class RiderInput:
    """Rider fields the camera reads (game space). Names: boarder_struct.csv."""
    velocity: tuple = (0.0, 0.0, 0.0)          # +0x150
    forward: tuple = (0.0, 1.0, 0.0)           # +0x180 basis_forward
    yaw: float = 0.0                           # +0x1B0
    anchor: tuple = (0.0, 0.0, 0.0)            # +0x4AD0 render-matrix translation (hips)
    boost_strength: float = 0.0                # +0x130
    motion: int = M_GROUND                     # +0x424
    control: int = 3                           # +0x428
    ground_normal: tuple = (0.0, 0.0, 1.0)     # +0x2A0
    surface: int = 1                           # +0x290
    pred_time_left: float = 0.0                # +0x57B8
    pred_land_velocity: tuple = (0.0, 0.0, 0.0)  # +0x57D0
    pred_land_normal: tuple = (0.0, 0.0, 1.0)    # +0x57E0
    goofy: int = 0                             # rider_profile+0x40


# World line query: f(a, b) -> None or (point, normal). Zero-radius, first hit (5, TODO radius flags).
LineFn = Callable[[tuple, tuple], Optional[Tuple[tuple, tuple]]]


def plane_world(z0: float) -> LineFn:
    """Horizontal plane z = z0, normal +Z, hit from either side (test world)."""
    def q(a, b):
        da, db = a[2] - z0, b[2] - z0
        if (da > 0) == (db > 0) or da == db: return None
        t = da / (da - db)
        return (v_add(a, v_mul(v_sub(b, a), t)), ZUP)
    return q


def new_view():
    v = [0.0] * 22
    v[6] = CTOR_FOV; v[7] = CTOR_NEAR
    return v


@dataclass
class Camera:
    defs: Dict[str, dict]
    scripts: Dict[str, list] = field(default_factory=dict)
    world: Optional[LineFn] = None
    view_index: int = 1                        # +0x00 start value 1
    mode: int = 4                              # +0x428 (view 1 -> chase near)
    cut: int = 1                               # +0x04 (a reset is a cut, 7.6)
    keep_cut: int = 0                          # +0x08
    static_count: int = 0                      # +0x0C
    clock: float = 500.0                       # +0x10 (not reset by 0x173A98)
    blend_done: int = 0                        # +0x18
    blend: float = 0.0                         # +0x20 wipe-out freeze blend
    name: str = ''                             # +0x24
    pushed: int = 0                            # +0xEB4
    roll_forced: int = 0                       # +0xEAC
    C: list = field(default_factory=new_view)  # +0x42C output
    V: list = field(default_factory=new_view)  # +0x484 target
    S: tuple = (0.0, 0.0, 0.0)                 # +0x4DC
    # rider table entry 0 (+0x4E8) and the evaluator copy (+0xB68)
    e_yaw: float = 0.0
    e_dir: tuple = (0.0, 0.0, 0.0)
    e_anchor: tuple = (0.0, 0.0, 0.0)
    last_look: tuple = (1.0, 0.0)              # DAT_00335724/28 last horizontal look direction
    # director
    script: Optional[list] = None
    script_clock: float = 0.0
    script_event: int = 0
    script_stack: list = field(default_factory=list)
    hold: int = 0
    script_evaluated: int = 0                  # +0x1C
    returned: int = 0                          # +0x3C
    stage_areas: Dict[int, list] = field(default_factory=dict)   # FUN_001777d0 (1 Start_0, 2 Finish_0)
    # FUN_00174d28 scratch views (DAT_003cda78 from, DAT_003cdad0 to): copied from V on the first call ever and
    # whenever u <= 0; otherwise they keep their state between frames and blends (statics shared by all cameras)
    blend_f: Optional[list] = None
    blend_t: Optional[list] = None
    stage_yaw: float = 0.0                     # 0x16e900 param 5 of the last position evaluation

    def __post_init__(self):
        self.C[6] = RESET_FOV                  # FUN_00173a98

    def mark_cut(self):                        # FUN_00177db0
        self.cut = 1; self.keep_cut = 1

    def set_mode(self, mode):                  # FUN_001776d0: a mode change is a hard cut
        self.mode = mode; self.cut = 1; self.keep_cut = 1

    def set_view_index(self, i):               # FUN_00173eb8
        self.view_index = i % 4
        self.set_mode(VIEW_TO_MODE[self.view_index])

    def cycle_view(self, step):                # FUN_00173e70 (pause menu, +1 / -1, wraps 0..3)
        self.set_view_index((self.view_index + step) % 4)

    def mode_def_name(self):
        return MODE_NAMES.get(self.mode, 'chase far') if self.mode not in (7, 9, 0) and self.mode <= 8 \
            else 'chase far'

    def update_entry(self, r: RiderInput):
        d, v = self.e_dir, r.velocity
        vl = v_len(v)
        reset = self.cut != 0 or v_len(d) < 0.5          # also covers d == 0
        wipe = r.motion in (M_WIPE, M_WIPE_RECOVER) and r.control != C_GETUP_FROM_SIT
        if not wipe:
            self.e_yaw = r.yaw
            src = r.forward if vl < DIR_SLOW else v
            if reset: d = src
            else: d = v_add(v_mul(v_norm(d), DIR_KEEP), v_mul(r.forward if vl < DIR_SLOW else v_norm(v), DIR_NEW))
        elif r.motion == M_WIPE_RECOVER:
            d = v if reset else v_add(v_mul(v_norm(d), DIR_KEEP * vl), v_mul(v, DIR_NEW))
        else:
            d = v if reset else v_add(v_mul(v_norm(d), DIR_KEEP_WIPE * vl), v_mul(v, DIR_NEW_WIPE))
        self.e_dir = v_norm(d)
        self.e_anchor = r.anchor

    def stage_area(self, area, B):
        """FUN_001777d0 -> (B', yaw) or None: (x, y, z, 1) x M; yaw = heading of (1, 0, 0, 0) x M (atan + quadrant)."""
        M = self.stage_areas.get(area) if area in (1, 2) else None
        if M is None: return None
        P = tuple(B[0] * M[c] + B[1] * M[4 + c] + B[2] * M[8 + c] + M[12 + c] for c in range(3))
        x, y = M[0], M[1]
        if x == 0.0:
            yaw = 0.0 if y == 0.0 else (-PI / 2 if y < 0 else PI / 2)
        else:
            yaw = math.atan(y / x)
            if x < 0: yaw += PI if y > 0 else -PI
        return P, yaw

    def eval_position(self, df, r: RiderInput, V=None, cut=None):
        """FUN_0016e900 on view V (default the camera's +0x484) with cut flag param 4; sets stage_yaw (param 5)."""
        if V is None: V = self.V
        if cut is None: cut = self.cut
        B = (df['base_x'], df['base_y'], df['base_z'])
        self.stage_yaw = 0.0
        if df['stage_area']:
            sa = self.stage_area(df['stage_area'], B)
            if sa is not None: B, self.stage_yaw = sa
        offset = (0.0, 0.0, 0.0)
        d = self.e_dir
        has_dir = v_len(d) != 0.0
        V[12] = 0
        if df['orbit_enable']:
            dist = df['orbit_dist'] + df['orbit_dist_boost'] * r.boost_strength
            e = df['elev_base']
            if df['elev_from_velocity'] and df['pos_attach_rider'] and has_dir:
                slope = PI / 2 - math.atan2(math.hypot(d[0], d[1]), d[2])
                e += slope / (df['elev_div_descending'] if d[2] < 0 else df['elev_div_ascending'])
                V[12] = 1; V[9], V[10], V[11] = d
            if df['elev_clamp_enable']:
                e = min(max(e, df['elev_min']), df['elev_max'])
            # elev_from_bone: no skeleton (TODO)
            lag = df['orbit_lag_enable'] and not cut and df['orbit_lag_div'] != 0
            if lag: e = V[20] + (e - V[20]) / df['orbit_lag_div']
            V[20] = e
            y = df['yaw_offset']
            if df['yaw_goofy_flip'] and r.goofy: y = wrap(PI - y) + PI
            if df['yaw_from_heading']:
                y += -math.atan2(d[0], d[1]) if (d[0] != 0.0 or d[1] != 0.0) else self.e_yaw 
                if y < 0: y += TWO_PI
            if lag: y = V[19] + wrap(y - V[19]) / df['orbit_lag_div']
            V[19] = y
            ce = math.cos(e)
            offset = (dist * math.sin(y) * ce, -dist * math.cos(y) * ce, -dist * math.sin(e))
        A = self.e_anchor if df['pos_attach_rider'] else (0.0, 0.0, 0.0)
        P = v_add(v_add(A, B), offset)
        V[14], V[15] = df['pos_smooth_enable'], df['pos_smooth_param']
        V[16], V[17] = df['unk_view40_enable'], df['unk_view44_value']
        hit = 0
        if df['orbit_enable']:
            P, hit = self.collide(df['orbit_dist'], P, A)
        return P, hit

    def clearance(self, P, A, r):
        """FUN_0016fb08."""
        if self.world is None: return P
        w = v_norm(v_sub(A, P))
        u = v_mul(v_norm(v_sub(ZUP, v_mul(w, v_dot(ZUP, w)))), r)
        h = self.world(v_add(P, u), v_sub(P, u))
        if h is None:
            u = v_cross(u, w)
            h = self.world(v_add(P, u), v_sub(P, u))
        if h is not None:
            P = v_add(h[0], v_mul(h[1], r))
        return P

    def collide(self, orbit_dist, P, A):
        """FUN_0016fe60 -> (P, hit)."""
        if self.world is None: return P, 0
        Ap = A
        h = self.world(A, v_add(A, (0.0, 0.0, CEIL_PROBE)))
        if h is not None and h[1][2] > 0:
            Ap = v_add(h[0], v_mul(h[1], CEIL_PUSH))
        h = self.world(Ap, P)
        if h is not None:
            pt, n = h
            P1 = v_sub(P, v_mul(n, v_dot(v_sub(P, pt), n)))
            h2 = self.world(Ap, P1)
            P = h2[0] if h2 is not None else P1
            n = h2[1] if h2 is not None else n                                 # the record of the last hit
            P = v_add(P, v_mul(n, 60.0 if orbit_dist > 500 else 40.0))
            return P, 1
        return self.clearance(P, A, 100.0 if orbit_dist > 500 else 40.0), 0

    def eval_target(self, df, r: RiderInput, landing_dip, add_yaw=0.0, V=None):
        """FUN_001722e8; add_yaw = param 1 (0x174d28 passes the stage area yaw, 0x174888 passes 0)."""
        if V is None: V = self.V
        P = (V[0], V[1], V[2])
        if not df['tgt_attach_rider']:
            yaw, pitch, roll = df['tgt_yaw'], df['tgt_pitch'], df['tgt_roll']
            L = P
        else:
            A = self.e_anchor            # tgt_use_bone: no skeleton, bone n / root fall back to anchor (TODO)
            L = (A[0] + df['tgt_off_x'], A[1] + df['tgt_off_y'], A[2] + df['tgt_off_z'])
            dx, dy = L[0] - P[0], L[1] - P[1]
            if math.hypot(dx, dy) < 0.01:
                dx, dy = self.last_look
            else:
                self.last_look = (dx, dy)
            yaw = math.atan2(dy, dx) + df['tgt_yaw']
            if landing_dip:
                vn = v_dot(v_mul(r.pred_land_velocity, -1.0), v_norm(r.pred_land_normal))
                if r.motion == M_AIR and r.pred_time_left < DIP_LEAD and vn > DIP_MIN:
                    self.clock = 0.0
                if r.motion == M_GROUND and self.clock < DIP_WINDOW:
                    k = min((vn - DIP_MIN) / DIP_RANGE, 1.0)                 
                    s = r.surface
                    K = 166.0 if s == 3 else 125.0 if (s == 4 or s > 14) else 250.0 if 12 <= s <= 14 else 200.0
                    L = (L[0], L[1], L[2] - k * K)
            pitch = math.atan2(L[2] - P[2], math.hypot(L[0] - P[0], L[1] - P[1])) + df['tgt_pitch']
            roll = 0.0
            if (df['roll_follow_slope'] or self.roll_forced) and r.motion not in (M_AIR, M_RAIL, M_WIPE) \
                    and r.motion != M_WIPE_RECOVER:
                roll = slope_roll(r.ground_normal, v_norm(v_sub(L, P)))
                roll = roll / (1.0 if self.roll_forced else df['roll_div'])
            roll += df['tgt_roll']
        if df['tgt_reverse']:                      # 0x1722e8: after both branches
            pitch = -pitch; yaw += PI
        if df['auto_fov'] and df['tgt_attach_rider']:     # fixed angles keep the definition fov
            fov = min(max(df['auto_fov_ref'] / (v_len(v_sub(L, P)) * df['fov']), 0.25), 2.0)
        else:
            fov = df['fov']
        V[3], V[4], V[5], V[6] = yaw + add_yaw, pitch, roll, fov
        V[18] = max(df['angle_smooth_div'], 1.0)

    def flip_and_clear(self, r):
        V, C = self.V, self.C
        if V[12] and wrap(V[3] - V[19]) < 0:       
            V[3] = wrap(V[3] + PI); V[4] = wrap(PI - V[4])
        if V[12] and self.blend > 0:
            C[0], C[1], C[2] = self.clearance((C[0], C[1], C[2]), self.e_anchor, 50.0)

    def smooth(self, r):
        V, C = self.V, self.C
        if self.cut:
            C[:] = V[:]
            self.S = (V[0], V[1], V[2])
            self.clock = CUT_CLOCK
            self.flip_and_clear(r)
            if not self.keep_cut: self.cut = 0
            self.blend_done = 0; self.keep_cut = 0
            return
        if V[21]:
            return
        if self.static_count >= 3:
            V[:] = C[:]
        if not self.blend_done or (V[14] and V[15] > 0):
            self.S = tuple(POS_KEEP * self.S[i] + POS_NEW * V[i] for i in range(3))
        else:
            self.S = (V[0], V[1], V[2])
        if self.static_count < 3:
            C[0], C[1], C[2] = self.S
        div = V[18]
        if self.blend > 0 and div < 50: div = self.blend * 20 + 10
        C[3] += wrap(V[3] - C[3]) / div
        C[4] += wrap(V[4] - C[4]) / div
        C[5] += wrap(V[5] - C[5]) / ROLL_DIV
        C[6] += (V[6] - C[6]) / div
        self.flip_and_clear(r)
        self.blend_done = 0

    def evaluate(self, name, r: RiderInput, smooth=True):
        df = self.defs[name]
        self.name = name
        self.static_count = min(self.static_count + 1, 3) if df['static_cut'] else 0     # FUN_00174c98
        old = (self.V[0], self.V[1], self.V[2])
        P, hit = self.eval_position(df, r)
        if not self.cut:
            P = v_add(old, v_mul(v_sub(P, old), 1.0 - self.blend))
        self.V[0], self.V[1], self.V[2] = P
        self.pushed = hit
        self.eval_target(df, r, 2 <= self.mode <= 6)
        if smooth:
            self.smooth(r)
        self.C[7] = df['near_clip']

    def _eval_scratch(self, df, r, W, cut):
        P, _hit = self.eval_position(df, r, W, cut)        # push-out result dropped
        W[0], W[1], W[2] = P
        self.eval_target(df, r, False, self.stage_yaw, W)

    def blend_to(self, to_name, r, u):
        if self.blend_f is None:                            # DAT_00336648 / DAT_0033664c one-time copies
            self.blend_f, self.blend_t = list(self.V), list(self.V)
        start = u <= 0.0
        if start:
            self.blend_f[:], self.blend_t[:] = self.V[:], self.V[:]
        F, T, V = self.blend_f, self.blend_t, self.V
        dfrom = self.defs.get(self.name)                    # the current camera (+0x24), camera cut flag (+4)
        if dfrom is not None:
            self._eval_scratch(dfrom, r, F, self.cut)
        dto = self.defs[to_name]
        self._eval_scratch(dto, r, T, 1 if start else 0)   # cut = (u <= 0)
        if u >= 1.0:
            V[:] = T[:]; V[12] = 0
            self.smooth(r)
            self.C[7] = dto['near_clip']                     # FUN_00174c58 (to)
            self.blend_done = 1                              # +0x18
            self.name = to_name                              # 0x175a08 strcpy(+0x24)
            return True
        for i in range(3): V[i] = F[i] + (T[i] - F[i]) * u
        for i in (3, 4, 5): V[i] = F[i] + wrap(T[i] - F[i]) * u
        V[6] = F[6] + (T[6] - F[6]) * u
        V[12] = 0
        self.smooth(r)
        if dfrom is not None: self.C[7] = dfrom['near_clip']   # FUN_00174c58 (from)
        return False

    def play_script(self, name):
        ev = self.scripts.get(name)
        if ev is None: return False
        self.script, self.script_clock, self.script_event, self.script_stack = ev, 0.0, 0, []
        self.returned = 0
        return True

    def _script_step(self, r, prev_clock):
        """Returns True if the script evaluated a camera this frame."""
        evaluated = False
        entered = False                                            # 0x175f50: an event completed this frame
        while self.script is not None:
            if self.script_event >= len(self.script):
                if self.script_stack:
                    self.script, self.script_clock, self.script_event = self.script_stack.pop()
                    self.script_event += 1
                    continue
                # FUN_00175eb8 with +0x34 still set: a cut (FUN_00177db0) unless the camera is "chase near" in mode 4
                if self.name != 'chase near' or self.mode != 4:
                    self.mark_cut()
                self.script = None
                break
            e = self.script[self.script_event]
            op, t0, t1 = e['opcode'], e['t0'], e['t1']
            if self.script_clock < t0: break
            if op == 0:                                            # CUT_TO
                if e['name'] in self.defs:
                    if (entered or (self.script_event == 0 and prev_clock == 0.0)) and self.name != e['name']:
                        self.mark_cut()                            # FUN_00177db0
                    self.evaluate(e['name'], r); evaluated = True
            elif op in (1, 0x1C):                                  # BLEND_TO (FUN_00175a08 / FUN_00174d28)
                if e['name'] in self.defs:
                    u = 1.0                                        # t1 == t0: the EE's x / 0 saturates
                    if t1 != t0:
                        tc = t0 if self.script_clock < t0 else (t1 if self.script_clock > t1 else self.script_clock)
                        u = (tc - t0) / (t1 - t0)
                    if 0.0 < u < 1.0: u = ease(e.get('a') or 0, u)
                    self.blend_to(e['name'], r, min(u, 1.0))
                    evaluated = True
            elif op in (5, 6):                                     # SET_MODE
                self.set_mode(4 if op == 5 else 5)
            elif op == 7:                                          # RETURN_TO_GAME_CAM
                self.returned = 1; self.mark_cut()
            elif op in (8, 9):
                self.hold = 1 if op == 8 else 0
            elif op == 13:
                self.mark_cut()
            elif op in (0x18, 0x19):
                self.roll_forced = 1 if op == 0x18 else 0
            elif op == 0x20:
                self.script = None; break
            if self.script_clock < t1 and op in (0, 1, 0x1C):
                break                                              # event still running
            if self.static_count == 3: self.static_count = 0               # 0x175f50: event completed
            entered = True
            self.script_event += 1
        return evaluated

    def step(self, r: RiderInput):
        self.update_entry(r)                                       # 1 (static counter 3 freezes: TODO)
        self.clock += DT
        if self.script is not None:
            prev_clock = self.script_clock
            self.script_clock += DT
            if self._script_step(r, prev_clock) or self.script is not None:
                return                                             # 5 / FUN_00174c70: no chase while a script runs
        if self.mode != 1:                                         # 6 wipe-out freeze blend
            if r.control != C_GETUP_FROM_SIT:
                if r.motion in (M_WIPE, M_WIPE_RECOVER) and not self.pushed:
                    self.blend = min(self.blend + BLEND_RISE, BLEND_CAP)
                else:
                    self.blend = max(self.blend - BLEND_DECAY, 0.0)
                if r.motion not in (M_WIPE, M_WIPE_RECOVER):
                    self.pushed = 0
        self.evaluate(self.mode_def_name(), r)

    def output(self):
        """(position, yaw, pitch, roll, fov, half_fov_render, near_render)."""
        C = self.C
        return ((C[0], C[1], C[2]), C[3], C[4], C[5], C[6],
                min(max(C[6] / 2, HALF_FOV_MIN), HALF_FOV_MAX), max(C[7], 1.0))


def ease(kind, u):
    """BLEND_TO ease: 0 linear, 1 Hermite(0,1,0,1), 2 Hermite(0,1,0,0) smoothstep, 3 Hermite."""
    h00, h10, h01, h11 = 2*u**3 - 3*u**2 + 1, u**3 - 2*u**2 + u, -2*u**3 + 3*u**2, u**3 - u**2
    if kind == 1: return h01 + h11
    if kind == 2: return h01
    if kind == 3: return h10 + h01
    return u


def slope_roll(n, w):
    """4.3 roll: signed angle from the vertical to the ground normal, both projected across w."""
    q = v_norm(v_sub(n, v_mul(w, v_dot(n, w))))
    r = v_norm(v_sub(ZUP, v_mul(w, v_dot(ZUP, w))))
    c = v_dot(r, q)
    if abs(c) > 1: c = 1.0
    ang = math.acos(c)
    if v_dot(v_cross(w, r), q) < 0: ang = -ang
    return ang


# RAM validation
def _ram_check(ram_path, defs):
    ram = open(ram_path, 'rb').read()
    f = lambda a, n=1: struct.unpack_from('<%df' % n, ram, a)
    i = lambda a: struct.unpack_from('<i', ram, a)[0]
    cam = struct.unpack_from('<I', ram, 0x337788)[0]
    Cr, Vr = list(f(cam + 0x42C, 22)), list(f(cam + 0x484, 22))
    for k in (12, 14, 15, 16): Cr[k] = i(cam + 0x42C + 4 * k); Vr[k] = i(cam + 0x484 + 4 * k)
    ent = cam + 0x4E8
    e_dir, e_anchor = f(ent + 8, 3), f(ent + 0x14, 3)
    rider = i(ent)
    df = defs[MODE_NAMES[i(cam + 0x428)]]
    errs = {}
    # position from the stored lag states (4.2 closed form)
    y, e = Vr[19], Vr[20]
    dist = df['orbit_dist'] + df['orbit_dist_boost'] * f(rider + 0x130)[0]
    P = (e_anchor[0] + dist * math.sin(y) * math.cos(e), e_anchor[1] - dist * math.cos(y) * math.cos(e),
         e_anchor[2] + df['base_z'] - dist * math.sin(e))
    errs['V.pos'] = max(abs(P[k] - Vr[k]) for k in range(3))
    L = v_add(e_anchor, (df['tgt_off_x'], df['tgt_off_y'], df['tgt_off_z']))
    errs['V.yaw'] = abs(wrap(math.atan2(L[1] - Vr[1], L[0] - Vr[0]) - Vr[3]))
    errs['V.pitch'] = abs(math.atan2(L[2] - Vr[2], math.hypot(L[0] - Vr[0], L[1] - Vr[1])) + df['tgt_pitch'] - Vr[4])
    errs['V.fov'] = abs(df['fov'] - Vr[6])
    errs['V.div'] = abs(df['angle_smooth_div'] - Vr[18])
    errs['C.near'] = abs(df['near_clip'] - Cr[7])
    errs['C.pos-S'] = max(abs(Cr[k] - v) for k, v in enumerate(f(cam + 0x4DC, 3)))
    errs['V.dir'] = max(abs(e_dir[k] - Vr[9 + k]) for k in range(3))
    # one full step on a converged (stationary) camera must be a fixed point
    step_err = None
    if v_len(f(rider + 0x150, 3)) < DIR_SLOW and v_len(v_sub(f(rider + 0x4AD0, 3), e_anchor)) < 1e-3:
        c = Camera(defs=defs, mode=i(cam + 0x428), cut=0, clock=f(cam + 0x10)[0], blend=f(cam + 0x20)[0])
        c.C, c.V, c.S = list(Cr), list(Vr), f(cam + 0x4DC, 3)
        c.e_dir, c.e_anchor, c.e_yaw = e_dir, e_anchor, f(ent + 4)[0]
        r = RiderInput(velocity=f(rider + 0x150, 3), forward=f(rider + 0x180, 3), yaw=f(rider + 0x1B0)[0],
                       anchor=f(rider + 0x4AD0, 3), boost_strength=f(rider + 0x130)[0], motion=i(rider + 0x424),
                       control=i(rider + 0x428), ground_normal=f(rider + 0x2A0, 3), surface=i(rider + 0x290))
        c.step(r)
        step_err = max([abs(c.C[k] - Cr[k]) for k in range(3)] + [abs(wrap(c.C[k] - Cr[k])) for k in (3, 4, 5, 6)])
    return errs, step_err


if __name__ == '__main__':
    here = os.path.dirname(os.path.abspath(__file__))
    cml = sys.argv[1] if len(sys.argv) > 1 else os.path.join(here, '..', '..', '..', 'extracted', 'DATA', 'CAMERA',
                                                             'COMMONOB.CML')
    defs = load_cml(cml)['cameras']
    ok = True
    for s in (1, 2):
        sys.path.insert(0, os.path.join(here, '..'))
        import traces
        if not traces.available('live', 'ride_session_%d' % s, 'spawn_ram.bin'): continue
        p = str(traces.path('live', 'ride_session_%d' % s, 'spawn_ram.bin'))
        errs, step_err = _ram_check(p, defs)
        bad = {k: v for k, v in errs.items() if v > (0.02 if k in ('V.pos', 'C.pos-S') else 1e-3)}
        print('session %d: %s; fixed-point step err %s' % (
            s, ', '.join('%s %.2g' % kv for kv in errs.items()), 'n/a' if step_err is None else '%.2g' % step_err))
        if bad or (step_err is not None and step_err > 0.02):
            ok = False; print('  FAIL', bad)
    print('OK' if ok else 'FAILED')
