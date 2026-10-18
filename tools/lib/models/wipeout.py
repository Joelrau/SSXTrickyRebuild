"""Wipe-out and recovery reference model.

The game tumbles a full rigid body; this model keeps the parts that set timings and speed loss and reduces the body to:
  * a point mass for the tumble (gravity, linear drag, race-line homing, ground bounces),
  * a scalar spin rate (angular drag) for the "is it still spinning" tests,
  * a scalar lying angle for the slide-phase alignment (1/12 per frame convergence).
Assumptions that are NOT from the code are marked ASSUMPTION; they stand in for helpers that are not exported yet
(tumble contact solver, animation lengths, surface table values).

Routines follow the original (@XXXXXX = code address):
  * TumbleBody / tumble_frame(): the rigid tumble (integrator, two-pass contact on the previous update's sphere
    positions, rider pivot rebuild). apply_impulse, the x0.95 step and the self-right, roll and input torques refresh
    WITHOUT the spin cap; only integrate() and the mass-properties step (@153AD8) cap |w|. The mass properties come
    from the 18-sphere rider shape (mass_properties());
  * tumble_rules(): the post-contact order of @10C2F0 (bookkeeping, predictor refresh only on anim 0x2DF, self-right,
    settle without an early exit, quick recovery without a grounded test and with the unprojected look-ahead direction,
    L = (1 - 3.3475 dt) L + dL, timeouts); tumble_bookkeeping(): the 0.1 s kind-3 grace; settle_decision(); tumble_anim();
  * slide_update(): the slide order (stored-h snap, then the +-400 cm ground check, fall -> body re-init while the rest
    of the update still runs). Slide gravity is the surface record's field 0, the snap uses height + smoothed surface
    field 7. phase_time keeps running from the tumble into the slide (the get-up speed limit uses it);
  * begin_recover(): the @10F178 rotations, the get-up animation picked from the lying pose and the look-ahead
    direction; recover_target(): arc(Y' -> d) (x) arc(Z -> n) with the 0.99999 clamp; anim_current(): the impact chains;
    reset_wait(): @106A90; respawn_line_pick(): @118C08;
  * the entry impulse is a rigid-body impulse (restitution 0.15, no friction) along the NEGATED info normal, applied
    only when the contact point already moves away from the obstacle.
"""
from dataclasses import dataclass, field
from typing import List
import math
import numpy as np

DT = 1.0 / 60.0

IMPACT_RESTITUTION = 0.15          # impulse at the impact point on entry
TUMBLE_GRAVITY = 1900.8359         # cm/s^2 during the tumble
TUMBLE_LINEAR_DRAG = 0.5           # 1/s
TUMBLE_ANGULAR_DRAG = 0.5          # 1/s
HOMING_ACCEL = 600.0               # cm/s^2 towards the race line ...
HOMING_WINDOW = 0.15               # ... while air_time is below this
INPUT_SPIN_DAMPING = 3.3475056     # 1/s, applied to angular momentum each frame
SETTLE_PHASE_TIME = 0.25           # s of phase time before a contact can become a slide
STEEP_NZ = 0.34906584              # n_z at or below this: too steep to settle
SLIDE_FACE_DOT = 0.75              # |forward . n| that picks back/face slide anims
QUICK_RECOVER_TOTAL = 0.5          # quick recovery: total time >  0.5 s
QUICK_RECOVER_PHASE = 0.1          #                 phase time > 0.1 s
QUICK_RECOVER_SPIN = 7.0           #                 |w| < 7 rad/s
QUICK_RECOVER_UP = 0.85            #                 up . n > 0.85
AIR_RIGHT_UP = 0.9                 # air self-right: up . n_land > 0.9 ...
AIR_RIGHT_SPIN = 4.0               # ... and |w| < 4 rad/s
TUMBLE_PHASE_TIMEOUT = 3.0         # s -> forced reset while racing
TUMBLE_TOTAL_TIMEOUT = 7.0         # s
STUCK_TIMEOUT_VALUE = 1.0e6        # written into stuck accumulator a
SLIDE_DAMPING_PER_FRAME = 0.98333335   #
SNAP_LIMIT = 8.333333              # cm per frame
SLIDE_ALIGN_FRACTION = 1.0 / 12.0  # lying angle shrinks by this fraction per frame
SLIDE_ALIGNED_SIN = 0.1            # |n x forward| below this counts as lying flat
GETUP_SPEED_BASE = 833.3333        # cm/s     v_up = min(base + rate * phase_time, cap)
GETUP_SPEED_RATE = 694.4444        # cm/s^2
GETUP_SPEED_CAP = 2222.2222        # cm/s
RECOVER_SLERP = 1.0 / 12.0         # orientation slerp per frame in WipeOutRecover
GETUP_PUSH = 416.6667              # cm/s added after the start-line get-up
TRICKY_BOOST_CAP = 0.66599226      # boost meter cap when Tricky is lost

# Slide phase surface terms: gravity = surface record field 0, snap offset =
# smoothed surface field 7.  Defaults are surface 1 (ordinary snow) of the exe surface table;
# callers pass the real surface via Slope.gravity / Slope.p7.
SURFACE1_GRAVITY = 1300.85
SURFACE1_P7 = 0.5

