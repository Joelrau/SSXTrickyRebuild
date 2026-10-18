"""Renderer "look" reference model: GS blend modes, the terrain lightmap pass, the draw-list sort key,
the sun flare / light halos (cLensFXObjNode) and the player indicator size.

Clean-room: behaviour only.
All colours are on the PS2 0..255 scale (texture/vertex 128 = 1.0, alpha 128 = 1.0).
"""
from __future__ import annotations

import math
from typing import List, Sequence, Tuple

# GS ALPHA modes (draw-state byte, 0..10): result = ((A - B) * C >> 7) + D, clamped 0..255 (COLCLAMP)
# A/B/D: 0 = Cs, 1 = Cd, 2 = 0; C: 0 = As, 1 = Ad, 2 = FIX.
ALPHA_MODES = {
    0: (2, 2, 2, 0),   # Cs (no blend)
    1: (2, 2, 2, 1),   # Cd (colour write has no effect)
    2: (1, 2, 0, 2),   # Cd * As
    3: (0, 1, 0, 1),   # (Cs - Cd) As + Cd   standard alpha blend
    4: (2, 1, 0, 1),   # Cd (1 - As)
    5: (0, 2, 0, 1),   # Cs As + Cd          additive
    6: (1, 0, 0, 2),   # (Cd - Cs) As        terrain lightmap pass (context 2)
    7: (2, 0, 0, 1),   # Cd - Cs As          subtractive (projected rider shadow on patches)
    8: (1, 2, 0, 1),   # Cd As + Cd
    9: (0, 1, 2, 1),   # (Cs - Cd) FIX + Cd
    10: (0, 2, 2, 1),  # Cs FIX + Cd
}


def gs_blend(mode: int, cs: float, cd: float, a_s: float, a_d: float = 0.0, fix: float = 0.0) -> float:
    """One colour channel through the GS alpha unit (integer >> 7 as the hardware does, clamped)."""
    a, b, c, d = ALPHA_MODES[mode]
    val = {0: cs, 1: cd, 2: 0.0}
    cc = {0: a_s, 1: a_d, 2: fix}[c]
    out = math.floor((val[a] - val[b]) * cc / 128.0) + val[d]
    return min(max(out, 0.0), 255.0)


LIGHTMAP_TILE = 1.0 / 16.0          # the VU table steps 0..1/16 in thirds (tess 4); w, h of the rect are not read


def lightmap_uv(rect: Sequence[float], u: float, v: float) -> Tuple[float, float]:
    """Lightmap page coordinate of patch parameter (u, v), with P(u, v) = sum C[4i+j] u^i v^j (pbd.py order).
    The tile is TRANSPOSED: page s follows v, page t follows u."""
    x, y = rect[0], rect[1]
    return x + v * LIGHTMAP_TILE, y + u * LIGHTMAP_TILE


def lightmap_region_clamp(rect: Sequence[float]) -> Tuple[int, int, int, int]:
    """CLAMP_2 REGION_CLAMP texel bounds (MINU, MAXU, MINV, MAXV) written per patch (128 x 128 page)."""
    minu, minv = int(rect[0] * 128.0), int(rect[1] * 128.0)
    span = int(rect[2] * 128.0) - 1            # the game uses w for both axes
    return minu, minu + span, minv, minv + span


def terrain_pixel(base_rgba: Sequence[float], lm_rgba: Sequence[float], dst_rgb: Sequence[float]) -> Tuple[float, ...]:
    """Final terrain colour: base texel (vertex colour 128 -> unmodulated) alpha-blended over dst (mode 3),
    then the lightmap texel applied with mode 6: clamp((C - lm.rgb) * lm.a / 128)."""
    c = [gs_blend(3, base_rgba[k], dst_rgb[k], base_rgba[3]) for k in range(3)]
    return tuple(gs_blend(6, lm_rgba[k], c[k], lm_rgba[3]) for k in range(3))


PIPELINE_GROUP = {1: 7, 2: 5, 3: 4, 4: 6, 5: 3, 6: 1, 7: 1, 10: 1, 8: 2, 9: 2}


def depth_value(view_depth: float, single_viewport: bool = True) -> int:
    """Value for sort mode 2 (material flag bit 18): trunc(w 1023 / 40000) clamped; split screen = 1."""
    if not single_viewport:
        return 1
    return min(max(int(view_depth * 1023.0 / 40000.0), 0), 1023)


def sort_key(layer: int, sort_mode: int, sort_value: int, pipeline: int, material: int, low: int) -> int:
    d = {0: 0x3FF, 1: 0x3FE, 2: sort_value}.get(sort_mode, 0)
    return ((0x13 - layer) << 27) | (d << 17) | (PIPELINE_GROUP.get(pipeline, 0) << 14) | (max(material, 0) << 5) \
        | max(low, 0)


def draw_order(keys: List[int]) -> List[int]:
    """Indices in draw order (stable radix sort on the 32-bit key, descending)."""
    return sorted(range(len(keys)), key=lambda i: -keys[i])


