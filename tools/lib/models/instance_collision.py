"""Prop-instance collision reference model.
Narrow-phase helpers come from models/collision.py.

Which query sees which instance:
  * main world query (main query: ground probe, landing ray, camera, WallCollision): patches + instances with flag
    0x20 and surface != -1;
  * PropCollision query (prop query, `prop_query`): only instances, flag 0x20 and surface == -1, coarse (first hit),
    one-sided triangle variant, no normal filter.  This is the ONLY query that sees unsurfaced props;
    WallCollision never does (that is why riders show no wall bounce on them).

Narrow phase per instance (instance narrow phase), per model node in array order:
  * node matrix = local (pbd node +0x14 offset, identity if none) x parent's composed matrix, then x instance
    matrix (row vectors); nodes with an animation flag (pbd node +0x10 != 0) take the bone matrix of the
    instance's animation node instead when that controller's type id has bit 0x100 (animation nodes
    0x100..0x103) -- not modelled here (no such controller on Garibaldi, RAM-checked);
  * only nodes with geometry (pbd node +0x04 != 0) are tested; the k-th such node uses the k-th entry of the
    record's collision model (mesh list / sphere-tree list); data-checked on all 12 tracks;
  * mesh: two-sided if the node's LOD header +0x1C bit 0 is set (slot 6: normal flipped to the sphere side), else
    slot 5 (prop query: one-sided variant, 0 <= d <= R for the bounding sphere);
  * box: node LOD header AABB; sphere-in-box with the BOUNDING sphere only (the sub-sphere loop re-tests the
    bounding sphere, box slot);
  * sphere tree: octree of occupancy bytes (RLE-packed), see SphereTree; query = bounding sphere, then every
    sub-sphere in index order IGNORING the mask, first hit wins.
Hit selection: |t - t_ref| per node (strict <), first hit if t_ref < 0.  Inside one mesh the running reference
is the previous HIT's t, not the best one (mesh loop quirk, `_mesh_query`).

Visit order (`ltg_order`): cells ix-1..ix+1 (outer) x iy-1..iy+1 (inner) around the query AABB centre, clamped;
cell then sub-cell AABB tests (strict); sub-cells 0..15 in storage order; the instance list (normal count, or
the alternate count in modes 3/5); no de-duplication.  The main query visits each sub-cell's patches before its
instances (not modelled here).

Not modelled: animated node matrices (above), the instance's controller cube AABB (prop query, controller type
bit 0x100), dynamic objects in the rider's neighbour list.
"""
import struct
from dataclasses import dataclass, field
from pathlib import Path
from typing import List, Optional

import numpy as np

import collision as col

COLLIDABLE = 0x20
REACTS = 0x80
TYPE_MESH, TYPE_BOX, TYPE_SPHERE_TREE = 1, 2, 3
PROP_PUSH_SCALE = 1.1            # position += 1.1 depth n
PROP_MIN_BOUNCE = 55.555553      # cm/s
PROP_RUMBLE_SCALE = 0.0012       # rider slot 13 (pad rumble) amount = a * this (0x125BC0 float 0x3A9D4952)
MOTION_MASK = {1: -1, 2: -1, 3: 0x003, 5: 0xFC3, 6: 0xFC3}   # sphere mask live when PropCollision runs


def _ssf(s):
    h = struct.unpack_from("<19I", s, 0)
    return dict(n_tree=h[5], p_tree=h[6], n_cm=h[7], p_cm=h[8], n_irec=h[13], p_irec=h[14], n_inst=h[15],
                p_iidx=h[16])


def _meshes(s, off, size, count):
    """Mesh = u32 tris, u32 verts, u32 index padding bytes, tris x 3 u32, padding, verts x f32[4], tris x f32[4]
    face normals (.ssf loader builds {tris, verts, idx, vert, normal} descriptors from exactly this)."""
    out, p = [], off
    for _ in range(count):
        nt, nv, pad = struct.unpack_from("<3I", s, p)
        tris = np.frombuffer(s, "<i4", nt * 3, p + 12).reshape(-1, 3)
        pv = p + 12 + nt * 12 + pad
        v = np.frombuffer(s, "<f4", nv * 4, pv).reshape(-1, 4)[:, :3].astype(float)
        n = np.frombuffer(s, "<f4", nt * 4, pv + nv * 16).reshape(-1, 4)[:, :3].astype(float)
        out.append((tris, v, n))
        p = pv + nv * 16 + nt * 16
    assert p - off == size, "collision model size mismatch"
    return out


