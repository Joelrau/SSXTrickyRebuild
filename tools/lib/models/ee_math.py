"""EE math library and body-quaternion helpers in single precision, mirroring the C++ port operation for operation
(Sim/Anim/RiderPose.cpp SinCosEE / AsinEE / AtanEE / Atan2EE, Sim/RiderSim.cpp RotateBodyEE / UpdateBasis, Sim/EEFloat.h).
Values are numpy float32 scalars; FPU adds go through fpu.fpu_add / fpu_sub, VU0 sums are IEEE."""
from __future__ import annotations

import numpy as np

from fpu import f32, fpu_add, fpu_sub

C = f32
ASIN_C = (C(0.16770227253437042), C(0.06765850633382797), C(0.05200503021478653), C(0.05498472973704338))
HALF_PI = C(1.5707964)


def sqrt32(v) -> np.float32:
    with np.errstate(all="ignore"):
        return f32(np.sqrt(np.float64(v)))


def sincos_ee(a):
    """0x250C10."""
    a = f32(a)
    k = fpu_sub(a * C(0.63661975), C(0.5)) if a < 0 else fpu_add(a * C(0.63661975), C(0.5))
    ki = int(k)                      # cvt.w.s truncates
    q = ki & 3
    x = fpu_sub(a, f32(ki) * HALF_PI)
    x2 = x * x
    p = x2 * C(2.7557319e-06)
    p = fpu_add(p, C(-0.0001984127))
    p = fpu_add(p * x2, C(0.008333334))
    p = fpu_add(p * x2, C(-0.16666667))
    p = p * x2
    p = fpu_add(p, C(1.0))
    s = p * x
    c = sqrt32(max(fpu_sub(C(1.0), s * s), C(0.0)))
    if q == 0:
        return s, c
    if q == 1:
        return c, -s
    if q == 2:
        return -s, -c
    return -c, s


def asin_ee(x):
    """0x250FD0."""
    c0, c1, c2, c3 = ASIN_C
    x = f32(x)

    def poly(u):
        return u * fpu_add(c0, u * fpu_add(c1, u * fpu_add(c2, u * c3)))
    if x < C(-0.70710677):
        u = fpu_sub(C(1.0), x * x)
        return fpu_sub(sqrt32(u) * fpu_add(poly(u), C(1.0)), HALF_PI)
    u = x * x
    if x <= C(0.70710677):
        return fpu_add(poly(u) * x, x)
    v = fpu_sub(C(1.0), u)
    return fpu_sub(HALF_PI, sqrt32(v) * fpu_add(poly(v), C(1.0)))


def atan_ee(x):
    c0, c1, c2, c3 = ASIN_C
    x = f32(x)

    def series(f):
        return sqrt32(f) * fpu_add(f * fpu_add(c0, f * fpu_add(c1, f * fpu_add(c2, f * c3))), C(1.0))
    if x < C(-1.0):
        return fpu_add(series(C(1.0) / fpu_add(x * x, C(1.0))), -HALF_PI)
    u = x * x
    if C(1.0) < x:
        return fpu_sub(HALF_PI, series(C(1.0) / fpu_add(u, C(1.0))))
    r = series(u / fpu_add(u, C(1.0)))
    return -r if x < 0 else r


def atan2_ee(y, x):
    y, x = f32(y), f32(x)
    if x == 0:
        return y if y == 0 else (-HALF_PI if y < 0 else HALF_PI)
    a = atan_ee(y / x)
    if x < 0:
        return fpu_add(a, C(3.1415927)) if 0 < y else fpu_sub(a, C(3.1415927))
    return a


def v3(v):
    return np.array(v, dtype=np.float32)


def vdot(a, b):
    m = a * b
    return (m[0] + m[1]) + m[2]


def vcross(a, b):
    return np.array([a[1] * b[2] - a[2] * b[1], a[2] * b[0] - a[0] * b[2], a[0] * b[1] - a[1] * b[0]], dtype=np.float32)


def vlength(a):
    return sqrt32(max(vdot(a, a), C(0.0)))


def rsqrt32(x):
    with np.errstate(all="ignore"):
        return f32(C(1.0) / f32(np.sqrt(np.float64(x))))


def vnormalize(a):
    return rsqrt32(vdot(a, a)) * a