# Rigid tumble body.  Point masses sit at the 18 rider-shape sphere centres.
SPHERE_MASSES = (4.0, 2.0, 2.0, 2.0, 2.0, 2.0, 2.0, 2.0, 4.0, 4.0, 2.0, 2.0, 2.0, 2.0, 3.0, 3.0, 3.0, 3.0)
INERTIA_SCALE = 1.2                # tensor scaled before inversion ...
INV_INERTIA_SCALE = 0.8            # ... and the inverse scaled after it
MAX_SPIN_RATE = 23.561945          # rad/s; |w| above this scales w and L down to it
TUMBLE_CONTACT_DAMPING = 0.95      # momentum and angular momentum after each tumble contact
TUMBLE_CONTACT_MIN_SPEED = 100.0   # cm/s; slower contacts get restitution 0 and friction 0
SELF_RIGHT_GAIN = 250951.34        # air self-righting torque gain (times m, dt)
SELF_RIGHT_YAW_GAIN = 125475.67
INPUT_TURN_GAIN = 75285.4          # player air_turn torque gain (times m, dt)
INPUT_TILT_GAIN = 125475.67        # player air_tilt torque gain (times m, dt)
SETTLE_ROLL_TORQUE = 400000.0      # roll torque that tips a sideways rider onto back/front

# ASSUMPTION placeholder (point-mass reduction only; the rigid contact is specified in
# and tumble_contact_coefficients())
TUMBLE_GROUND_RESTITUTION = 0.15

# Get-up animation lengths: .afl frame counts per board class
# (0 bx, 1 fr, 2 ex animation set; None = no such animation), 30 fps, 0.5 animation frames per game
# frame. WipeOutRecover lasts until the get-up is done and the riding loop starts: 2 (n - 1) game
# frames, plus the usual one frame before the control sees the loop. Checked on 19 live get-ups.
ANIM_FPS = 30.0
GETUP_ANIM_FRAMES = {
    0x24A: (51, 57, 56),          # on back, look-ahead along up      (ROLLBWD2BASE)
    0x24B: (35, 35, 30),          # on back, against up                (GU_FROMBUTTFWD)
    0x24C: (35, 35, 33),          # on back, otherwise / side          (GU_FROMBUTTRIGHT)
    0x24D: (41, 31, 33),          # face down, against up              (GU_FROMFACEBWD)
    0x24E: (30, 35, 33),          # face down, along up                (GU_FROMFACEFWD)
    0x24F: (35, 35, 33),          # face down, otherwise / side        (GU_FROMFACERIGHT)
    0x250: (21, 21, 10),          # quick recovery (crunch)            (CT_CRUNCH2BASE)
    0x251: (21, 21, 10),          # quick recovery (stretch)           (CT_STRETCH2BASE)
    0x252: (None, None, 33),      # board class 2, on back, side
    0x253: (None, None, 33),      # board class 2, face down, side
}
DEFAULT_GETUP_ANIM = 0x24C        # most common "otherwise" case


def getup_seconds(anim_id: int = DEFAULT_GETUP_ANIM, board_class: int = 0) -> float:
    """WipeOutRecover duration for a get-up animation (done + one frame of control latency)."""
    n = GETUP_ANIM_FRAMES[anim_id][board_class]
    if n is None:
        n = GETUP_ANIM_FRAMES[DEFAULT_GETUP_ANIM][board_class]
    return (n - 1) / ANIM_FPS + DT


GETUP_ANIM_SECONDS = getup_seconds()   # bx "otherwise" get-up, 69 frames = 1.15 s


def getup_speed_limit(phase_time: float) -> float:
    """Speed below which a lying rider may start the get-up."""
    return min(GETUP_SPEED_BASE + GETUP_SPEED_RATE * phase_time, GETUP_SPEED_CAP)


def start_wipeout_boost(boost_meter: float, tricky_timer: float, trick_pending: bool,
                        infinite_boost: bool):
    """Boost side effect of StartWipeOut: returns (boost_meter, tricky_timer)."""
    if trick_pending and tricky_timer > 0.0 and not infinite_boost:
        return min(boost_meter, TRICKY_BOOST_CAP), 0.0
    return boost_meter, tricky_timer


def mass_properties(centres):
    """Tumble body from the posed rider shape.

    centres: 18 sphere centres (world, cm).  Returns (inv_mass, centre, inv_inertia 3x3).
    Point masses, sphere radii ignored; I is about the mass centre, scaled by 1.2, inverted,
    and the inverse scaled by 0.8.  Recomputed every tumble frame from the current pose.
    """
    c = np.asarray(centres, float)
    m = np.asarray(SPHERE_MASSES[:len(c)], float)
    total = float(m.sum())
    centre = (m[:, None] * c).sum(axis=0) / total
    inertia = np.zeros((3, 3))
    for mi, ri in zip(m, c - centre):
        inertia += mi * (np.dot(ri, ri) * np.eye(3) - np.outer(ri, ri))
    inv_inertia = np.linalg.inv(INERTIA_SCALE * inertia) * INV_INERTIA_SCALE
    return 1.0 / total, centre, inv_inertia