def rle_unpack(b, p):
    """RLE unpack: signed count byte c; c < 0 copies -c literal bytes, c > 0 repeats the next byte c + 1 times,
    c = 0 ends.  Returns (bytes, packed length incl. the terminator)."""
    out, q = bytearray(), p
    while True:
        c = struct.unpack_from("<b", b, q)[0]
        q += 1
        if c == 0:
            return bytes(out), q - p
        if c < 0:
            out += b[q:q - c]
            q -= c
        else:
            out += bytes([b[q]]) * (c + 1)
            q += 1


OCTANTS = np.array([[-1, -1, -1], [-1, -1, 1], [-1, 1, -1], [-1, 1, 1],
                    [1, -1, -1], [1, -1, 1], [1, 1, -1], [1, 1, 1]], float)   # RAM octant table: bit2 x, bit1 y, bit0 z


@dataclass
class SphereTree:
    """One node's sphere tree (.ssf header +0x14 count / +0x18 -> {offset, size, tree count} entries).  Node space."""
    depth: int                    # deepest level index (header +0x0C)
    center: np.ndarray            # root centre (header +0x10)
    levels: list                  # per level (radius, child offset, index stride = 8^level)
    occ: bytes                    # occupancy bytes, sum(8^l) long, root at index 0

    def query_sphere(self, q, r):
        """tree walk from the root.  Returns (push vector, contact) or None; depth-first, octants 0..7, first hit."""
        return self._visit(np.asarray(q, float), float(r), self.center, 0, 0)

    def _visit(self, q, r, c, level, idx):
        d = q - c
        f = r + self.levels[level][0]
        dd = float(d @ d)
        if not dd < f * f:
            return None
        bits = self.occ[idx]
        if level < self.depth and bits:
            off = self.levels[level + 1][1]
            stride = self.levels[level][2]
            for k in range(8):
                if (bits >> k) & 1:
                    res = self._visit(q, r, c + OCTANTS[k] * off, level + 1, (k + 1) * stride + idx)
                    if res is not None:
                        return res
            return None
        dist = np.sqrt(dd)
        n = d / dist if dist > 0.0 else np.array([0.0, 0.0, 1.0])   # TODO: the game divides by 0 here
        depth = f - dist
        return n * depth, q - n * (r - depth)

    def leaves(self):
        """All solid spheres (centre, radius) -- for drawing/debugging, not used by queries."""
        out = []

        def rec(c, level, idx):
            bits = self.occ[idx]
            if level < self.depth and bits:
                for k in range(8):
                    if (bits >> k) & 1:
                        rec(c + OCTANTS[k] * self.levels[level + 1][1], level + 1, (k + 1) * self.levels[level][2] + idx)
            else:
                out.append((c, self.levels[level][0]))
        rec(self.center, 0, 0)
        return out


def _trees(s, off, size, count):
    out, q = [], off
    for _ in range(count):
        packed, unpacked_size_hint, compressed, depth = struct.unpack_from("<4i", s, q)
        c = np.array(struct.unpack_from("<3f", s, q + 0x10), float)
        levels = [struct.unpack_from("<ffi", s, q + 0x70 + 12 * i) for i in range(depth + 1)]
        p = q + 0x70 + 12 * (depth + 1)
        if compressed:
            occ, used = rle_unpack(s, p)
            assert used == unpacked_size_hint, "packed length mismatch"
        else:
            occ = s[p:p + sum(8 ** i for i in range(depth + 1))]   # TODO: no uncompressed tree in the retail data
        assert len(occ) == sum(8 ** i for i in range(depth + 1))
        out.append(SphereTree(depth, c, levels, occ))
        q = p + packed + unpacked_size_hint
    assert q - off == size, "sphere tree size mismatch"
    return out


