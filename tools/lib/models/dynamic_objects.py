"""Dynamic / breakable props.

  * cRollerNode: rigid-body props (cones, crash bags, bins, signs ...);
  * the moving-object list = sweep-and-prune pair manager (description only);
  * cMeshAnimNode: break-apart debris;
  * cCrackedNode: health-based breakables (glass).
Clean-room behaviour model; constants are as compiled.
"""
from __future__ import annotations

import math
from dataclasses import dataclass, field
from typing import Callable, List, Optional, Sequence, Tuple

FRAME = 1.0 / 60.0
GRAVITY = 980.0                       # cm/s^2, -z

Vec = Tuple[float, float, float]


def _add(a, b): return (a[0] + b[0], a[1] + b[1], a[2] + b[2])
def _sub(a, b): return (a[0] - b[0], a[1] - b[1], a[2] - b[2])
def _mul(a, s): return (a[0] * s, a[1] * s, a[2] * s)
def _dot(a, b): return a[0] * b[0] + a[1] * b[1] + a[2] * b[2]
def _cross(a, b): return (a[1] * b[2] - a[2] * b[1], a[2] * b[0] - a[0] * b[2], a[0] * b[1] - a[1] * b[0])
def _len(a): return math.sqrt(_dot(a, a))


def _norm(a):
    n = _len(a)
    return _mul(a, 1.0 / n) if n else a


def _mat_vec(m, v):                   # m: 3 rows; v as column
    return tuple(_dot(r, v) for r in m)


def _mat_mul(a, b):
    bt = list(zip(*b))
    return [tuple(_dot(r, c) for c in bt) for r in a]


def _transpose(m):
    return [tuple(r) for r in zip(*m)]


def _inv3(m):
    (a, b, c), (d, e, f), (g, h, i) = m
    A, B, C = e * i - f * h, -(d * i - f * g), d * h - e * g
    det = a * A + b * B + c * C
    inv = [(A, -(b * i - c * h), b * f - c * e),
           (B, a * i - c * g, -(a * f - c * d)),
           (C, -(a * h - b * g), a * e - b * d)]
    return [tuple(x / det for x in r) for r in inv]


def quat_to_matrix(q):
    """q = (w, x, y, z) -> rotation rows."""
    w, x, y, z = q
    return [(1 - 2 * (y * y + z * z), 2 * (x * y + w * z), 2 * (x * z - w * y)),
            (2 * (x * y - w * z), 1 - 2 * (x * x + z * z), 2 * (y * z + w * x)),
            (2 * (x * z + w * y), 2 * (y * z - w * x), 1 - 2 * (x * x + y * y))]


# cRollerNode
INERTIA_SCALE = 0.075                 # body inverse inertia = sphere-tree inverse matrix x 0.075
OMEGA_MAX = 23.561945                 # rad/s (7.5 pi): |w| clamp, L scaled with it
REST_ENERGY = 10000.0                 # smoothed (v.P + w.L)/m below this -> rest
REST_TIME = 10.0                      # s since the last wake -> rest
GROUND_REST_TIME = 5.0                # s: a terrain contact after this forces the rest next frame
CONTACT_DAMP = 0.96666664             # P and L x this on every contact
MIN_PUSH = 2.0                        # cm: push-out depth floor
FRICTION = 0.5                        # node +0xDC (constant)
RESTITUTION = 0.0                     # node +0xD8 (constant 0 in the ctor; no writer found)
SLOW_SPEED = 277.77777                # cm/s (+0xE0): below it the terrain bounce uses e = 0 (e is 0 anyway)
KICK_UP_MAX = 600.0                   # cm/s
SPIN_KICK = 10.0                      # rad/s along the contact normal (or -v_record)
RIDER_E = 0.3                         # rider -> body impulse uses (1 + 0.3)


@dataclass
class RollerRecord:
    """Factory subtype 0, 36 bytes: payload words 1..6 = mass, w2, w3, velocity xyz."""
    mass: float
    w2: float                          # copied to node +0xD0; no reader found (TODO)
    w3: float                          # not read by the ctor (TODO)
    velocity: Vec                      # (0,0,0): kick from the rider contact; else this launch velocity