def rigid_impulse(v, w, r, n, inv_mass, inv_inertia, restitution, friction=0.0):
    """Contact impulse at offset r from the mass centre along unit normal n.

    Returns the linear and angular momentum changes (dP, dL).  Friction is a tangential impulse
    proportional to the tangential contact speed (friction * |v_t| / k_t, opposing v_t), applied
    first; the normal impulse -(1 + e) v_n / k_n follows unless the contact already separates.
    """
    v, w, r, n = (np.asarray(a, float) for a in (v, w, r, n))
    dP, dL = np.zeros(3), np.zeros(3)

    def k(d):
        return inv_mass + float(np.dot(d, np.cross(inv_inertia @ np.cross(r, d), r)))

    vn = float(np.dot(v + np.cross(w, r), n))
    if friction != 0.0:
        vc = v + np.cross(w, r)
        vt = vc - vn * n
        speed_t = float(np.linalg.norm(vt))
        if speed_t > 0.0:
            t = vt / speed_t
            J = t * (-friction * speed_t / k(t))
            dP += J
            dL += np.cross(r, J)
            v2, w2 = v + inv_mass * dP, w + inv_inertia @ dL
            vn = float(np.dot(v2 + np.cross(w2, r), n))
            if vn > 0.0:
                return dP, dL
    J = n * (-(1.0 + restitution) * vn / k(n))
    dP += J
    dL += np.cross(r, J)
    return dP, dL


def tumble_contact_coefficients(normal_z: float, speed: float):
    """Tumble contact (restitution, friction) from the contact normal's z and |v|."""
    if speed < TUMBLE_CONTACT_MIN_SPEED:
        return 0.0, 0.0
    return 0.8 - 0.7 * abs(normal_z), 0.15 * abs(normal_z)


def entry_velocity(velocity, impact_normal, w=None, r=None, inv_mass=None, inv_inertia=None):
    """Entry impulse.

    impact_normal is the normal handed to StartWipeOut (the obstacle's outward surface normal in
    every caller found).  The code negates it and applies the 0.15-restitution impulse only if the
    contact-point velocity has a negative component along the negated normal, i.e. when the rider
    already moves away from the obstacle.  That is false at a landing bail, an air wall hit and a
    bail surface, true after a grounded wall bounce (bounce wipe-out).  Without body data the
    point-mass form is used (r = 0; angular velocity is 0 at entry).
    """
    n = -np.asarray(impact_normal, float)
    n = n / np.linalg.norm(n)
    v = np.asarray(velocity, float)
    w = np.zeros(3) if w is None else np.asarray(w, float)
    r = np.zeros(3) if r is None else np.asarray(r, float)
    if float(np.dot(v + np.cross(w, r), n)) >= 0.0:
        return v
    if inv_mass is None:
        return v - (1.0 + IMPACT_RESTITUTION) * float(np.dot(v, n)) * n
    dP, _ = rigid_impulse(v, w, r, n, inv_mass, inv_inertia, IMPACT_RESTITUTION)
    return v + inv_mass * dP


def choose_getup_anim(lying_anim: int, d_up: float, d_side: float, board_class: int) -> int:
    """Get-up animation at BeginRecover.

    lying_anim: 0x2DC (on the back) or 0x2DD (face down).  d_up / d_side: unit direction to the
    path look-ahead dotted with the body up / side axes, measured before the re-orientation.
    Only the positive side direction counts as "along side".
    """
    c = 0.70710677
    face_down = lying_anim == 0x2DD
    if d_up > c:
        return 0x24E if face_down else 0x24A
    if d_side > c:
        if board_class == 2:
            return 0x253 if face_down else 0x252
        return 0x24F if face_down else 0x24C
    if d_up < -c:
        return 0x24D if face_down else 0x24B
    return 0x24F if face_down else 0x24C


def tumble_anim(up_n: float, side_n: float, fwd_n: float, state11: bool) -> int:
    """Tumble animation from the body axes dotted with the ground normal.

    The first id of each pair is used while the channel-0 animation category is 0x11, the second otherwise.
    Checked on the 5 live entries .
    """
    if up_n < -0.7:
        pair = (0x2E4, 0x2EB)
    elif up_n > 0.7:
        pair = (0x2E3, 0x2EA)
    elif side_n > 0.7:
        pair = (0x2E5, 0x2EC)
    elif side_n < -0.7:
        pair = (0x2E6, 0x2ED)
    elif fwd_n > 0.0:
        pair = (0x2E1, 0x2E8)
    else:
        pair = (0x2E2, 0x2E9)
    return pair[0] if state11 else pair[1]


TUMBLE_CONTACT_MASK = sum(1 << i for i in (0, 1, 6, 7, 8, 9, 12, 13, 14, 17))   # spheres 0, 1, 6-9, 12-14, 17
SECOND_PASS_FILTER_DOT = 0.8       # second query ignores contacts whose normal is within this of the first
INSTANCE_RESTITUTION = 0.5         # flag-0x80 instances: e = 0.5, mu = 0
NEIGHBOUR_RESTITUTION = 0.5        # tumble body vs a type-0 rigid moving object: J = -1.5 v_rel / k_sum
AIR_GRACE = 0.1                    # s of kind-3 updates (no closing contact) that still count as grounded


def quat_mul(a, b):
    w1, x1, y1, z1 = a
    w2, x2, y2, z2 = b
    return np.array([w1 * w2 - x1 * x2 - y1 * y2 - z1 * z2, w1 * x2 + x1 * w2 + y1 * z2 - z1 * y2,
                     w1 * y2 - x1 * z2 + y1 * w2 + z1 * x2, w1 * z2 + x1 * y2 - y1 * x2 + z1 * w2])


