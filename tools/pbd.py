"""Parser for SSX Tricky PS2 level geometry (.pbd)."""
import struct
from dataclasses import dataclass, field
from pathlib import Path

HEADER_COUNTS = (
    "player_starts", "patches", "instances", "particle_instances", "materials", "material_blocks",
    "lights", "splines", "spline_segments", "texture_flipbooks", "models", "particle_models",
    "textures", "cameras", "lightmap_size",
)
HEADER_OFFSETS = (
    "player_start", "patch", "instance", "particle_instance", "material", "material_block",
    "light", "spline", "spline_segment", "texture_flipbook", "model_pointer", "models",
    "particle_model_pointer", "particle_models", "camera_pointer", "cameras", "hash", "mesh_data",
)

PATCH_SIZE = 448
INSTANCE_SIZE = 256
SPLINE_SIZE = 40
SPLINE_SEGMENT_SIZE = 128


@dataclass
class Patch:
    lightmap_rect: tuple   # (x, y, w, h) inside the lightmap page, normalised
    uvs: list   # 4 corner UVs, (u, v)
    coeffs: list   # 16 power-basis coefficients (x, y, z), index 4*i + j
    bbox_min: tuple
    bbox_max: tuple
    corners: list
    surface_type: int
    visibility: int
    texture: int
    lightmap_id: int
    flag_word: int = 0   # +0x1AC, i32: < 0 = ShowOff patch, drawn and collided only in modes 3/5

    def evaluate(self, u: float, v: float) -> tuple:
        us = (1.0, u, u * u, u * u * u)
        vs = (1.0, v, v * v, v * v * v)
        x = y = z = 0.0
        for i in range(4):
            for j in range(4):
                w = us[i] * vs[j]
                c = self.coeffs[4 * i + j]
                x += c[0] * w
                y += c[1] * w
                z += c[2] * w
        return x, y, z

    def to_bezier(self) -> list:
        """Convert power-basis coefficients to 16 Bezier control points (row-major, 4*i + j)."""
        inv = ((1, 0, 0, 0), (1, 1 / 3, 0, 0), (1, 2 / 3, 1 / 3, 0), (1, 1, 1, 1))
        out = []
        for a in range(4):
            for b in range(4):
                p = [0.0, 0.0, 0.0]
                for i in range(4):
                    for j in range(4):
                        w = inv[a][i] * inv[b][j]
                        if w:
                            c = self.coeffs[4 * i + j]
                            p[0] += c[0] * w
                            p[1] += c[1] * w
                            p[2] += c[2] * w
                out.append(tuple(p))
        return out


@dataclass
class Instance:
    matrix: list                  # 16 floats, row-major, row-vector convention (translation in [12:15])
    model_id: int
    bbox_min: tuple
    bbox_max: tuple


@dataclass
class SplineSegment:
    """128-byte spline segment record."""
    coeffs: list   # 4 (x, y, z): P(t) = A t^3 + B t^2 + C t + D, stored [A, B, C, D]
    dist_poly: tuple   # t(s') = ((a s' + b) s' + c) s' + d, s' = (s - start_dist) / 100
    prev: int   # previous segment index or -1
    next: int   # next segment index or -1
    spline: int
    bbox_min: tuple
    bbox_max: tuple
    length: float                 # cm
    start_dist: float             # cm from the spline start


@dataclass
class Spline:
    bbox_min: tuple
    bbox_max: tuple
    segment_count: int
    first_segment: int
    segments: list = field(default_factory=list)   # each: 4 power-basis coefficients (x, y, z), lowest power first


@dataclass
class PBD:
    counts: dict
    offsets: dict
    patches: list
    instances: list
    splines: list
    model_pointers: list
    raw: bytes
    spline_segments: list = field(default_factory=list)


def _vec(data, off, n):
    return struct.unpack_from(f"<{n}f", data, off)


def load(path) -> PBD:
    data = Path(path).read_bytes()
    pos = 4
    counts = dict(zip(HEADER_COUNTS, struct.unpack_from(f"<{len(HEADER_COUNTS)}i", data, pos)))
    pos += 4 * len(HEADER_COUNTS)
    offsets = dict(zip(HEADER_OFFSETS, struct.unpack_from(f"<{len(HEADER_OFFSETS)}i", data, pos)))

    patches = []
    for k in range(counts["patches"]):
        o = offsets["patch"] + k * PATCH_SIZE
        lightmap = _vec(data, o, 4)
        uvs = [_vec(data, o + 16 + 16 * n, 2) for n in range(4)]
        coeffs = [None] * 16
        for n in range(16):
            coeffs[15 - n] = _vec(data, o + 80 + 16 * n, 3)   # stored highest power first
        bbox_min = _vec(data, o + 336, 3)
        bbox_max = _vec(data, o + 348, 3)
        corners = [_vec(data, o + 360 + 16 * n, 3) for n in range(4)]
        surface_type, _lo, vis, tex, lm = struct.unpack_from("<Ihhhh", data, o + 424)
        flag_word = struct.unpack_from("<i", data, o + 0x1AC)[0]
        patches.append(Patch(lightmap, uvs, coeffs, bbox_min, bbox_max, corners, surface_type, vis, tex, lm, flag_word))

    instances = []
    for k in range(counts["instances"]):
        o = offsets["instance"] + k * INSTANCE_SIZE
        matrix = list(_vec(data, o, 16))
        model_id = struct.unpack_from("<i", data, o + 192)[0]
        bbox_min = _vec(data, o + 204, 3)
        bbox_max = _vec(data, o + 216, 3)
        instances.append(Instance(matrix, model_id, bbox_min, bbox_max))

    segments = []
    for s in range(counts["spline_segments"]):
        so = offsets["spline_segment"] + s * SPLINE_SEGMENT_SIZE
        rows = [_vec(data, so + 16 * n, 3) for n in range(4)]   # rows multiply t^3, t^2, t, 1
        poly = _vec(data, so + 0x40, 4)
        prev, nxt, owner = struct.unpack_from("<3i", data, so + 0x50)
        length, start = _vec(data, so + 0x74, 2)
        segments.append(SplineSegment(rows, poly, prev, nxt, owner, _vec(data, so + 0x5C, 3),
                                      _vec(data, so + 0x68, 3), length, start))

    splines = []
    for k in range(counts["splines"]):
        o = offsets["spline"] + k * SPLINE_SIZE
        bmin, bmax = _vec(data, o, 3), _vec(data, o + 12, 3)
        _u1, seg_count, seg_first, _u2 = struct.unpack_from("<4i", data, o + 24)
        sp = Spline(bmin, bmax, seg_count, seg_first)
        for s in range(seg_first, seg_first + seg_count):
            sp.segments.append(segments[s].coeffs[::-1])
        splines.append(sp)

    model_pointers = list(struct.unpack_from(f"<{counts['models']}i", data, offsets["model_pointer"]))
    return PBD(counts, offsets, patches, instances, splines, model_pointers, data, segments)