class RigidBody:
    def __init__(self, mass: float, inv_inertia_body, com_offset: Vec, pos: Vec, q: Tuple[float, ...]):
        self.inv_mass = 1.0 / mass
        self.inv_I_body = [tuple(x * INERTIA_SCALE for x in r) for r in inv_inertia_body]
        self.com_offset = com_offset
        self.x = pos                     # centre of mass (world)
        self.q = q                       # (w, x, y, z)
        self.P = (0.0, 0.0, 0.0)         # linear momentum
        self.L = (0.0, 0.0, 0.0)         # angular momentum
        self.v = (0.0, 0.0, 0.0)
        self.w = (0.0, 0.0, 0.0)
        self.update_inertia()

    def update_inertia(self):
        R = quat_to_matrix(self.q)       # rows = body axes in world
        Rt = _transpose(R)
        self.inv_I = _mat_mul(_mat_mul(Rt, self.inv_I_body), R)

    def derive(self):
        self.v = _mul(self.P, self.inv_mass)
        self.w = _mat_vec(self.inv_I, self.L)
        n = _len(self.w)
        if n > OMEGA_MAX:
            s = OMEGA_MAX / n
            self.w = _mul(self.w, s)
            self.L = _mul(self.L, s)

    def set_omega(self, w: Vec):
        self.w = w
        self.L = _mat_vec(_inv3(self.inv_I), w)

    def integrate(self, F: Vec, T: Vec, dt: float = FRAME):
        """x += v dt; q += 0.5 dt (w x q) and renormalise; P += F dt; L += T dt (then v, w derived)."""
        self.x = _add(self.x, _mul(self.v, dt))
        w0, x0, y0, z0 = self.q
        wx, wy, wz = self.w
        nw = w0 + (-wx * x0 - wy * y0 - wz * z0) * 0.5 * dt
        nx = x0 + (wx * w0 + wy * z0 - wz * y0) * 0.5 * dt
        ny = y0 + (wy * w0 + wz * x0 - wx * z0) * 0.5 * dt
        nz = z0 + (wz * w0 + wx * y0 - wy * x0) * 0.5 * dt
        n = math.sqrt(nw * nw + nx * nx + ny * ny + nz * nz)
        self.q = (1.0, nx, ny, nz) if n == 0.0 else (nw / n, nx / n, ny / n, nz / n)
        self.P = _add(self.P, _mul(F, dt))
        self.L = _add(self.L, _mul(T, dt))

    def point_velocity(self, c: Vec) -> Vec:
        return _add(self.v, _cross(self.w, _sub(c, self.x)))

    def apply_impulse(self, r: Vec, J: Vec):
        self.P = _add(self.P, J)
        self.L = _add(self.L, _cross(r, J))
        self.derive()

    def _k(self, r: Vec, d: Vec) -> float:
        return self.inv_mass + _dot(d, _cross(_mat_vec(self.inv_I, _cross(r, d)), r))

    def contact_impulse(self, e: float, mu: float, c: Vec, n: Vec):
        """Contact impulse: optional friction impulse (kills mu of the tangential contact speed), then if the
        contact still closes, a normal impulse -(1 + e) u_n / K."""
        r = _sub(c, self.x)
        u = self.point_velocity(c)
        un = _dot(u, n)
        if mu != 0.0:
            t = _sub(u, _mul(n, un))
            tl = _len(t)
            if tl > 0.0:
                th = _mul(t, 1.0 / tl)
                self.apply_impulse(r, _mul(th, -mu * tl / self._k(r, th)))
            u = self.point_velocity(c)
            un = _dot(u, n)
            if un > 0.0:
                return
        self.apply_impulse(r, _mul(n, -(e + 1.0) * un / self._k(r, n)))


