"""World collision and ground-probe reference model.

What is modelled
  * the ground-probe contract as a small interface: ProbeInput -> ProbeResult;
  * a reference World made of power-basis bicubic patches:
      - on-demand 10x10 tessellation, fine (9x9) / coarse (3x3) cells, two triangles per cell
      - segment-vs-triangle and sphere-set-vs-triangle tests
      - hit selection by |t - t_ref|, type-17 patches ignored
      - Newton refinement of line hits on the analytic surface, 4 iterations
  * the grounded wall response, rider-vs-rider separation / impulse and the hit classifier.

The .ltg grid is replaced by a plain AABB loop: it changes speed, not results.
"""
from dataclasses import dataclass, field
from typing import List, Optional, Sequence
import numpy as np

from ee_math import v3, vdot

Z_AXIS = np.array([0.0, 0.0, 1.0])

TESS_N = 10                      # vertices per side, u, v = k / 9
NEWTON_ITERATIONS = 4            # line type 2 refinement
PARALLEL_EPS = 1e-10             # segment-triangle: |n.d| below this is a miss
INSIDE_EPS = 1e-4                # point-in-triangle tolerance
IGNORED_SURFACES = {17}          # never collided
PROBE_BELOW = 100.0              # probe segment starts this far below P along n
PROBE_ABOVE = 200.0              # ... and ends this far above
PROBE_T_REF = 0.5                # prefer the hit nearest P + 50 n
FLAT_EPS = 1e-4                  # |n x Z| below this: fall line falls back to forward
WALL_FILTER_DOT = 0.8            # wall test ignores normals this close to ground normal
WALL_PUSH_SCALE = 1.1            # push-out = 1.1 * depth
WALL_RESTITUTION = 0.5           # bounce = max(0.5 a, 55.56)
WALL_MIN_BOUNCE = 55.555553      # cm/s (2 km/h)
RIDER_SEPARATION_SHARE = 0.55    # each rider moves 0.55 of the overlap
HIT_CLAMP = 500.0                # cm/s, applied impulse clamp
HIT_LAUNCH = 1000.0              # above this a grounded rider can be launched
HIT_BUMP = 167.31738             # stumble threshold
HIT_KNOCKDOWN = 733.9661         # knock-down threshold (normal modes)
HIT_KNOCKDOWN_MODE0 = 530.50287  # knock-down threshold when the game-mode global is 0
NO_GROUND_KIND = 3               # contact_kind when the probe misses


@dataclass
class Patch:
    """P(u, v) = sum_{i,j} C[4 i + j] u^i v^j  (pbd.py convention; the game stores C reversed)."""
    coeffs: np.ndarray            # shape
    surface: int = 1
    _grid: Optional[np.ndarray] = field(default=None, repr=False)

    def _powers(self, x):
        return np.array([1.0, x, x * x, x * x * x]), np.array([0.0, 1.0, 2 * x, 3 * x * x])

    def eval(self, u, v):
        """Return P, dP/du, dP/dv."""
        pu, dpu = self._powers(u)
        pv, dpv = self._powers(v)
        c = self.coeffs.reshape(4, 4, 3)
        p = np.einsum("i,j,ijk->k", pu, pv, c)
        du = np.einsum("i,j,ijk->k", dpu, pv, c)
        dv = np.einsum("i,j,ijk->k", pu, dpv, c)
        return p, du, dv

    def grid(self):
        """10x10 tessellation at u, v = k/9, built on first use (the game keeps an LRU cache)."""
        if self._grid is None:
            ks = np.linspace(0.0, 1.0, TESS_N)
            self._grid = np.array([[self.eval(u, v)[0] for v in ks] for u in ks])
        return self._grid

    def aabb(self):
        g = self.grid().reshape(-1, 3)
        return g.min(axis=0), g.max(axis=0)


def cell_triangles(patch: Patch, fine: bool):
    """Yield (row, col, which, (V0, V1, V2), unit normal) for each cell.

    Corners: V00 (row r, col c), V01 (col + step), V10 (row + step), V11.
    Triangle A = (V11, V10, V01) is tested before triangle B = (V01, V10, V00).
    """
    g = patch.grid()
    step = 1 if fine else 3
    for r in range(0, TESS_N - 1, step):
        for c in range(0, TESS_N - 1, step):
            v00, v01 = g[r, c], g[r, c + step]
            v10, v11 = g[r + step, c], g[r + step, c + step]
            for which, tri in ((0, (v11, v10, v01)), (1, (v01, v10, v00))):
                # sign chosen to agree with dP/du x dP/dv
                n = np.cross(tri[1] - tri[2], tri[0] - tri[1])
                ln = np.linalg.norm(n)
                if ln > 0:
                    yield r // step, c // step, which, tri, n / ln