def quat_rotate(q, v):
    """v' = v + 2w (u x v) + 2 u x (u x v), q = (w, u)."""
    u = np.asarray(q[1:], float)
    t = np.cross(u, v)
    return np.asarray(v, float) + 2.0 * q[0] * t + 2.0 * np.cross(u, t)


def quat_rotate_inv(q, v):
    return quat_rotate(np.array([q[0], -q[1], -q[2], -q[3]]), v)


@dataclass
class Contact:
    """One world-query result: depth t (> 0 = penetrating), point c, outward normal m, hit kind, surface."""
    depth: float
    point: np.ndarray
    normal: np.ndarray
    kind: int = 0
    surface: int = 1


class TumbleBody:
    """WipeOut rigid body (motion data +0x70..+0xFC): x = mass centre, q = orientation (w, x, y, z), P and L
    momenta, v = P/M, w = I^-1 L.  The inertia is world-frame and rebuilt every frame from the posed spheres."""

    def __init__(self, sphere_centres, rider_q, rider_v):
        inv_m, c, inv_i = mass_properties(sphere_centres)
        self.inv_mass, self.inv_inertia = inv_m, inv_i
        self.x = c.copy()
        self.q = np.asarray(rider_q, float).copy()
        self.P = np.asarray(rider_v, float) / inv_m
        self.L = np.zeros(3)
        self.refresh()

    @property
    def mass(self):
        return 1.0 / self.inv_mass

    def refresh(self):
        """Refresh: v = P/M, w = I^-1 L; |w| clamped to 23.5619 rad/s (L scaled with it)."""
        self.v = self.P * self.inv_mass
        self.w = self.inv_inertia @ self.L
        s = float(np.linalg.norm(self.w))
        if s > MAX_SPIN_RATE:
            self.w *= MAX_SPIN_RATE / s
            self.L *= MAX_SPIN_RATE / s

    def refresh_nocap(self):
        """v = P/M, w = I^-1 L without the spin cap (@154338 tail, @10E190 after x0.95)."""
        self.v = self.P * self.inv_mass
        self.w = self.inv_inertia @ self.L

    def apply_impulse(self, point, J):
        """Impulse at a point (@154338): P += J, L += r x J, refresh WITHOUT the spin cap."""
        self.P = self.P + J
        self.L = self.L + np.cross(np.asarray(point) - self.x, J)
        self.refresh_nocap()

    def impulse(self, point, normal, e, mu):
        """Contact impulse (restitution e, friction mu) with the body's own mass properties."""
        r = np.asarray(point, float) - self.x
        dP, _ = rigid_impulse(self.v, self.w, r, normal, self.inv_mass, self.inv_inertia, e, mu)
        if np.any(dP):
            self.apply_impulse(point, dP)

    def integrate(self, F, tau, dt):
        """Integrate, then refresh: x += v dt (old v); q += 0.5 dt (w (x) q) from the old q, normalised;
        P += F dt; L += tau dt."""
        self.x = self.x + self.v * dt
        dq = quat_mul(np.array([0.0, *self.w]), self.q)
        self.q = self.q + 0.5 * dt * dq
        nq = float(np.linalg.norm(self.q))
        self.q = self.q / nq if nq > 0.0 else np.array([1.0, 0, 0, 0])
        self.P = self.P + F * dt
        self.L = self.L + tau * dt
        self.refresh()


def tumble_frame(body, pose_spheres_world, rider_p, rider_q, ground_n, path_point, air_time, dt, query):
    """One tumble update (motion update -> forces/integration -> contact pass)

    pose_spheres_world: the 18 sphere centres as rebuilt from the skeleton at the END of the previous frame (world,
        posed at rider_p / rider_q). The caller has already lifted the SPHERES (not rider_p) by
        lift = 0.5 (p7 + 10) max(n_z, 0) along n: 0x1267A0 moves the set +0x470 and its bound, never +0x140, so the
        pose offset below runs from the unlifted p to the lifted centroid and the returned rider_p is not lowered
        (REP0001 crash trace).
    query(spheres, filter_normal) -> Contact or None: sphere-set query (TUMBLE_CONTACT_MASK) with the spheres exactly as
        given; the code does not move them to the new body pose before querying. filter_normal (second pass) makes
        the query skip contacts whose normal is within dot > 0.8 of it.
    Instances and neighbour objects (third pass, neighbour loop) are left to the caller.
    Returns (rider_p, rider_q, rider_v, record or None); record = the last closing contact (kind 0/1/2).
    """
    spheres = np.array(pose_spheres_world, float)
    #: mass properties from the current pose; the centroid is returned, NOT written into body.x
    body.inv_mass, centroid, body.inv_inertia = mass_properties(spheres)
    body.refresh()
    d_local = quat_rotate_inv(rider_q, centroid - np.asarray(rider_p, float))
    M = body.mass
    F = np.array([0.0, 0.0, -TUMBLE_GRAVITY * M]) - TUMBLE_LINEAR_DRAG * body.P
    tau = -TUMBLE_ANGULAR_DRAG * body.L
    if air_time < HOMING_WINDOW:
        d = np.asarray(path_point, float) - centroid
        d = d - float(np.dot(d, ground_n)) * np.asarray(ground_n, float)
        nd = float(np.linalg.norm(d))
        if nd > 0.0:
            F = F + (HOMING_ACCEL * M) * d / nd
    body.integrate(F, tau, dt)
    record = None
    first_normal = None
    for pass_no in (1, 2):
        hit = query(spheres, first_normal if pass_no == 2 else None)
        if hit is None or hit.depth <= 0.0:
            break
        m = np.asarray(hit.normal, float)
        if pass_no == 2 and float(np.dot(m, first_normal)) < 0.0:
            m = m - float(np.dot(m, first_normal)) * first_normal
            m = m / np.linalg.norm(m)
        push = hit.depth * m
        body.x = body.x + push
        spheres = spheres + push
        r = np.asarray(hit.point, float) - body.x
        if float(np.dot(body.v + np.cross(body.w, r), m)) < 0.0:
            e, mu = tumble_contact_coefficients(m[2], float(np.linalg.norm(body.v)))
            body.impulse(hit.point, m, e, mu)
            body.P = body.P * TUMBLE_CONTACT_DAMPING
            body.L = body.L * TUMBLE_CONTACT_DAMPING
            body.refresh_nocap()
            record = Contact(hit.depth, np.asarray(hit.point, float), m, hit.kind, hit.surface)
        if pass_no == 1:
            first_normal = m
    # pivot: the rider root keeps its body-frame offset from this frame's pose centroid
    new_q = body.q.copy()
    new_p = body.x - quat_rotate(new_q, d_local)
    return new_p, new_q, body.v.copy(), record