class Roller:
    """One woken prop.  asleep = node +0x140 (slot 12): asleep props get no update; the next rider
    touch re-runs the instance's slot-1 effect, whose Roller factory call restarts it (slot 14)."""

    def __init__(self, rec: RollerRecord, body: RigidBody, contact: Optional[dict] = None):
        self.body = body
        self.mass = rec.mass
        self.t = 0.0                     # +0xE4 seconds since wake
        self.contact_time = 0.0          # +0xE8 seconds of contact (decays x 0.8333 per frame)
        self.energy = 0.0                # +0xC8 smoothed activity
        self.asleep = False
        if any(rec.velocity):
            body.P = _mul(rec.velocity, rec.mass)
            body.v = rec.velocity
            body.set_omega(_mul(_norm(_mul(rec.velocity, -1.0)), SPIN_KICK))
        else:
            # contact record {c, v_hat, n, a}: v0 = 0.5 a v_hat + (0, 0, min(100 + 5000/m, 600))
            up = min(5000.0 / rec.mass + 100.0, KICK_UP_MAX)
            v0 = _add(_mul(contact["v_hat"], contact["a"] * 0.5), (0.0, 0.0, up)) if contact else (0.0, 0.0, up)
            body.v = v0
            body.P = _mul(v0, rec.mass)
            body.set_omega(_mul(contact["n"], SPIN_KICK) if contact else (0.0, 0.0, 0.0))
        self.energy = _dot(body.v, body.P) + _dot(body.w, body.L)

    def wake(self) -> None:
        """Wake on a rider hit through the moving-object list: only acts on an asleep body."""
        if self.asleep:
            self.t = 0.0
            self.contact_time = 0.0
            self.energy = _dot(self.body.v, self.body.P) + _dot(self.body.w, self.body.L)
            self.asleep = False

    def frame(self, terrain_query: Callable = None, prop_query: Callable = None) -> bool:
        """One update; returns True on the frame it comes to rest (done(1): slot-3 effect, or asleep)."""
        if self.asleep:
            return False
        b = self.body
        self.t += FRAME
        self.energy = self.energy * 0.95 + (_dot(b.v, b.P) + _dot(b.w, b.L)) * 0.05 * b.inv_mass
        if self.energy < REST_ENERGY or self.t >= REST_TIME:
            self.t = 0.0
            b.P = b.L = b.v = b.w = (0.0, 0.0, 0.0)
            self.asleep = True
            return True
        self.contact_time *= 0.8333333
        d = min(max(self.contact_time * 10.0, 0.0), 2.0)
        k = (2.0 * self.t) * d + d                      # drag grows with contact time and age
        F = _sub((0.0, 0.0, -GRAVITY * self.mass), _mul(b.P, k))
        T = _mul(b.L, -k)
        b.integrate(F, T)
        b.update_inertia()
        b.derive()
        for query, is_terrain in ((terrain_query, True), (prop_query, False)):
            if query is None:
                continue
            hit = query(b)                              # -> (depth, n, c) or None
            if not hit:
                continue
            depth, n, c = hit
            depth = max(depth, MIN_PUSH)
            b.x = _add(b.x, _mul(n, depth))
            b.L = _mul(b.L, CONTACT_DAMP)
            b.w = _mat_vec(b.inv_I, b.L)
            b.v = _mul(b.v, CONTACT_DAMP)
            b.P = _mul(b.v, self.mass)
            if _dot(b.point_velocity(c), n) < 0.0:
                e = RESTITUTION if not (is_terrain and _len(b.v) < SLOW_SPEED) else 0.0
                b.contact_impulse(e, FRICTION, c, n)
                self.contact_time += FRAME
                if is_terrain and self.t > GROUND_REST_TIME:
                    self.t = REST_TIME
        return False


def rider_hits_body(body: RigidBody, rider_v: Vec, rider_mass: float, c: Vec, n: Vec) -> Optional[Vec]:
    """Rider -> body impulse through the moving-object list: n points from the body
    to the rider; when v_n = n . (v_rider - v_point) < 0 the body gets J n at r, the rider nothing."""
    r = _sub(c, body.x)
    vn = _dot(n, _sub(rider_v, body.point_velocity(c)))
    if vn >= 0.0:
        return None
    J = (1.0 + RIDER_E) * vn / (_dot(n, _cross(_mat_vec(body.inv_I, _cross(r, n)), r)) + body.inv_mass
                                + 1.0 / rider_mass)
    body.apply_impulse(r, _mul(n, J))
    return _mul(n, J)


def rider_movable_dv(a: float, rider_mass: float, prop_mass: float, e: float) -> float:
    """Rider bounce off a Roller-controlled prop (movable branch), as compiled."""
    k = 1.0 / (rider_mass / prop_mass + 1.0)
    return math.sqrt(max((a * k) ** 2 - (1.0 - e) * k, 0.0)) - a * k


# Instance flag changes when the Roller is created: clear 0x2 and 0x20, set 0x1 | 0x4 | 0x40
ROLLER_FLAGS_CLEAR, ROLLER_FLAGS_SET = 0x22, 0x45

# cMeshAnimNode
MESH_MAX_PIECES = 24
DEBRIS_GRAVITY = 16.333334            # cm/frame per frame, times record word 2


@dataclass
class MeshAnimRecord:
    """Subtype 20, 52 bytes: words 1..10."""
    keep: int                          # word 1: 1 = stay gone after the time; else done(1) (restore)
    gravity: float                     # word 2
    seconds: float                     # word 3: life
    velocity: Vec                      # words 4..6, instance space (zero: |a| v_hat of the contact)
    spread: Vec                        # words 7..9: random velocity range (x, y signed, z up only)
    vel_scale: float                   # word 10


@dataclass
class Piece:
    pos: Vec
    q: Tuple[float, float, float, float]    # (w, x, y, z)
    vel: Vec                                 # cm per frame
    axis: Vec
    spin: float                              # rad per frame