def _inside(point, tri, n, eps=INSIDE_EPS):
    """Point-in-triangle for a point in the triangle plane, independent of vertex winding."""
    signs = []
    for k in range(3):
        a, b = tri[k], tri[(k + 1) % 3]
        signs.append(np.dot(np.cross(b - a, point - a), n) / max(1.0, np.linalg.norm(b - a)))
    return min(signs) >= -eps or max(signs) <= eps


def segment_triangle(a, d, tri, n):
    """Two-sided segment A + t d, t in [0, 1], against a triangle.  Returns t or None."""
    denom = np.dot(n, d)
    if abs(denom) < PARALLEL_EPS:
        return None
    t = np.dot(n, tri[0] - a) / denom
    if t < 0.0 or t > 1.0:
        return None
    return t if _inside(a + t * d, tri, n) else None


def _sphere_crosses_edge(center, radius, a, b):
    e = b - a
    length = np.linalg.norm(e)
    rel = center - a
    proj = np.dot(rel, e) / length
    perp2 = np.dot(rel, rel) - proj * proj
    if proj < 0.0 or perp2 > radius * radius:
        return False
    return not (length < proj - np.sqrt(radius * radius - perp2))


def _sphere_touches(center, radius, tri, n, d):
    foot = center - d * n
    if _inside(foot, tri, n):
        return True
    return any(_sphere_crosses_edge(center, radius, a, b)
               for a, b in ((tri[0], tri[1]), (tri[0], tri[2]), (tri[1], tri[2])))


@dataclass
class SphereSet:
    """Rider collision shape: bounding sphere plus masked sub-spheres."""
    center: np.ndarray
    radius: float
    subs: List[tuple] = field(default_factory=list)      # (center, radius)
    mask: int = -1

    def enabled(self):
        return [s for k, s in enumerate(self.subs) if (self.mask >> k) & 1]


def sphere_set_triangle(shape: SphereSet, tri, n):
    """Return (push vector, contact point) or None (5.3, boarder slot 5)."""
    d = np.dot(shape.center - tri[0], n)
    if abs(d) > shape.radius or not _sphere_touches(shape.center, shape.radius, tri, n, d):
        return None
    if not shape.subs:
        return n * (shape.radius - d), shape.center - d * n
    best = None
    for c, r in shape.enabled():
        di = np.dot(c - tri[0], n)
        if di < r and _sphere_touches(c, r, tri, n, di) and (best is None or di < best[0]):
            best = (di, n * (r - di), c - di * n)
    return None if best is None else (best[1], best[2])


@dataclass
class Hit:
    """World hit record."""
    kind: int                    # 0 static surface, 1 prop without surface, 2 animated prop
    point: np.ndarray
    normal: np.ndarray
    surface: int
    t: float                     # segment fraction for lines, depth for sphere sets
    patch: Optional[Patch] = None
    cell: tuple = (0, 0, 0)      # (row, col, triangle) of the fine/coarse grid
    fine: bool = True


def newton_refine(patch: Patch, a, d, u, v, t):
    """Line-vs-patch Newton on (u, v, t), fixed iteration count.

    Returns (point, normal) on success, None if (u, v) leaves [0, 1] (caller keeps the mesh hit).
    """
    for _ in range(NEWTON_ITERATIONS):
        p, du, dv = patch.eval(u, v)
        residual = p - (a + t * d)
        jac = np.column_stack([du, dv, -d])
        try:
            step = np.linalg.solve(jac, residual)
        except np.linalg.LinAlgError:
            return None
        u, v, t = u - step[0], v - step[1], t - step[2]
        if not (0.0 <= u <= 1.0 and 0.0 <= v <= 1.0):
            return None
    p, du, dv = patch.eval(u, v)
    n = np.cross(du, dv)
    return p, n / np.linalg.norm(n)