def tumble_bookkeeping(phase_time, air_time, closing_contact, ground_check_hit, dt):
    """phase_time / air_time after one tumble update

    closing_contact: a closing contact happened in the contact pass this update (record kind 0/1/2).
    Returns (phase_time, air_time, counts_as_grounded).  The airborne branch tests air_time before adding dt, so up
    to 7 consecutive updates without a closing contact still count as grounded while the ground check hits.
    (The self-righting / air-recovery branch for air_time > 0.1 is separate:)
    """
    if closing_contact:
        return phase_time + dt, 0.0, True
    if air_time > AIR_GRACE or not ground_check_hit:
        return 0.0, air_time + dt, False
    return phase_time + dt, air_time + dt, True


def settle_decision(phase_time, ground_check_hit, surface, n_z, fwd_n):
    """Slide entry test: 0x2DC / 0x2DD, 'roll' (+-400000 M dt about forward x n), or None."""
    if not ground_check_hit or phase_time <= SETTLE_PHASE_TIME or surface in (6, 10):
        return None
    if n_z <= STEEP_NZ:
        return None
    if fwd_n > SLIDE_FACE_DOT:
        return 0x2DC
    if fwd_n < -SLIDE_FACE_DOT:
        return 0x2DD
    return "roll"


@dataclass
class Slope:
    """Planar test terrain: falls along +x at 'angle', race line along y = 0."""
    angle: float = math.radians(20.0)
    gravity: float = SURFACE1_GRAVITY      # surface record field 0 under the rider
    p7: float = SURFACE1_P7                # smoothed surface field 7 under the rider

    @property
    def normal(self):
        return np.array([math.sin(self.angle), 0.0, math.cos(self.angle)])

    def height(self, p):
        return float(np.dot(p, self.normal))       # signed distance to the plane through 0


@dataclass
class WipeoutState:
    p: np.ndarray
    v: np.ndarray
    spin: float                                     # |w|, rad/s
    lying_angle: float = math.radians(45.0)         # angle between forward and +-n in the slide
    phase: str = "tumble"                           # tumble / slide / recover / done / reset
    total_time: float = 0.0
    phase_time: float = 0.0
    air_time: float = 0.0
    events: List[tuple] = field(default_factory=list)


def tumble_step(s: WipeoutState, slope: Slope):
    """One tumble frame, point-mass reduction."""
    n = slope.normal
    a = np.array([0.0, 0.0, -TUMBLE_GRAVITY]) - TUMBLE_LINEAR_DRAG * s.v
    if s.air_time < HOMING_WINDOW and abs(s.p[1]) > 1e-6:
        to_line = np.array([0.0, -s.p[1], 0.0])
        to_line -= np.dot(to_line, n) * n
        a += HOMING_ACCEL * to_line / max(np.linalg.norm(to_line), 1e-9)
    s.v = s.v + a * DT
    s.p = s.p + s.v * DT
    s.spin *= (1.0 - TUMBLE_ANGULAR_DRAG * DT) * (1.0 - INPUT_SPIN_DAMPING * DT)
    h = slope.height(s.p)
    grounded = h <= 0.0
    if grounded:
        s.p = s.p - h * n
        vn = np.dot(s.v, n)
        if vn < 0.0:                                 # ASSUMPTION: contact solver bounce
            s.v = s.v - (1.0 + TUMBLE_GROUND_RESTITUTION) * vn * n
        s.air_time = 0.0
        s.phase_time += DT
    else:
        # air_time is tested BEFORE it grows; inside the 0.1 s grace the update still
        # counts as grounded (phase_time += dt) when the +-400 cm ground check hits (assumed here)
        if s.air_time > AIR_GRACE:
            s.phase_time = 0.0
        else:
            s.phase_time += DT
        s.air_time += DT
    return grounded