def _ltg(b):
    """Cells as dicts: aabb, subs = [(aabb, n_patch, n_norm, n_alt, [instance indices])]."""
    nx, ny, ncell = struct.unpack_from("<3i", b, 0x2C)
    sub_n, table = struct.unpack_from("<2i", b, 0x48)
    cells = []
    for co in struct.unpack_from(f"<{ncell}I", b, table):
        if not co:
            cells.append(None)
            continue
        cell_box = struct.unpack_from("<6f", b, co)
        sub0 = co + struct.unpack_from("<I", b, co + 0x38)[0]
        subs = []
        for k in range(sub_n):
            so = sub0 + 76 * k
            box = struct.unpack_from("<6f", b, so)
            n_p, n_norm, n_alt, n_r, n_x, n_l = struct.unpack_from("<6H", b, so + 0x24)
            p = struct.unpack_from("<I", b, so + 0x38)[0]
            lst = list(struct.unpack_from(f"<{max(n_norm, n_alt)}i", b, co + p)) if p and max(n_norm, n_alt) else []
            empty = n_p == 0 and n_norm == 0 and n_alt == 0 and n_r == 0 and n_x == 0 and n_l == 0
            subs.append((box, n_p, n_norm, n_alt, lst, empty))
        cells.append(dict(box=cell_box, subs=subs))
    hdr = dict(min=struct.unpack_from("<3f", b, 4), cell=struct.unpack_from("<f", b, 0x28)[0], nx=nx, ny=ny)
    return hdr, cells


def _ltg_sets(cells):
    normal, alt = set(), set()
    for cell in cells:
        if cell:
            for _box, _np, n_norm, n_alt, lst, _e in cell["subs"]:
                normal.update(lst[:n_norm])
                alt.update(lst[n_norm:n_alt])
    assert not (normal & alt), "instance in both .ltg roles"
    return normal, alt


@dataclass
class Node:
    kind: int                       # TYPE_MESH / TYPE_BOX / TYPE_SPHERE_TREE
    M: np.ndarray                   # node -> game space (row vectors, p' = p M)
    inv: np.ndarray
    two_sided: bool = False         # pbd node LOD header +0x1C bit 0 (meshes)
    tris: list = field(default_factory=list)     # (tri (3,3) node space, unit normal) -- meshes
    box: tuple = None               # (min, max) node space
    tree: SphereTree = None
    animated: bool = False          # pbd node +0x10 != 0


@dataclass
class Instance:
    index: int
    flags: int                    # raw .ssf flag word (high half = initial state)
    surface: int
    mtype: int
    alternate_only: bool          # listed only past the normal count (modes 3/5)
    lo: np.ndarray                # pbd AABB
    hi: np.ndarray
    mass: float = 0.0             # .ssf record +0x00: 0 = bump only, else push + bounce (value used by Roller only)
    restitution: float = 0.5      # .ssf record +0x04
    nodes: List[Node] = field(default_factory=list)
    tris: List[tuple] = field(default_factory=list)    # game-space copies: (tri, unit normal)
    boxes: List[tuple] = field(default_factory=list)   # (node->game 4x4, min, max)

    @property
    def reacts(self):
        return bool((self.flags >> 16) & REACTS)


def _apply(M, p):
    return np.append(p, 1.0) @ M