class World:
    """Reference world: a list of patches (the game adds prop instances and an .ltg grid)."""

    def __init__(self, patches: Sequence[Patch]):
        self.patches = list(patches)

    def _candidates(self, lo, hi):
        for patch in self.patches:
            if patch.surface in IGNORED_SURFACES:
                continue
            pmin, pmax = patch.aabb()
            if np.all(pmin <= hi) and np.all(lo <= pmax):
                yield patch

    def line_query(self, a, b, query_type=1, t_ref=PROBE_T_REF) -> Optional[Hit]:
        """Line query.  query_type 0 coarse, 1 fine, 2 fine + Newton refinement."""
        a, b = np.asarray(a, float), np.asarray(b, float)
        d = b - a
        best = None
        for patch in self._candidates(np.minimum(a, b), np.maximum(a, b)):
            for r, c, which, tri, n in cell_triangles(patch, fine=query_type != 0):
                t = segment_triangle(a, d, tri, n)
                if t is None:
                    continue
                hit = Hit(0, a + t * d, n, patch.surface, t, patch, (r, c, which), query_type != 0)
                if t_ref < 0:
                    best = hit
                    break
                if best is None or abs(t - t_ref) < abs(best.t - t_ref):
                    best = hit
        if best is not None and query_type == 2:
            step = 1.0 / (TESS_N - 1)
            u0, v0 = (best.cell[0] + 0.5) * step, (best.cell[1] + 0.5) * step
            refined = newton_refine(best.patch, a, d, u0, v0, best.t)
            if refined is not None:
                best.point, best.normal = refined
        return best

    def sphere_query(self, shape: SphereSet, fine=False, filter_dir=None) -> Optional[Hit]:
        """Sphere-set query.  Coarse returns the first hit, fine the deepest (t_ref = 2R)."""
        lo, hi = shape.center - shape.radius, shape.center + shape.radius
        t_ref = 2.0 * shape.radius if fine else -1.0
        best = None
        for patch in self._candidates(lo, hi):
            for r, c, which, tri, n in cell_triangles(patch, fine=fine):
                res = sphere_set_triangle(shape, tri, n)
                if res is None:
                    continue
                push, point = res
                depth = float(np.linalg.norm(push))
                if depth <= 0.0:
                    continue
                m = push / depth
                if filter_dir is not None and vdot(v3(m), v3(filter_dir)) > np.float32(WALL_FILTER_DOT):   # slot 4 0x2578C0: VU dot
                    continue
                hit = Hit(0, point, m, patch.surface, depth, patch, (r, c, which), fine)
                if t_ref < 0:
                    return hit
                if best is None or abs(depth - t_ref) < abs(best.t - t_ref):
                    best = hit
        return best


@dataclass
class ProbeInput:
    position: np.ndarray
    ground_normal: np.ndarray            # previous n
    forward: np.ndarray                  # body forward axis
    contact_side: np.ndarray             # previous contact side
    lean: float = 0.0                    # lean filter value (degrees)
    rider_scale: float = 1.0             # rider profile size scale


@dataclass
class ProbeResult:
    found: bool
    surface: Optional[int]               # None = unchanged
    ground_normal: np.ndarray
    contact_point: Optional[np.ndarray]  # None = unchanged
    contact_forward: np.ndarray
    contact_side: np.ndarray
    height: Optional[float]              # (P - c) . n'; None = unchanged
    fall_line: np.ndarray
    contact_kind: int


def _unit(x):
    return x / np.linalg.norm(x)


def fall_line(n, forward):
    """Downhill direction in the tangent plane, or forward on flat ground."""
    c = np.cross(n, Z_AXIS)
    if np.linalg.norm(c) <= FLAT_EPS:
        return forward
    return _unit(np.cross(n, _unit(c)))


def ground_probe(world: World, s: ProbeInput) -> ProbeResult:
    n = _unit(s.ground_normal)
    probe_point = s.position + s.contact_side * (s.rider_scale * s.lean)
    a = probe_point - PROBE_BELOW * n
    b = probe_point + PROBE_ABOVE * n
    hit = world.line_query(a, b, query_type=2, t_ref=PROBE_T_REF)
    if hit is None:
        n2 = Z_AXIS.copy()
        fwd = _unit(s.forward - np.dot(s.forward, n2) * n2)
        return ProbeResult(False, None, n2, None, fwd, np.cross(n2, fwd), None, s.forward,
                           NO_GROUND_KIND)
    n2 = hit.normal
    fwd = _unit(s.forward - np.dot(s.forward, n2) * n2)
    return ProbeResult(True, hit.surface, n2, hit.point, fwd, np.cross(n2, fwd),
                       float(np.dot(probe_point - hit.point, n2)), fall_line(n2, s.forward),
                       hit.kind)


def wall_response(velocity, ground_normal, hit_normal, depth, grounded=True):
    """Grounded wall contact.  Returns (position delta, new velocity, approach speed)."""
    m = np.asarray(hit_normal, float)
    g = np.asarray(ground_normal, float)
    if np.dot(m, g) < 0.0:
        m = _unit(m - np.dot(m, g) * g)
    push = WALL_PUSH_SCALE * depth * m
    if grounded and np.dot(push, g) < 0.0:
        push = push - np.dot(push, g) * g
    v = np.asarray(velocity, float).copy()
    approach = -np.dot(m, v)
    if approach >= 0.0:
        bounce = max(WALL_RESTITUTION * approach, WALL_MIN_BOUNCE)
        v = v + m * (approach + bounce)
    return push, v, approach


def rider_mass(stat_weight, stat_power, boost_strength):
    """Collision mass of a rider: stat bytes 0..255, boost_strength 0..1."""
    base = stat_weight * (0.79599 + 2.145779 * stat_power / 255.0)
    return base * (1.0 + 7.999995 * boost_strength)