def slide_step(s: WipeoutState, slope: Slope):
    """One slide frame."""
    n = slope.normal
    g0 = getattr(slope, "gravity", SURFACE1_GRAVITY)       # surface field 0 (not 981)
    p7 = getattr(slope, "p7", SURFACE1_P7)                 # smoothed surface field 7
    g_t = g0 * (n[2] * n - np.array([0.0, 0.0, 1.0]))
    s.p = s.p + s.v * DT                                   # fixed 1/60 s here, not time_scale
    s.v = (s.v + g_t * DT) * SLIDE_DAMPING_PER_FRAME
    e = max(-SNAP_LIMIT, min(SNAP_LIMIT, slope.height(s.p) + p7))
    s.p = s.p - e * n
    s.v = s.v - np.dot(s.v, n) * n
    s.lying_angle -= math.asin(min(math.sin(s.lying_angle), 0.99999)) * SLIDE_ALIGN_FRACTION
    s.phase_time += DT


def simulate(v0, impact_normal, slope=Slope(), spin0=9.0, racing=True, max_time=12.0):
    """Wall/obstacle wipe-out timeline on a plane.  Returns the WipeoutState with .events."""
    s = WipeoutState(p=np.array([0.0, 120.0, 5.0]), v=entry_velocity(v0, impact_normal), spin=spin0)
    s.events.append((0.0, "start", float(np.linalg.norm(s.v))))
    recover_left = GETUP_ANIM_SECONDS
    while s.total_time < max_time and s.phase not in ("done", "reset"):
        s.total_time += DT
        speed = float(np.linalg.norm(s.v))
        if s.phase == "tumble":
            grounded = tumble_step(s, slope)
            if grounded and s.phase_time > SETTLE_PHASE_TIME and slope.normal[2] > STEEP_NZ:
                s.phase = "slide"            # phase_time is NOT reset here (the get-up speed uses it)
                s.events.append((s.total_time, "slide", speed))
            elif s.phase_time >= TUMBLE_PHASE_TIMEOUT or s.total_time > TUMBLE_TOTAL_TIMEOUT:
                s.phase = "reset" if racing else "recover"
                s.events.append((s.total_time, "timeout -> " + s.phase, speed))
        elif s.phase == "slide":
            slide_step(s, slope)
            aligned = math.sin(s.lying_angle) < SLIDE_ALIGNED_SIN
            if aligned and speed < getup_speed_limit(s.phase_time):
                s.phase, s.phase_time = "recover", 0.0
                s.events.append((s.total_time, "get-up starts", speed))
        elif s.phase == "recover":
            slide_step(s, slope)                     # recover motion slides the same way
            recover_left -= DT
            if recover_left <= 0.0:
                s.phase = "done"
                s.events.append((s.total_time, "riding loop -> Ground + Cruise", float(np.linalg.norm(s.v))))
    return s


def plane_query(normal, offset=0.0, radius=20.0):
    """Test world for tumble_frame: a plane n.x = offset; every sphere has `radius`; deepest sphere wins."""
    n = np.asarray(normal, float) / np.linalg.norm(normal)

    def query(spheres, filter_normal):
        if filter_normal is not None and float(np.dot(n, filter_normal)) > SECOND_PASS_FILTER_DOT:
            return None
        depth = radius - (spheres @ n - offset)
        i = int(np.argmax(depth))
        if depth[i] <= 0.0:
            return None
        return Contact(float(depth[i]), spheres[i] - (float(spheres[i] @ n) - offset) * n, n)
    return query


def rigid_demo():
    """Drop a stick-figure sphere set at 2000 cm/s onto a 30 deg slope and report spin and speed per contact."""
    rng = np.random.default_rng(1)
    pose = rng.normal(0.0, 25.0, (18, 3)) + np.array([0.0, 0.0, 60.0])
    q = np.array([1.0, 0.0, 0.0, 0.0])
    p = np.zeros(3)
    body = TumbleBody(pose, q, np.array([1800.0, 0.0, -900.0]))
    n = np.array([math.sin(math.radians(30)), 0.0, math.cos(math.radians(30))])
    query = plane_query(n, offset=-5.0)
    local = pose - p
    out = []
    for frame in range(40):
        spheres = p + np.array([quat_rotate(q, c) for c in local])
        p, q, v, rec = tumble_frame(body, spheres, p, q, n, p + np.array([5000.0, 0, 0]), 1.0, DT, query)
        if rec is not None:
            out.append((frame, float(np.linalg.norm(v)), float(np.linalg.norm(body.w))))
    return out


ANIM_CHAIN = {0x2E0: 0x2DE, **{i: 0x2DF for i in range(0x2E1, 0x2EE)}}     # one-shot impacts: on-done 5 / 6
CATEGORY_17 = {0x2DF, *range(0x2E0, 0x2E7)}                                  # airborne tumbling (first of each pair)


def anim_current(anim_id, play_update, n_frames, update):
    """@15FDD0 as the wipe-out code sees it: an impact one-shot of n frames played at update u0 reads as its chain
    target from update u0 + 2 (n - 1) + 1 on (live: 0x2E9 n 30 -> 0x2DF after 59 updates, 0x2E0 n 14 -> 0x2DE after 27)."""
    nxt = ANIM_CHAIN.get(anim_id)
    if nxt is not None and n_frames > 0 and update - play_update >= 2 * (n_frames - 1) + 1:
        return nxt
    return anim_id


