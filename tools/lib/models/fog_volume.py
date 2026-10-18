"""Fog volumes (cFogObjMan / cPS2FogVolume).

Billboard fog puffs from the .pbd particle models / particle instances, drawn with PARTICLE.SSH image 34 'fog0',
ALPHA mode 3, no depth write. Used by races and by the front end (ssxfe.pbd).

    python models/fog_volume.py [pbd]     # lists models / instances and checks the layout
"""
import struct
import sys
from dataclasses import dataclass

FOG_TEXTURE_IMAGE = 34          # game+0x30 handle table entry 34 -> PARTICLE.SSH 'fog0' 128x128
ALPHA_MODE = 3                  # (Cs - Cd) As + Cd
LAYER = 4                       # renderer slot 58
FAR_FULL, FAR_ZERO = 12500.374, 20000.182     # cm, camera -> centre of the instance bbox
NEAR_CULL, NEAR_FULL = 500.0, 2601.9497       # cm, view depth w of each point
INSTANCE_SIZE = 112


@dataclass
class FogPoint:
    pos: tuple      # instance space
    rgb: tuple      # unit; x128 at build
    radius: float   # cm


@dataclass
class FogInstance:
    matrix: tuple   # 16 floats, row-major rows X, Y, Z, T
    model: int
    bbox_min: tuple
    bbox_max: tuple


def far_factor(d: float) -> float:
    """Alpha factor from the camera distance to the instance bbox centre; None = not drawn (cFogObjMan::vfunc_03)."""
    if d >= FAR_ZERO:
        return None
    return 1.0 if d < FAR_FULL else (FAR_ZERO - d) / 7499.8076


def near_factor(w: float) -> float:
    """Per-point alpha factor from its view depth; None = point skipped (cPS2FogVolume::vfunc_04)."""
    if w <= NEAR_CULL:
        return None
    if w < NEAR_FULL:
        t = (w - NEAR_CULL) / 2101.9497
        return t * t
    return 1.0


def point_alpha(d: float, w: float):
    """GS alpha byte of one puff (RGBAQ A, truncated; w is truncated to an int first); None when not drawn."""
    f, n = far_factor(d), near_factor(float(int(w)))
    return None if f is None or n is None else int(128.0 * f * n)


def bbox_centre(inst) -> tuple:
    """The far-fade reference point: (min + max) / 2 of the instance bbox (+0x44 / +0x50)."""
    return tuple((a + b) * 0.5 for a, b in zip(inst.bbox_min, inst.bbox_max))


def depth_key(w_origin: float) -> int:
    """Batch sort value of an instance (sort mode 2) from the view depth of its origin."""
    return max(0, min(1023, int(max(w_origin, 0.0) * 1023.0 / 40000.0)))


def ee_qsort_fog(keys: list) -> list:
    """qsort 0x2FB5C8 (EE library, BSD / Bentley-McIlroy) of [(index, int w)] with the comparator 0x1CAD28
    (a.w < b.w ? 1 : 0, never negative), step for step; returns the sorted copy (cPS2FogVolume::vfunc_04)."""
    A = list(keys)

    def cmp(a, b):
        return 1 if a[1] < b[1] else 0

    def med3(a, b, c):
        if cmp(A[a], A[b]) < 0:
            return b if cmp(A[b], A[c]) < 0 else (c if cmp(A[a], A[c]) < 0 else a)
        return b if cmp(A[b], A[c]) > 0 else (a if cmp(A[a], A[c]) < 0 else c)

    def insertion(lo, n):
        for pm in range(lo + 1, lo + n):
            pl = pm
            while pl > lo and cmp(A[pl - 1], A[pl]) > 0:
                A[pl], A[pl - 1] = A[pl - 1], A[pl]
                pl -= 1

    def qs(a, n):
        while True:
            if n < 7:
                insertion(a, n)
                return
            pm = a + n // 2
            if n > 7:
                pl, pn = a, a + n - 1
                if n > 40:
                    d = n // 8
                    pl, pm, pn = med3(pl, pl + d, pl + 2 * d), med3(pm - d, pm, pm + d), med3(pn - 2 * d, pn - d, pn)
                pm = med3(pl, pm, pn)
            A[a], A[pm] = A[pm], A[a]
            swapped = False
            pa = pb = a + 1
            pc = pd = a + n - 1
            while True:
                while pb <= pc:
                    r = cmp(A[pb], A[a])
                    if r > 0:
                        break
                    if r == 0:
                        swapped = True
                        A[pa], A[pb] = A[pb], A[pa]
                        pa += 1
                    pb += 1
                while pb <= pc:
                    r = cmp(A[pc], A[a])
                    if r < 0:
                        break
                    if r == 0:
                        swapped = True
                        A[pc], A[pd] = A[pd], A[pc]
                        pd -= 1
                    pc -= 1
                if pb > pc:
                    break
                A[pb], A[pc] = A[pc], A[pb]
                swapped = True
                pb += 1
                pc -= 1
            if not swapped:
                insertion(a, n)
                return
            end = a + n
            r = min(pa - a, pb - pa)
            for k in range(r):
                A[a + k], A[pb - r + k] = A[pb - r + k], A[a + k]
            r = min(pd - pc, end - pd - 1)
            for k in range(r):
                A[pb + k], A[end - r + k] = A[end - r + k], A[pb + k]
            if pb - pa > 1:
                qs(a, pb - pa)
            r = pd - pc
            if r > 1:
                a, n = end - r, r
                continue
            return

    if len(A) > 1:
        qs(0, len(A))
    return A


