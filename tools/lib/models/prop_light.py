"""Prop (instance) vertex lighting, texture LOD and frame clear colour.

Clean-room reference model written from the behaviour spec; no game code.

Prop lighting [instance draw loop, renderer upload slot 85, VU1 image 5 lighting subroutine;
data: .pbd instance bytes 64..191]:
every instance carries its own baked lighting block (3 directional lights + ambient, already in the
instance's MODEL space, colours on the PS2 0..255 scale where 128 = 1.0). The renderer uploads it
unchanged with the instance matrix; the VU lights each vertex from its stored normal:

    c = min(ambient + sum_i colour_i * max(0, n . dir_i), 255)      per channel, incl. alpha
    displayed colour = texture * c / 128                            (PS2 texture modulate)

`ambient.w` is 128 in the data (alpha 1.0); light colours have w = 0. Prop meshes have no vertex
colours, so this is the whole lighting; no per-track ambient, no .pbd light walk and no rider-style
scale is applied at run time (RAM: the blocks are byte-identical to the file).

Texture LOD [LOD triple builder and TEX1 packing]: the per-track table
fields 0x68..0x78 are the GS TEX1 mipmap-LOD controls of every mipmapped
texture: LCM = 1 - enabled, L = shift s, K = trunc(-16 * 2^s * log2(D)) (1/16 units), MMAG linear,
MMIN linear-mipmap-linear, MXL = the texture's own max level. GS LOD (GS manual formula) with
Q = 1/w: LOD = 2^L * log2(w) + K/16 = 2^s * log2(w / D): base level up to view depth D.

Clear colour [renderer slot 13; ram]: table 0x48 RGB -> race manager +0x5C
(as /255) -> renderer +0x1724 (bytes, x255) -> full-screen sprite RGBAQ (R, G, B, 0x80) drawn first.
"""
from __future__ import annotations

import math
import struct
from dataclasses import dataclass
from typing import Sequence, Tuple

INSTANCE_SIZE = 256
BLOCK_OFFSET = 64          # bytes 64..191 of the .pbd instance record (= RAM instance +0x40)


@dataclass
class PropLightBlock:
    dirs: Tuple[Tuple[float, float, float], ...]      # 3 unit vectors toward the light, model space
    colours: Tuple[Tuple[float, float, float, float], ...]  # 3 x RGBA, 0..255 scale (w = 0)
    ambient: Tuple[float, float, float, float]        # RGBA, 0..255 scale (w = 128)

    @staticmethod
    def from_instance(rec: bytes) -> "PropLightBlock":
        """rec = one 256-byte .pbd instance record. +0x40: 3 rows (x, y, z components of the three
        directions, i.e. the directions are the COLUMNS), +0x70 unused (0,0,0,1, not uploaded),
        +0x80/+0x90/+0xA0 light colours, +0xB0 ambient."""
        rows = [struct.unpack_from("<4f", rec, BLOCK_OFFSET + 16 * k) for k in range(3)]
        dirs = tuple((rows[0][i], rows[1][i], rows[2][i]) for i in range(3))
        cols = tuple(struct.unpack_from("<4f", rec, 0x80 + 16 * i) for i in range(3))
        amb = struct.unpack_from("<4f", rec, 0xB0)
        return PropLightBlock(dirs, cols, amb)


def vertex_colour(block: PropLightBlock, n: Sequence[float]) -> Tuple[float, float, float, float]:
    """VU1 per-vertex result (before FTOI0 truncation): RGBA on the 0..255 scale, 128 = 1.0."""
    d = [max(0.0, n[0] * v[0] + n[1] * v[1] + n[2] * v[2]) for v in block.dirs]
    out = []
    for ch in range(4):
        c = block.ambient[ch] + sum(block.colours[i][ch] * d[i] for i in range(3))
        out.append(min(c, 255.0))
    return tuple(out)


def vertex_colour_u8(block: PropLightBlock, n) -> Tuple[int, int, int, int]:
    """As the VU stores it: FTOI0 (truncation toward zero) of the clamped value. Values cannot go
    below 0 unless the data has negative colours (none seen); the VU applies no lower clamp."""
    return tuple(int(v) for v in vertex_colour(block, n))


def modulate(tex_rgb: Sequence[float], vc: Sequence[float]) -> Tuple[float, ...]:
    """PS2 texture-modulate: texel x vertex colour / 128 (texel 0..255), clamped to 255."""
    return tuple(min(t * c / 128.0, 255.0) for t, c in zip(tex_rgb, vc))


def tex1_lod_params(enabled: int, dist: float, shift: int) -> Tuple[int, int, int]:
    """LOD triple: (LCM, L, K). enabled = table 0x68; dist/shift = 0x6C/0x70 (1P) or 0x74/0x78."""
    if not enabled:
        return 1, 0, 0
    k = int(-((math.log(dist) / 0.6931472) * float(1 << shift)) * 16.0)   # trunc toward zero
    return 0, shift, k


def gs_lod(view_depth: float, L: int, K: int) -> float:
    """GS manual LOD formula for LCM = 0 (Q = 1/w)."""
    return (2 ** L) * math.log2(view_depth) + K / 16.0


def clear_colour(table_rgb: Sequence[int]) -> Tuple[int, int, int, int]:
    """Frame clear RGBA. The game round-trips the bytes through /255 and x255 (float -> int, the
    exe float-to-int helper); for all 13 table entries this returns the stored bytes (checked below)."""
    out = []
    for c in table_rgb:
        f = float(c) / 255.0
        out.append(int(round(f * 255.0)))   # TODO: 0x2F79A8 rounding mode; identical for these inputs
    return tuple(out) + (0x80,)


def selftest() -> int:
    import os, struct, sys
    sys.path.insert(0, os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
    import exe_tables
    raw = exe_tables.world_track_table()
    table = []
    for k in range(13):     # 0x257DC0 records (0x94 bytes)
        o = k * 0x94
        table.append({"track": str(k), "colour_rgb": list(struct.unpack_from("<3i", raw, o + 0x48)),
                      "draw_dist_1p": struct.unpack_from("<f", raw, o + 0x60)[0],
                      "fog_on": struct.unpack_from("<i", raw, o + 0x68)[0],
                      "fog_dist_1p": struct.unpack_from("<f", raw, o + 0x6C)[0],
                      "fog_shift_1p": struct.unpack_from("<i", raw, o + 0x70)[0]})
    bad = 0
    for rec in table:
        rgb = rec["colour_rgb"]
        if clear_colour(rgb)[:3] != tuple(rgb):
            bad += 1
        lcm, L, K = tex1_lod_params(rec["fog_on"], rec["fog_dist_1p"], rec["fog_shift_1p"])
        if not -2048 <= K <= 2047:
            bad += 1
        print(f"{rec['track']:10s} clear {tuple(rgb)}  TEX1 1P: LCM {lcm} L {L} K {K} "
              f"(LOD 0 up to {rec['fog_dist_1p']:.0f} cm, LOD at draw dist {gs_lod(rec['draw_dist_1p'], L, K):5.2f})")
    print("selftest", "ok" if not bad else f"FAIL {bad}")
    return bad


if __name__ == "__main__":
    raise SystemExit(selftest())