def load_track(stem: Path, keep_ltg=False):
    """Collidable instances of <stem>.pbd/.ssf/.ltg.  With keep_ltg also returns (ltg header, cells)."""
    s = stem.with_suffix(".ssf").read_bytes()
    d = stem.with_suffix(".pbd").read_bytes()
    counts = struct.unpack_from("<15i", d, 4)
    offs = struct.unpack_from("<18i", d, 64)
    n_inst, p_inst, p_mptr, p_models = counts[2], offs[2], offs[10], offs[11]
    h = _ssf(s)
    recs = [struct.unpack_from("<ffIi4h", s, h["p_irec"] + 24 * k) for k in range(h["n_irec"])]
    idx = struct.unpack_from(f"<{h['n_inst']}i", s, h["p_iidx"])
    assert len(idx) == n_inst
    cms = [_meshes(s, *struct.unpack_from("<3i", s, h["p_cm"] + 12 * k)) for k in range(h["n_cm"])]
    trees = [_trees(s, *struct.unpack_from("<3i", s, h["p_tree"] + 12 * k)) for k in range(h["n_tree"])]
    ltg_hdr, cells = _ltg(stem.with_suffix(".ltg").read_bytes())
    normal, alt = _ltg_sets(cells)
    out = []
    for i in range(n_inst):
        r = recs[idx[i]]
        if not (r[2] >> 16) & COLLIDABLE:
            continue
        o = p_inst + 256 * i
        M = np.array(struct.unpack_from("<16f", d, o), float).reshape(4, 4)
        model = struct.unpack_from("<i", d, o + 192)[0]
        inst = Instance(i, r[2], r[3], r[4], i in alt and i not in normal,
                        np.array(struct.unpack_from("<3f", d, o + 204)), np.array(struct.unpack_from("<3f", d, o + 216)),
                        r[0], r[1])
        if r[4] in (TYPE_MESH, TYPE_BOX, TYPE_SPHERE_TREE):
            m = p_models + struct.unpack_from("<i", d, p_mptr + 4 * model)[0]
            _t, n_obj, obj_off, _mb = struct.unpack_from("<4i", d, m)
            objs = []
            for k in range(n_obj):
                ob = m + obj_off + 24 * k
                f = struct.unpack_from("<6i", d, ob)
                mat = np.array(struct.unpack_from("<16f", d, ob + f[5]), float).reshape(4, 4) if f[5] != -1 else np.eye(4)
                objs.append((f, ob, mat))
            chain = []
            for k, (f, ob, mat) in enumerate(objs):        # parents precede children (instance narrow phase reuses them)
                par = f[0]
                chain.append(mat @ chain[par] if 0 <= par < k else mat)
            geo = 0
            for k, (f, ob, _mat) in enumerate(objs):
                if f[1] == 0:
                    continue
                lod = ob + f[1]
                Mw = chain[k] @ M
                node = Node(r[4], Mw, np.linalg.inv(Mw), animated=f[4] != 0)
                if r[4] == TYPE_MESH:
                    node.two_sided = bool(struct.unpack_from("<I", d, lod + 0x1C)[0] & 1)
                    tris, v, n = cms[r[5]][geo]
                    for t_i, t in enumerate(tris):
                        nn = n[t_i]
                        if np.linalg.norm(nn) > 0.0:   # a few stored normals are zero: such triangles never collide
                            node.tris.append((v[t], nn / np.linalg.norm(nn)))
                    lin_it = np.linalg.inv(Mw[:3, :3])
                    for tri, nn in node.tris:
                        vw = np.c_[tri, np.ones(3)] @ Mw
                        nw = lin_it @ nn
                        inst.tris.append((vw[:, :3], nw / np.linalg.norm(nw)))
                elif r[4] == TYPE_BOX:
                    node.box = (np.array(struct.unpack_from("<3f", d, lod + 4)), np.array(struct.unpack_from("<3f", d, lod + 16)))
                    inst.boxes.append((Mw, *node.box))
                else:
                    node.tree = trees[r[5]][geo]
                geo += 1
                inst.nodes.append(node)
        out.append(inst)
    if keep_ltg:
        return out, (ltg_hdr, cells)
    return out


def _to_node(shape: col.SphereSet, inv):
    """Slot 1: copy the shape and transform every centre into node space (radii unchanged, mask kept)."""
    return col.SphereSet(_apply(inv, np.asarray(shape.center, float))[:3], shape.radius,
                         [(_apply(inv, np.asarray(c, float))[:3], r) for c, r in shape.subs], shape.mask)


def _subs_used(shape):
    return [s for k, s in enumerate(shape.subs) if (shape.mask >> k) & 1]


def sphere_set_triangle_variant(shape: col.SphereSet, tri, n, variant):
    """Boarder triangle tests: 'abs' variant abs (|d| <= R), 'one' variant one (0 <= d <= R; prop query),
    'two' variant two (two-sided mesh: n flipped to the bounding sphere's side).  Sub-spheres: d_i < r_i, smallest
    d_i.  Returns (push, contact) or None."""
    d = float(np.dot(shape.center - tri[0], n))
    if variant == "one":
        if d < 0.0 or d > shape.radius:
            return None
    elif abs(d) > shape.radius:
        return None
    if variant == "two" and d < 0.0:
        n, d = -n, -d
    if not col._sphere_touches(shape.center, shape.radius, tri, n, d):
        return None
    if not shape.subs:
        return n * (shape.radius - d), shape.center - d * n
    best = None
    for c, r in _subs_used(shape):
        di = float(np.dot(c - tri[0], n))
        if di < r and col._sphere_touches(c, r, tri, n, di) and (best is None or di < best[0]):
            best = (di, n * (r - di), c - di * n)
    return None if best is None else (best[1], best[2])


