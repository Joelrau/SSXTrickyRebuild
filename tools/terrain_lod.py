"""Terrain patch LOD of the original: edge levels, drawn strips, the shared
per-patch vertex block and the 81 index templates used by the run-time terrain component (World/SSXTerrainLod.h).
"""
from fractions import Fraction as Fr

LEVEL_TABLE = (4, 4, 6, 8)
A_CM, B_CM = 3000.0, 15000.0
EDGES = ((0, 1), (2, 3), (0, 2), (1, 3))
LEVELS = (4, 6, 8)


def edge_levels(corners, eye, a=A_CM, b=B_CM):
    d0 = 2.0 * a * a
    k = -2.0 / (2.0 * b * b - d0)
    c = 3.0 - k * d0
    sq = [sum((e - p) ** 2 for e, p in zip(eye, q)) for q in corners]
    out = []
    for i, j in EDGES:
        L = min(max(c + k * (sq[i] + sq[j]), 1.0), 3.0)
        out.append(LEVEL_TABLE[int(L)])
    return out


def _grid_strips(ks):
    return [[(u, ks[s + o]) for u in ks for o in (0, 1)] for s in range(len(ks) - 1)]


def _edge_point(edge, t, inner):
    off = Fr(1, 7) if inner else Fr(0)
    return {0: (t, off), 1: (t, 1 - off), 2: (off, t), 3: (1 - off, t)}[edge]


def _stitch(edge, n):
    o = [_edge_point(edge, Fr(k, n - 1), False) for k in range(n)]
    i = [None] + [_edge_point(edge, Fr(k, 7), True) for k in range(1, 7)]
    if n == 8:
        return [o[0]] + [x for k in range(1, 7) for x in (i[k], o[k])] + [o[7]]
    if n == 6:
        return [o[0]] + [x for k in range(1, 6) for x in (i[k], o[k])] + [i[6]]
    if n == 4:
        return [i[1], o[0], i[2], o[1], i[3], o[2], i[4], o[3], i[5], i[6]]
    raise ValueError(n)


def patch_strips(levels):
    """[c0c1, c2c3, c0c2, c1c3] -> strips of (u, v) Fractions in the original's draw order."""
    if len(set(levels)) == 1:
        n = levels[0]
        return _grid_strips([Fr(k, n - 1) for k in range(n)])
    return _grid_strips([Fr(k, 7) for k in range(1, 7)]) + [_stitch(e, levels[e]) for e in range(4)]


def lattice():
    """The 108 (u, v) of the per-patch vertex block, in block order."""
    out = [(Fr(i, 7), Fr(j, 7)) for i in range(8) for j in range(8)]
    for n in (6, 4):
        out += [(Fr(i, n - 1), Fr(j, n - 1)) for i in range(n) for j in range(n)
                if not (i in (0, n - 1) and j in (0, n - 1))]
    return out


LATTICE = lattice()
LATTICE_INDEX = {uv: k for k, uv in enumerate(LATTICE)}
assert len(LATTICE) == 108 and len(LATTICE_INDEX) == 108


def signatures():
    """All 81 level combinations, index = sum(LEVELS.index(l) * 3^e)."""
    out = []
    for s in range(81):
        out.append([LEVELS[(s // 3 ** e) % 3] for e in range(4)])
    return out


def signature_index(levels):
    return sum(LEVELS.index(l) * 3 ** e for e, l in enumerate(levels))


def template(levels):
    """Triangles (block indices) of a signature, every one wound counter-clockwise in (u, v) (as pbd2gltf grid_indices)."""
    tris = []
    for strip in patch_strips(levels):
        for j in range(len(strip) - 2):
            a, b, c = strip[j], strip[j + 1], strip[j + 2]
            cross = (b[0] - a[0]) * (c[1] - a[1]) - (b[1] - a[1]) * (c[0] - a[0])
            if cross == 0:
                continue
            if cross < 0:
                b, c = c, b
            tris.append((LATTICE_INDEX[a], LATTICE_INDEX[b], LATTICE_INDEX[c]))
    return tris


MAX_TRIS = 98


if __name__ == "__main__":
    for lv in signatures():
        t = template(lv)
        area = sum(abs((LATTICE[b][0] - LATTICE[a][0]) * (LATTICE[c][1] - LATTICE[a][1])
                       - (LATTICE[b][1] - LATTICE[a][1]) * (LATTICE[c][0] - LATTICE[a][0])) for a, b, c in t) / 2
        assert area == 1 and len(t) <= MAX_TRIS, (lv, area, len(t))
    assert [len(template([n] * 4)) for n in LEVELS] == [18, 50, 98]
    print("terrain_lod: 81 templates cover the unit square, max", max(len(template(l)) for l in signatures()), "tris")