def tumble_rules(st, rec_kind, rec_normal, rec_surface, check_hit, n, basis, w_norm, anim, dt, pred, stick,
                 racing, lookahead_dot_fwd, probe_ok):
    """The @10C2F0 tumble branch after the contact pass, in the original order.

    st: dict with air_time, phase_time, total_time (single precision in the game), surface; mutated.
    rec_kind: contact record kind (3 = no closing contact), rec_normal / rec_surface its normal / surface.
    n: ground normal after the ground check (Z on a miss; numpy array, replaced in place by the record normal);
    basis = (side, forward, up); w_norm() = |w| now; anim = channel-0 id read at the start of the update;
    pred = dict(time_left, surface, up_dot_land) of the +0x57B0 predictor after its tick / refresh; stick =
    (air_turn, air_tilt); lookahead_dot_fwd = unit(+0x360 - p) . forward (NOT projected); probe_ok() = the full
    ground probe @128AE8 (called only when the other quick-recovery tests pass).
    Returns the actions in order.
    """
    side, fwd, up = basis
    out = []
    damp = True
    if rec_kind == 3:
        air = st['air_time']
        if air > AIR_GRACE:
            if anim == 0x2DF:
                out += [('anim', 0x2E0), ('predictor', 'refresh')]
            else:
                out.append(('predictor', 'tick'))
            if pred['time_left'] > 0.1 and pred['surface'] not in (6, 10):
                if abs(stick[0]) < 0.5 and abs(stick[1]) < 0.5:
                    out.append(('self_right',))   # L = (1 - 3.3475 dt) L + dL_up + dL_yaw; w = I^-1 L (no cap)
                    damp = False
                    if pred['up_dot_land'] > AIR_RIGHT_UP and w_norm() < AIR_RIGHT_SPIN:
                        out += [('ramp_1F0', 0.0), ('anim', 0x224), ('T61',)]
                        return out
            st['phase_time'] = 0.0
        elif not check_hit:
            st['phase_time'] = 0.0
        else:
            out.append(('contact_kind', 0))
            st['phase_time'] += dt
        st['air_time'] = air + dt
    else:
        if not check_hit or n[2] < rec_normal[2]:
            n[:] = rec_normal
        st['surface'] = rec_surface if rec_kind in (0, 2) else 10
        if (st['phase_time'] == 0.0 and anim == 0x2DF) or anim in (0x2DE, 0x2E0):
            out += [('tumble_anim',), ('predictor', 'clear')]
        st['air_time'] = 0.0
        st['phase_time'] += dt
    cur = anim
    if check_hit and st['phase_time'] > SETTLE_PHASE_TIME and st['surface'] not in (6, 10):
        c = float(np.dot(fwd, n))
        out.append(('slide_sound',))
        if n[2] > STEEP_NZ:
            if c > SLIDE_FACE_DOT or c < -SLIDE_FACE_DOT:
                cur = 0x2DC if c > SLIDE_FACE_DOT else 0x2DD
                out.append(('anim', cur))             # no early exit: quick recovery and input still run
            else:
                out.append(('roll', -1.0 if c < 0.0 else 1.0))   # L += dt (+-400000) M (forward x n), w = I^-1 L
    if (st['total_time'] > QUICK_RECOVER_TOTAL and st['phase_time'] > QUICK_RECOVER_PHASE
            and st['surface'] not in (6, 10) and w_norm() < QUICK_RECOVER_SPIN and n[2] > STEEP_NZ
            and abs(lookahead_dot_fwd) > 0.8 and float(np.dot(up, n)) > QUICK_RECOVER_UP and probe_ok()):
        out.append(('T62', 0x250 if cur in CATEGORY_17 else 0x251))
        return out
    out.append(('damp_input', damp))                  # L = (1 - 3.3475 dt) L + dL if damp else L + dL; w = I^-1 L
    if st['phase_time'] >= TUMBLE_PHASE_TIMEOUT or st['total_time'] > TUMBLE_TOTAL_TIMEOUT:
        if racing:
            out.append(('stuck_1e6',))
        else:
            out.append(('begin_recover', 0x2DC if float(np.dot(fwd, n)) > 0.0 else 0x2DD))
    return out


def slide_update(p, v, n, h, p7, g0, ground_check, dt_timer, phase_time):
    """One @10C2F0 slide update before the alignment: fixed 1/60 s steps, the snap from the STORED h, then the ground
    check (@10FD58). ground_check(p) -> (hit, n, surface, h). Returns (p, v, n, h, surface, phase_time, fall)."""
    phase_time += dt_timer
    g = n[2] * g0
    p = p + v * (1.0 / 60.0)
    v = (v + np.array([n[0] * g, n[1] * g, g - g0]) * (1.0 / 60.0)) * SLIDE_DAMPING_PER_FRAME
    e = max(-SNAP_LIMIT, min(SNAP_LIMIT, h + p7))
    p = p - e * n
    h = h - e
    v = v - float(np.dot(v, n)) * n
    hit, n2, surface, h2 = ground_check(p)
    fall = (not hit) or n2[2] < 0.3 or surface in (6, 10)
    return p, v, n2, (h2 if hit else h), surface, phase_time, fall