def _mesh_query(shape, node: Node, variant, t_ref):
    """mesh loop over one node's triangles.  Quirk: the comparison reference is the previous hit's t, not the
    stored best (the stored result changes only when a hit beats its predecessor)."""
    last, best = 1e30, None
    v = "two" if node.two_sided else variant
    for k, (tri, n) in enumerate(node.tris):
        res = sphere_set_triangle_variant(shape, tri, n, v)
        if res is None:
            continue
        t = float(np.linalg.norm(res[0]))
        if abs(t - t_ref) < abs(last - t_ref):
            best = (t, res[0] / t if t > 0 else n, res[1], k)
            if t_ref < 0.0:
                return best
        last = t
    return best


def sphere_box(center, r, lo, hi):
    """sphere-box: for axis x, y, z: if the centre's other two coordinates lie inside [lo, hi] (closed), the face
    depths are (c - lo + r) and (hi - c + r); both must be >= 0; the smallest depth over all faces wins (strict <,
    order: -x, +x, -y, +y, -z, +z).  Returns (depth, unit normal, contact) or None."""
    best = None
    for a in range(3):
        o1, o2 = (a + 1) % 3, (a + 2) % 3
        if not (lo[o1] <= center[o1] <= hi[o1] and lo[o2] <= center[o2] <= hi[o2]):
            continue
        dlo = center[a] - lo[a] + r
        dhi = hi[a] - center[a] + r
        if dlo < 0.0 or dhi < 0.0:
            continue
        for depth, sign, face in ((dlo, -1.0, lo[a]), (dhi, 1.0, hi[a])):
            if best is None or depth < best[0]:
                nrm = np.zeros(3)
                nrm[a] = sign
                pt = np.array(center, float)
                pt[a] = face
                best = (depth, nrm, pt)
    return best


def _box_query(shape, node: Node):
    """Slot 8 / box slot: the bounding sphere only.  With a non-zero mask the loop re-tests the bounding sphere once
    per enabled sub-sphere, so the result is the same, but no hit at all if no enabled bit is below the count."""
    if shape.mask != 0 and not any((shape.mask >> k) & 1 for k in range(len(shape.subs))):
        return None   # also with no sub-spheres at all and a non-zero mask (the loop never runs)
    res = sphere_box(np.asarray(shape.center, float), shape.radius, *node.box)
    if res is None or res[0] <= 0.0:   # TODO: depth 0 gives a zero push; the game normalises it (NaN)
        return None
    return res[0], res[1], res[2], 0


def _tree_query(shape, node: Node):
    """Slot 7 / tree sphere-set test: bounding sphere must hit; then (if there are sub-spheres) the first sub-sphere in index
    order that hits, mask ignored."""
    res = node.tree.query_sphere(shape.center, shape.radius)
    if res is None:
        return None
    if shape.subs:
        res = None
        for c, r in shape.subs:
            res = node.tree.query_sphere(c, r)
            if res is not None:
                break
        if res is None:
            return None
    push, contact = res
    t = float(np.linalg.norm(push))
    if t <= 0.0:
        return None
    return t, push / t, contact, 0


def instance_query(inst: Instance, shape: col.SphereSet, variant="abs", t_ref=-1.0):
    """instance narrow phase for a sphere set.  Returns col.Hit (game space) or None."""
    best = None
    for k, node in enumerate(inst.nodes):
        ls = _to_node(shape, node.inv)
        if node.kind == TYPE_MESH:
            res = _mesh_query(ls, node, variant, t_ref)
        elif node.kind == TYPE_BOX:
            res = _box_query(ls, node)
        else:
            res = _tree_query(ls, node)
        if res is None:
            continue
        t, n_l, p_l, sub = res
        if best is not None and not abs(t - t_ref) < abs(best.t - t_ref):
            continue
        nw = n_l @ node.M[:3, :3]   # the game maps the normal like a direction and does not renormalise it
        kind = 1 if inst.surface == -1 else (2 if node.animated else 0)
        best = col.Hit(kind, _apply(node.M, p_l)[:3], nw, inst.surface, t, None, (inst.index, k, sub), t_ref >= 0)
        if t_ref < 0.0:
            return best
    return best