def gs_modulate(ct, at, cv, av):
    """TEX0 TFX MODULATE, TCC 1: (Cs rgb, As) from the texel (Ct, At 0..128) and the vertex bytes (Cv, Av)."""
    return tuple(min((c * v) >> 7, 255) for c, v in zip(ct, cv)), min((at * av) >> 7, 255)


def gs_alpha3(cs: int, a: int, cd: int) -> int:
    """ALPHA mode 3 = 0x44: ((Cs - Cd) As >> 7) + Cd, COLCLAMP."""
    return max(0, min(255, (((cs - cd) * a) >> 7) + cd))


def half_size(radius: float, w: float, proj_x: float, proj_y: float) -> tuple:
    """Screen half-extent of the quad (renderer +0x1730 / +0x1744 projection scales)."""
    return radius / w * proj_x, radius / w * proj_y


def load(path: str):
    d = open(path, "rb").read()
    h = struct.unpack_from("<34i", d, 0)
    n_inst, n_models = h[4], h[12]
    off_inst, off_ptr, off_models = h[16 + 3], h[16 + 12], h[16 + 13]
    models = []
    for m in range(n_models):
        o = off_models + struct.unpack_from("<i", d, off_ptr + 4 * m)[0]
        size, n_sub, sub = struct.unpack_from("<3i", d, o)
        pts = []
        for _ in range(n_sub):                   # retail: always 1 (the game re-reads the first one)
            so = o + sub
            ob = so + struct.unpack_from("<i", d, so + 4)[0]
            cnt, lst = struct.unpack_from("<2i", d, ob + 0x20)
            for k in range(cnt):
                v = struct.unpack_from("<7f", d, ob + lst + 28 * k)
                pts.append(FogPoint(v[0:3], v[3:6], v[6]))
        models.append(pts)
    inst = []
    for i in range(n_inst):
        o = off_inst + INSTANCE_SIZE * i
        inst.append(FogInstance(struct.unpack_from("<16f", d, o), struct.unpack_from("<i", d, o + 0x40)[0],
                                struct.unpack_from("<3f", d, o + 0x44), struct.unpack_from("<3f", d, o + 0x50)))
    return models, inst


if __name__ == "__main__":
    p = sys.argv[1] if len(sys.argv) > 1 else \
        __import__("os").path.join(__file__, "..", "..", "..", "..", "unpacked", "MODELS", "SSXFE", "data", "models",
                                     "ssxfe.pbd")
    models, inst = load(p)
    ok = all(0 <= i.model < len(models) for i in inst) and all(len(m) >= 1 for m in models)
    for k, m in enumerate(models):
        print(k, [(round(q.radius, 1), tuple(round(c, 2) for c in q.rgb)) for q in m])
    print("instances", len(inst), "models", len(models), "ALL OK" if ok else "FAIL")
    assert point_alpha(1000.0, 3000.0) == 128 and point_alpha(30000.0, 3000.0) is None
    assert [w for _, w in ee_qsort_fog(list(enumerate([100, 900, 300, 700, 500, 200, 800, 600, 400, 1000])))] ==         [200, 900, 300, 700, 500, 1000, 800, 600, 400, 100]
    (cs, a) = gs_modulate((223, 222, 222), 81, (64, 64, 64), 128)
    assert cs[0] == 111 and a == 81 and gs_alpha3(cs[0], a, 128) == 117
    assert abs(near_factor(1551.0) - 0.25) < 1e-3 and near_factor(400.0) is None