class MeshAnim:
    def __init__(self, rec: MeshAnimRecord, pieces: Sequence[Tuple[Vec, Tuple[float, ...]]],
                 inst_rot, contact: Optional[dict], rand01: Callable[[], float],
                 rand_bit: Callable[[], int]):
        if any(rec.velocity):
            v0 = _mat_vec(_transpose(inst_rot), rec.velocity)   # instance matrix rows applied to the vector
        elif contact:
            v0 = _mul(contact["v_hat"], abs(contact["a"]))
        else:
            v0 = (0.0, 0.0, 0.0)
        v0 = _mul(v0, rec.vel_scale)
        self.timer = int(rec.seconds * 60.0)
        self.keep = rec.keep
        self.gravity = rec.gravity
        self.active = True                       # +0x6CC (drawn while set)
        self.pieces: List[Piece] = []
        for pos, q in pieces[:MESH_MAX_PIECES]:
            spin = (rand01() * 8.0 + 2.0) * FRAME
            axis = _norm((rand01() * 2 - 1, rand01() * 2 - 1, rand01() * 2 - 1))
            self.pieces.append(Piece(pos, tuple(q), None, axis, spin))
        for p in self.pieces:
            rx = rec.spread[0] * rand01() * FRAME
            if rand_bit():
                rx = -rx
            ry = rec.spread[1] * rand01() * FRAME
            if rand_bit():
                ry = -ry
            rz = rec.spread[2] * rand01() * FRAME
            p.vel = _add(_mul(v0, FRAME), (rx, ry, rz))

    def frame(self) -> Optional[str]:
        """Returns 'done' on frames the node calls done(1) (keep != 1: slot-3 effect, or restore flags and
        destroy), 'stop' when the time is up with keep == 1 (the node stays, draws nothing)."""
        if self.timer < 1 or self.timer - 1 < 1:
            if self.timer >= 1:
                self.timer -= 1
            self.active = False
            return "stop" if self.keep == 1 else "done"
        self.timer -= 1
        if not self.active:
            return None
        for p in self.pieces:
            h = p.spin * 0.5
            s, c = math.sin(h), math.cos(h)
            dq = (c, p.axis[0] * s, p.axis[1] * s, p.axis[2] * s)
            p.q = _qmul(p.q, dq)
            n = math.sqrt(sum(x * x for x in p.q))
            p.q = (1.0, 0.0, 0.0, 0.0) if n == 0 else tuple(x / n for x in p.q)
        for p in self.pieces:
            p.pos = _add(p.pos, p.vel)
            p.vel = (p.vel[0], p.vel[1], p.vel[2] - self.gravity * DEBRIS_GRAVITY)
        return None


def _qmul(a, b):
    w1, x1, y1, z1 = a
    w2, x2, y2, z2 = b
    return (w1 * w2 - x1 * x2 - y1 * y2 - z1 * z2,
            w1 * x2 + x1 * w2 + y1 * z2 - z1 * y2,
            w1 * y2 + y1 * w2 + z1 * x2 - x1 * z2,
            w1 * z2 + z1 * w2 + x1 * y2 - y1 * x2)


# cCrackedNode
CRACK_DAMAGE = 0.036
CRACK_COOLDOWN = 30


class Cracked:
    def __init__(self, seconds: float, health: float, contact: Optional[dict] = None):
        self.timer = int(seconds * 60.0)        # -1 s -> never expires
        self.health = health
        self.cooldown = CRACK_COOLDOWN
        self._damage(contact)                   # the creating touch already damages (no cooldown check)

    def _damage(self, contact):
        if contact and self.health > 0.0:
            self.cooldown = CRACK_COOLDOWN
            self.health -= abs(_dot(contact["v_hat"], contact["n"]) * contact["a"]) * CRACK_DAMAGE

    def touch(self, contact):
        if self.cooldown == 0:
            self._damage(contact)

    def frame(self) -> Optional[str]:
        """'break' = run slot 4 (or become a cDeadNode without one); 'expire' = done(1)."""
        if self.cooldown > 0:
            self.cooldown -= 1
        if self.timer >= 1:
            self.timer -= 1
            if self.timer == 0:
                return "expire"
        if self.health <= 0.0:
            return "break"
        return None


def flipbook_crack_frame(n_frames: int, r01: float) -> int:
    """The Cracked node draws its instance through a cTexFlip binding at frame -1 (random 1..n-1)."""
    if n_frames <= 2:
        return min(1, n_frames - 1)
    f = math.ceil((n_frames - 1) * r01 + 1)
    return min(f, n_frames - 1)