def ltg_order(hdr, cells, lo, hi, alternate_mode=False):
    """Instance indices in the game's visit order for a query AABB [lo, hi] (prop query / main query).  Duplicates kept."""
    c = (np.asarray(lo) + np.asarray(hi)) * 0.5
    size = hdr["cell"]
    ix = int((c[0] - hdr["min"][0] + size) / size)       # truncation as in the game (= floor + 1 inside the grid)
    iy = int((c[1] - hdr["min"][1] + size) / size)
    for i in range(max(ix - 2, 0), min(max(ix, 0), hdr["nx"] - 1) + 1):
        for j in range(max(iy - 2, 0), min(max(iy, 0), hdr["ny"] - 1) + 1):
            cell = cells[j * hdr["nx"] + i]
            if cell is None or not _overlap(cell["box"], lo, hi):
                continue
            for box, _np, n_norm, n_alt, lst, empty in cell["subs"]:
                if empty or not _overlap(box, lo, hi):
                    continue
                yield from lst[:(n_alt if alternate_mode else n_norm)]


def _overlap(box, lo, hi):
    return all(box[a] < hi[a] and lo[a] < box[3 + a] for a in range(3))


class InstanceWorld:
    def __init__(self, instances: List[Instance], alternate_mode=False, ltg=None):
        self.instances = instances
        self.by_index = {i.index: i for i in instances}
        self.alternate_mode = alternate_mode
        self.ltg = ltg                        # (hdr, cells) from load_track(keep_ltg=True): exact visit order
        self._lo = np.array([i.lo for i in instances]).reshape(-1, 3)
        self._hi = np.array([i.hi for i in instances]).reshape(-1, 3)

    def _candidates(self, lo, hi, surfaced):
        if self.ltg is not None:
            seq = (self.by_index[k] for k in ltg_order(*self.ltg, lo, hi, self.alternate_mode) if k in self.by_index)
        else:   # index order (differs from the game only for first-hit ties between instances)
            hits = np.nonzero(np.all(self._lo <= hi, axis=1) & np.all(lo <= self._hi, axis=1))[0]
            seq = (self.instances[k] for k in hits if self.alternate_mode or not self.instances[k].alternate_only)
        for inst in seq:
            if (inst.surface != -1) != surfaced:
                continue
            if not (np.all(inst.lo < hi) and np.all(lo < inst.hi)):
                continue
            yield inst

    def line_query(self, a, b, t_ref=col.PROBE_T_REF) -> Optional[col.Hit]:
        """Surfaced instances only; line slot 5 is two-sided; per-mesh mesh loop quirk applies."""
        a, b = np.asarray(a, float), np.asarray(b, float)
        d = b - a
        best = None
        for inst in self._candidates(np.minimum(a, b), np.maximum(a, b), True):
            ib = None
            for k, node in enumerate(inst.nodes):
                if node.kind != TYPE_MESH:
                    continue      # TODO: line vs box / sphere tree slots not exported (no surfaced non-mesh prop)
                la, lb = _apply(node.inv, a)[:3], _apply(node.inv, b)[:3]
                last, nb = 1e30, None
                for j, (tri, n) in enumerate(node.tris):
                    t = col.segment_triangle(la, lb - la, tri, n)
                    if t is None:
                        continue
                    if abs(t - t_ref) < abs(last - t_ref):
                        nb = (t, n, j)
                        if t_ref < 0:
                            break
                    last = t
                if nb is None or (ib is not None and not abs(nb[0] - t_ref) < abs(ib.t - t_ref)):
                    continue
                nw = nb[1] @ node.M[:3, :3]   # as instance_query (only 27 scaled instances in the retail data)
                ib = col.Hit(0, a + nb[0] * d, nw, inst.surface, nb[0], None, (inst.index, nb[2], k))
                if t_ref < 0:
                    break
            if ib is None:
                continue
            if t_ref < 0:
                return ib
            if best is None or abs(ib.t - t_ref) < abs(best.t - t_ref):
                best = ib
        return best

    def sphere_query(self, shape: col.SphereSet, fine=False, filter_dir=None, surfaced=False,
                     variant="abs") -> Optional[col.Hit]:
        """Generic boarder query over instances (the game's main query uses surfaced=True; unsurfaced props are seen
        only by prop_query).  The filter is applied to each instance's single best hit (main query)."""
        c = np.asarray(shape.center, float)
        lo, hi = c - shape.radius, c + shape.radius
        t_ref = 2.0 * shape.radius if fine else -1.0
        best = None
        for inst in self._candidates(lo, hi, surfaced):
            hit = instance_query(inst, shape, variant, t_ref)
            if hit is None:
                continue
            if best is not None and not abs(hit.t - t_ref) < abs(best.t - t_ref):
                continue
            if filter_dir is not None and np.dot(hit.normal, filter_dir) > col.WALL_FILTER_DOT:
                continue
            best = hit
            if t_ref < 0:
                return best
        return best

    def prop_query(self, shape: col.SphereSet) -> Optional[col.Hit]:
        """PropCollision's query (prop query): unsurfaced collidable instances, coarse (first hit in visit order),
        one-sided triangle variant, no filter.  Pass the shape with the motion's live mask (MOTION_MASK)."""
        c = np.asarray(shape.center, float)
        lo, hi = c - shape.radius, c + shape.radius
        for inst in self._candidates(lo, hi, False):
            hit = instance_query(inst, shape, "one", -1.0)
            if hit is not None:
                return hit
        return None