def rider_separation(overlap_vec, a_fixed=False, b_fixed=False):
    """Position deltas (dA, dB) for an overlap vector from A to B."""
    d = np.asarray(overlap_vec, float)
    if a_fixed and b_fixed:
        return np.zeros(3), np.zeros(3)
    if a_fixed:
        return np.zeros(3), d
    if b_fixed:
        return -d, np.zeros(3)
    return -RIDER_SEPARATION_SHARE * d, RIDER_SEPARATION_SHARE * d


def rider_impulses(va, vb, ma, mb, u):
    """Signed impulses (J_A, J_B) along u (A towards B): fully inelastic normal exchange."""
    k = (np.dot(vb, u) - np.dot(va, u)) / (ma + mb)
    return mb * k, -ma * k


def classify_rider_hit(j, motion, along_ground_positive, heading_ok, mode_zero=False):
    """Outcome of ApplyRiderHit: 'launch', 'knockdown', 'bump', 'push' or 'none'.

    motion: 'ground', 'air', 'rail', 'wipeout'.
    """
    if abs(j) > HIT_LAUNCH and motion == "ground" and heading_ok and along_ground_positive:
        return "launch"
    if motion == "wipeout":
        return "push"
    mag = 0.0 if (motion == "air" and along_ground_positive) else abs(j)
    threshold = HIT_KNOCKDOWN_MODE0 if mode_zero else HIT_KNOCKDOWN
    if mag > threshold or motion == "rail":
        return "knockdown"
    if mag > HIT_BUMP:
        return "bump"
    return "push"


def sample_patch(origin=(0.0, 0.0, 0.0), size=2000.0, drop=400.0, bulge=60.0, surface=1):
    """A 20 m square snow patch: falls 'drop' cm along +x (easing off), crowned by 'bulge' across y.

    x = ox + size u,  y = oy + size v,  z = oz - drop u + 0.3 drop u^2 + 4 bulge (v - v^2)
    """
    c = np.zeros((16, 3))
    c[0] = origin                          # constant term
    c[4] = (size, 0.0, -drop)              # u
    c[8] = (0.0, 0.0, 0.3 * drop)          # u^2
    c[1] = (0.0, size, 4.0 * bulge)        # v
    c[2] = (0.0, 0.0, -4.0 * bulge)        # v^2
    return Patch(c, surface)


if __name__ == "__main__":
    np.set_printoptions(precision=3, suppress=True)
    patch = sample_patch()
    world = World([patch])
    u, v = 0.37, 0.61
    surface_point, du, dv = patch.eval(u, v)
    true_n = _unit(np.cross(du, dv))
    rider = surface_point + 20.0 * true_n           # 20 cm above the snow
    s = ProbeInput(position=rider, ground_normal=Z_AXIS.copy(), forward=np.array([1.0, 0, 0]),
                   contact_side=np.array([0.0, 1.0, 0.0]), lean=0.0)
    res = ground_probe(world, s)
    print("ground probe on a sample patch")
    print("  found          ", res.found, " surface", res.surface, " kind", res.contact_kind)
    print("  contact point  ", res.contact_point)
    print("  normal         ", res.ground_normal, " (analytic at (u,v):", true_n, ")")
    print("  height         ", round(res.height, 3), "cm")
    print("  contact fwd    ", res.contact_forward, " side", res.contact_side)
    print("  fall line      ", res.fall_line)
    mesh = world.line_query(s.position - 100 * Z_AXIS, s.position + 200 * Z_AXIS, 1)
    print("  mesh-only hit  ", mesh.point, " normal", mesh.normal)
    miss = ground_probe(world, ProbeInput(rider + np.array([0, 0, 5000.0]), Z_AXIS, s.forward,
                                          s.contact_side))
    print("probe far above the snow: found", miss.found, " kind", miss.contact_kind)

    wall = World([Patch(np.array([[0, 0, 0], [0, 0, 300], [0, 0, 0], [0, 0, 0],
                                  [0, 500, 0]] + [[0, 0, 0]] * 11, float), surface=10)])
    shape = SphereSet(np.array([20.0, 250.0, 150.0]), 30.0)
    hit = wall.sphere_query(shape, fine=False, filter_dir=Z_AXIS)
    if hit is not None:
        dp, v_new, a = wall_response(np.array([-900.0, 0, 0]), Z_AXIS, hit.normal, hit.t)
        print("wall: depth", round(hit.t, 2), "normal", hit.normal, "push", dp,
              "v'", v_new, "approach", round(a, 1))
    for j, motion in ((150, "ground"), (300, "ground"), (800, "ground"), (1200, "ground"), (50, "rail")):
        print("rider hit", j, motion, "->", classify_rider_hit(j, motion, True, True))