def begin_recover(face_down, d_up, d_side, board_class, profile_stance):
    """@10F178: (rotations, anim, goofy, anim_yaw, mirror). Rotations in order: ('side', a) then ('up', a) about the
    CURRENT basis axes (q = r (x) q). d_up / d_side: unit(+0x360 - p) against up / side before any rotation."""
    rot = [('side', math.pi / 2 if face_down else -math.pi / 2)]
    regular = profile_stance == 0 or board_class != 2
    goofy = yaw = mirror = None
    c = 0.70710677
    if d_up > c:
        anim = 0x24E if face_down else 0x24A
        if not face_down:
            rot.append(('up', math.pi))
    elif d_side > c:
        rot.append(('up', -math.pi / 2 if regular else math.pi / 2))
        goofy, yaw = (True, -math.pi) if regular else (False, 0.0)
        if board_class == 2:
            anim = 0x253 if face_down else 0x252
        else:
            mirror, anim = True, (0x24F if face_down else 0x24C)
    elif d_up < -c:
        anim = 0x24D if face_down else 0x24B
        if face_down:
            rot.append(('up', math.pi))
    else:
        rot.append(('up', math.pi / 2 if regular else -math.pi / 2))
        anim = 0x24F if face_down else 0x24C
    return rot, anim, goofy, yaw, mirror


def arc(a, b):
    """@2508B8: shortest-arc quaternion a -> b with |a x b| clamped at 0.99999 (pi - asin past 90 deg); None if a x b = 0."""
    ax = np.cross(a, b)
    s = float(np.linalg.norm(ax))
    if s == 0.0:
        return None
    ang = math.asin(min(s, 0.99999))
    if float(np.dot(a, b)) < 0.0:
        ang = math.pi - ang
    return np.array([math.cos(ang / 2), *(ax / s * math.sin(ang / 2))])


def recover_target(n, d):
    """@1102C8 target: arc(Y' -> d) (x) arc(Z -> n), Y' = arc(Z -> n) Y; d = unit(look-ahead - p) in the n plane."""
    q = np.array([1.0, 0.0, 0.0, 0.0])
    r = arc(np.array([0.0, 0.0, 1.0]), np.asarray(n, float))
    if r is not None:
        q = quat_mul(r, q)
    r = arc(quat_rotate(q, np.array([0.0, 1.0, 0.0])), np.asarray(d, float))
    if r is not None:
        q = quat_mul(r, q)
    return q


def slerp_game(qa, qb, t=RECOVER_SLERP):
    """@250A48: shortest path, acos via pi/2 - asin, linear weights when 1 - dot <= 0.001, no normalisation."""
    dot = float(np.dot(qa, qb))
    if dot < 0.0:
        qb, dot = -qb, -dot
    if 1.0 - dot > 0.001:
        om = math.pi / 2 - math.asin(dot)
        so = math.sin(om)
        wa, wb = math.sin((1.0 - t) * om) / so, math.sin(t * om) / so
    else:
        wa, wb = 1.0 - t, t
    return wa * qa + wb * qb


def reset_wait(t):
    """@106A90 after the timer step (t += 1/60, single precision): (render_alpha, reset_fade or None, respawn)."""
    alpha = min(1.0, max(0.0, (0.5 - t) * 2.0))
    fade = min(1.0, max(0.0, 1.0 - (0.5 - t) / 0.3)) if t > 0.2 else None
    return alpha, fade, t > 0.8


def respawn_line_pick(q, current, candidates):
    """@118C08 line choice around Q (the point `advance` further along the current line): candidates = up to 6
    (line_id, t, p_t, p_t800, length); lines with t > length - 200 are skipped; score |Q - p_t|^2 + |Q - p_t800|^2;
    the smallest wins, the current line is kept when it wins. Returns the line id."""
    best, pick = 1e37, None
    for line, t, pt, pt800, length in candidates:
        if length - 200.0 < t:
            continue
        sc = float(np.dot(q - pt800, q - pt800) + np.dot(q - pt, q - pt))
        if sc < best:
            best, pick = sc, (None if line == current else line)
    return current if pick is None else pick


if __name__ == "__main__":
    for frame, speed, spin in rigid_demo()[:6]:
        print("rigid demo contact at update %2d: |v| %6.0f cm/s, |w| %5.2f rad/s" % (frame, speed, spin))
    print("get-up speed limit: t=0 %.0f, t=1 %.0f, t=3 %.0f cm/s"
          % (getup_speed_limit(0), getup_speed_limit(1), getup_speed_limit(3)))
    print("Tricky lost on crash:", start_wipeout_boost(0.95, 12.0, True, False))
    # rider at 2500 cm/s (90 km/h) hits a wall facing back up the slope, 30 deg off head-on
    wall_n = np.array([-math.cos(math.radians(30)), math.sin(math.radians(30)), 0.0])
    st = simulate(np.array([2500.0, 0.0, -300.0]), wall_n)
    print("wipe-out timeline (head-on-ish wall hit at 2500 cm/s on a 20 deg slope)")
    for t, what, speed in st.events:
        print("  t=%5.2f s  %-34s speed %7.1f cm/s" % (t, what, speed))
    st2 = simulate(np.array([1200.0, 0.0, 0.0]), np.array([-1.0, 0.0, 0.0]), Slope(math.radians(5)))
    print("timeline 2 (1200 cm/s into a wall on a 5 deg slope)")
    for t, what, speed in st2.events:
        print("  t=%5.2f s  %-34s speed %7.1f cm/s" % (t, what, speed))