def update_basis(q):
    """0x129008: normalise q and rebuild (side, forward, up). Returns (q, side, fwd, up), all float32."""
    x, y, z, w = (f32(c) for c in q)
    ln = sqrt32(fpu_add(fpu_add(fpu_add(x * x, y * y), z * z), w * w))
    if ln == 0:
        w = C(1.0)
    else:
        inv = C(1.0) / ln
        w, x, y, z = w * inv, x * inv, y * inv, z * inv
    w2 = fpu_add(w, w)

    def rot(ex, ey, ez):
        cx, cy, cz = fpu_sub(y * ez, z * ey), fpu_sub(z * ex, x * ez), fpu_sub(x * ey, y * ex)
        dx, dy, dz = fpu_sub(y * cz, z * cy), fpu_sub(z * cx, x * cz), fpu_sub(x * cy, y * cx)
        tx, ty, tz = fpu_add(ex, w2 * cx), fpu_add(ey, w2 * cy), fpu_add(ez, w2 * cz)
        two = C(2.0)
        return np.array([fpu_add(tx, two * dx), fpu_add(ty, two * dy), fpu_add(tz, two * dz)], dtype=np.float32)
    return (x, y, z, w), rot(C(1), C(0), C(0)), rot(C(0), C(1), C(0)), rot(C(0), C(0), C(1))


def rotate_body_ee(q, axis, angle):
    """0x128E90: q <- q (x) rotation(axis, angle), then the basis. Returns update_basis' tuple."""
    s, cs = sincos_ee(f32(angle) * C(0.5))
    ax, ay, az = s * axis[0], s * axis[1], s * axis[2]
    x, y, z, w = (f32(c) for c in q)
    nx = fpu_sub(fpu_add(fpu_add(cs * x, ax * w), ay * z), az * y)
    ny = fpu_sub(fpu_add(fpu_add(cs * y, ay * w), az * x), ax * z)
    nz = fpu_sub(fpu_add(fpu_add(cs * z, az * w), ax * y), ay * x)
    nw = fpu_sub(fpu_sub(fpu_sub(cs * w, ax * x), ay * y), az * z)
    return update_basis((nx, ny, nz, nw))


def quat_from_basis(side, fwd, up):
    """Quaternion (x, y, z, w) of the rotation whose images of X, Y, Z are side, fwd, up (double, then rounded)."""
    m = np.array([side, fwd, up], dtype=np.float64).T      # columns are the images
    t = m[0, 0] + m[1, 1] + m[2, 2]
    if t > 0:
        s = 0.5 / np.sqrt(t + 1.0)
        w, x, y, z = 0.25 / s, (m[2, 1] - m[1, 2]) * s, (m[0, 2] - m[2, 0]) * s, (m[1, 0] - m[0, 1]) * s
    elif m[0, 0] > m[1, 1] and m[0, 0] > m[2, 2]:
        s = 2.0 * np.sqrt(1.0 + m[0, 0] - m[1, 1] - m[2, 2])
        w, x, y, z = (m[2, 1] - m[1, 2]) / s, 0.25 * s, (m[0, 1] + m[1, 0]) / s, (m[0, 2] + m[2, 0]) / s
    elif m[1, 1] > m[2, 2]:
        s = 2.0 * np.sqrt(1.0 + m[1, 1] - m[0, 0] - m[2, 2])
        w, x, y, z = (m[0, 2] - m[2, 0]) / s, (m[0, 1] + m[1, 0]) / s, 0.25 * s, (m[1, 2] + m[2, 1]) / s
    else:
        s = 2.0 * np.sqrt(1.0 + m[2, 2] - m[0, 0] - m[1, 1])
        w, x, y, z = (m[1, 0] - m[0, 1]) / s, (m[0, 2] + m[2, 0]) / s, (m[1, 2] + m[2, 1]) / s, 0.25 * s
    n = np.sqrt(x * x + y * y + z * z + w * w)
    return (f32(x / n), f32(y / n), f32(z / n), f32(w / n))


def _even_poly(r2):
    return fpu_add(fpu_add(fpu_add(fpu_add(r2 * C(2.4801588e-05), C(-0.0013888889)) * r2, C(0.041666668)) * r2, C(-0.5)) * r2, C(1.0))


def _odd_poly(r2, r):
    return fpu_add((fpu_add(fpu_add(fpu_add(r2 * C(2.7557319e-06), C(-0.0001984127)) * r2, C(0.008333334)) * r2, C(-0.16666667)) * r2) * r, r)


def _range_reduce(x):
    x = f32(x)
    k = fpu_sub(x * C(0.63661975), C(0.5)) if x < 0 else fpu_add(x * C(0.63661975), C(0.5))
    ki = int(k)
    u = ki & 0xFFFFFFFF
    r = fpu_sub(x, f32(ki) * HALF_PI)
    return u, r, r * r


def cos_ee(x):
    u, r, r2 = _range_reduce(x)
    out = _even_poly(r2) if (u & 1) == 0 else _odd_poly(r2, r)
    return -out if ((u + 1) & 2) != 0 else out


def sin_ee(x):
    u, r, r2 = _range_reduce(x)
    out = _odd_poly(r2, r) if (u & 1) == 0 else _even_poly(r2)
    return -out if (u & 2) != 0 else out