# rays of the sun glare fan: (angle rad, weight); .rodata table of 37 pairs
GLARE_RAYS = [(0.0, 0.2857), (0.2618, 0.4762), (0.4538, 0.7619), (0.5411, 0.5714), (0.6981, 0.2857),
              (0.9599, 0.4286), (1.1694, 0.5238), (1.3439, 0.8095), (1.4137, 0.7143), (1.5184, 0.381),
              (1.7453, 0.8095), (1.8151, 0.6667), (1.9373, 0.8571), (1.9897, 0.2381), (2.2689, 0.4286),
              (2.4784, 0.7143), (2.5831, 0.8095), (2.6529, 0.5714), (2.81, 0.2857), (3.0718, 0.1905),
              (3.3685, 0.3333), (3.6128, 0.7143), (3.7176, 0.619), (3.8572, 0.7143), (3.9619, 0.2381),
              (4.2411, 0.4286), (4.4506, 0.3333), (4.6949, 0.7143), (4.7997, 0.4286), (5.0091, 0.6667),
              (5.1313, 0.4762), (5.3233, 0.7619), (5.4105, 0.5238), (5.5851, 0.1905), (5.8818, 0.7619),
              (5.969, 0.5238), (6.1436, 0.619)]
HALO_CLASS = {0x10: (100.73963, 300.0), 0x20: (200.07841, 500.0), 0x40: (500.0, 800.0)}   # (radius, pull-in) cm
MAX_HALOS = 10


def sun_direction(azimuth_deg: float, elevation_deg: float) -> Tuple[float, float, float]:
    a, e = math.radians(azimuth_deg), math.radians(elevation_deg)
    return math.cos(a) * math.cos(e), math.sin(a) * math.cos(e), math.sin(e)


def sun_distance_and_size(dist: float, size: float, far: float) -> Tuple[float, float]:
    """Sun placed at the table distance, or at far - 1 with the size scaled by (far - 1) / dist."""
    if far < dist:
        return far - 1.0, size * (far - 1.0) / dist
    return dist, size


def visibility(z_block: Sequence[Sequence[int]], sun_z: int, inside: Sequence[Sequence[bool]]) -> float:
    """Occlusion test on the 16 x 16 (halo: 16 x 8) z-buffer block read back around the projected point.
    PS2 Z grows toward the camera; a pixel counts when its Z <= the point's Z (nothing nearer drawn)."""
    total = sum(len(r) for r in z_block)
    n_in = sum(1 for r in inside for b in r if b)
    vis = sum(1 for r, ri in zip(z_block, inside) for z, b in zip(r, ri) if b and z <= sun_z)
    if n_in == total:
        return vis / total
    half = total // 2
    return max((vis - half) / half, 0.0)


def ray_edge_falloff(d2: float) -> float:
    """Alpha factor of a glare-fan edge vertex at squared NDC distance d2 from the sun."""
    return (3.0 - min(d2, 3.0)) / 3.0


def halo_centre_weight(ndc_x: float, ndc_y: float) -> float:
    """k^4 with k = (sqrt 2 - r) / sqrt 2, r = NDC distance of the light from the screen centre."""
    r = math.hypot(ndc_x, ndc_y)
    return ((1.41422 - r) * 0.70710355) ** 4


def halo_sizes(radius: float, ndc_x: float, ndc_y: float, t34: float, t38: float) -> Tuple[float, float, float]:
    """World sizes of the three halo elements: core, ring, rotated streak (table 0x34 / 0x38)."""
    k4 = halo_centre_weight(ndc_x, ndc_y)
    return radius, radius * (t34 + k4 * 1.0095813), radius * (t38 + k4 * 1.2522545)


def halo_streak_angle(ndc_x: float) -> float:
    return -(ndc_x * math.pi * 0.5)


def halo_distance_fade(view_depth: float, max_depth: float) -> float:
    """1 up to half the table 0x5C distance, then linear to 0 at it (lights beyond are not collected)."""
    h = max_depth * 0.5
    return 1.0 if view_depth <= h else (h - (view_depth - h)) / h


def halo_colour(light_rgb: Sequence[float], intensity: float) -> Tuple[float, float, float]:
    """Normalised light colour (unit vector), x 128 in the packet."""
    c = [x * intensity for x in light_rgb]
    n = math.sqrt(sum(x * x for x in c))
    return tuple(c) if n == 0.0 else tuple(x / n for x in c)


def indicator_half_width(view_depth: float) -> float:
    if view_depth <= 0.0:
        return 76.48759
    if view_depth >= 31128.541:
        return 139.85631
    return view_depth / 31128.541 * 63.36872 + 76.48759


def selftest() -> int:
    bad = 0
    # lightmap: neutral alpha 128 with zero tint keeps the base; alpha 255 doubles (clamped)
    bad += terrain_pixel((100, 100, 100, 128), (0, 0, 0, 128), (0, 0, 0)) != (100, 100, 100)
    bad += terrain_pixel((100, 100, 100, 128), (0, 0, 0, 255), (0, 0, 0)) != (199, 199, 199)
    bad += terrain_pixel((100, 100, 100, 128), (20, 0, 0, 128), (0, 0, 0)) != (80, 100, 100)
    bad += lightmap_region_clamp((0.25, 0.5, 1 / 16, 1 / 16)) != (32, 39, 64, 71)
    bad += lightmap_uv((0.25, 0.5), 1.0, 0.0) != (0.25, 0.5 + 1 / 16)
    bad += depth_value(40000.0) != 1023 or depth_value(-5.0) != 0 or depth_value(9000.0, False) != 1
    keys = [sort_key(3, 2, 10, 5, 0, 0), sort_key(3, 2, 900, 5, 0, 0), sort_key(3, 0, 0, 3, 0, 0)]
    bad += draw_order(keys) != [2, 1, 0]                 # opaque first, then far-to-near
    bad += abs(indicator_half_width(15564.27) - 108.17) > 0.01
    bad += len(GLARE_RAYS) != 37
    print("look selftest:", "ALL OK" if not bad else f"{bad} FAILED")
    return bad


if __name__ == "__main__":
    raise SystemExit(selftest())