@dataclass
class PropContact:
    """Result of prop_response.  dp/v are the rider's new position delta and velocity; flags say what fired."""
    dp: np.ndarray
    v: np.ndarray
    normal: np.ndarray
    approach: float
    touch_effect: bool            # handler touch handler: instance effect slot 1 script (or controller slot 18)
    reaction: str                 # 'none' (no 0x80), 'bump' (mass 0), 'bounce' (immovable), 'movable', 'skipped'
    impulse: float = 0.0          # dv along n (the value passed to the impact handler 0x126540)
    rumble: float = 0.0


def prop_response(inst: Instance, hit: col.Hit, v, motion, control, ground_normal, rider_mass=1.0,
                  movable_controller=False):
    """PropCollision (first branch) + prop reaction.  `movable_controller` = the instance has a controller whose slot 10
    returns 0 (type id 0: Roller); otherwise the immovable branch is used."""
    v = np.asarray(v, float).copy()
    n = np.asarray(hit.normal, float)
    if motion == 2:                                   # Ground: normal made tangent to the ground
        g = np.asarray(ground_normal, float)
        n = n - np.dot(n, g) * g
        n = n / np.linalg.norm(n)
    approach = float(-np.dot(n, v))                   # contact record +0x30
    out = PropContact(np.zeros(3), v, n, approach, True, "none")
    if not inst.reacts:
        return out
    if control == 0x16:                               # ResetWait
        out.reaction = "skipped"
        return out
    if inst.mass == 0.0:
        out.reaction = "bump"                         # bump reaction(n, no boost loss): bump animation in Cruise/Prewind
        return out
    e = inst.restitution
    a = float(-np.dot(n, v))
    if not movable_controller:
        dp = PROP_PUSH_SCALE * hit.t * n
        if motion == 2 and np.dot(dp, ground_normal) < 0.0:   # push helper
            g = np.asarray(ground_normal, float)
            dp = dp - np.dot(dp, g) * g
        dv = max(a * e + a, a + PROP_MIN_BOUNCE)
        out.dp, out.reaction = dp, "bounce"
    else:
        k = 1.0 / (rider_mass / inst.mass + 1.0)
        av = a * k
        dv = np.sqrt(max(av * av - (1.0 - e) * k, 0.0)) - av
        out.reaction = "movable"
    out.rumble = a * PROP_RUMBLE_SCALE
    out.v = v + n * dv
    out.impulse = dv
    return out
