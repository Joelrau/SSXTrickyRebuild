"""Rider lighting probe reference model.

Written from the behaviour spec. Units cm; Z up; game space. `probe(P)` returns what the rider
renderer receives for one frame: an ambient colour and three directional light slots, after the
per-track adjustment.

Data: the .pbd LIGHTS (92-byte records) and the .ltg sub-cell light lists (u16 count +0x2E, cell-relative list offset +0x44). `load_track(dir)` reads both from the
track files; the model itself only needs plain lists.

Validated against PCSX2 RAM (traces/live/*/spawn_ram.bin, Garibaldi start line, 18 riders): the
ambient, slot-0 colour and slot-0 direction the game stored (AI riders: cBoarder - 0x4AC / - 0x47C /
- 0x450; the human rider 0x30 further) match to 6.5e-7 (rebuild tools/check_world_presentation.py).
These samples only exercise the ambient + sun path (no spot / point / modulator in range).

Slot rule: the game keeps THREE directional slots
in a list sorted by ascending score, score = abs(r + g + b) of the (attenuated, modulated) colour.
Slots start at score 0, colour 0, direction (1, 0, 0). Each directional/point/spot candidate, in
light-list order, enters only if its score is strictly greater than the current lowest score; it is
inserted in sorted position and the lowest entry drops out. So a single light lands in game slot 2
(the RAM shows the Garibaldi sun at render+0x430 / +0x460, slots 0 and 1 empty). A negative
(darkening) light competes by the magnitude of its sum. All three slots are scaled by table 0x90.
`ProbeResult.colours/directions` are listed strongest first (index i = game slot 2 - i), which keeps
index 0 = the RAM field the rebuild check compares; `game_slots()` gives the game's order.
Flag bits other than 0x100 / 0x200 are ignored.
"""
from __future__ import annotations

import math
import struct
from dataclasses import dataclass
from pathlib import Path
from typing import List, Optional, Sequence, Tuple

DIRECTIONAL, SPOT, POINT, AMBIENT = 0, 1, 2, 3
FLAG_MODULATOR = 0x100
FLAG_NOT_MODULATED = 0x200
FALLOFF_KNEE = 100.0          # cm: below this the falloff is 1
LIGHT_SLOTS = 3               # the probe initialises three slots; the track adjustment scales three
EMPTY_DIRECTION = (1.0, 0.0, 0.0)   # game default for an unused slot
SUBCELL_SIDE = 4              # 4 x 4 sub-cells per .ltg cell; k = sy * 4 + sx


@dataclass
class Light:
    type: int
    flags: int
    colour: Tuple[float, float, float]
    direction: Tuple[float, float, float]
    position: Tuple[float, float, float]
    cos_cone: float
    falloff: int
    exponent: float

    @staticmethod
    def from_record(r: bytes) -> "Light":
        t, fl = struct.unpack_from("<iI", r, 0)
        return Light(t, fl, struct.unpack_from("<3f", r, 0x10), struct.unpack_from("<3f", r, 0x1C),
                     struct.unpack_from("<3f", r, 0x28), struct.unpack_from("<f", r, 0x4C)[0],
                     struct.unpack_from("<i", r, 0x50)[0], struct.unpack_from("<f", r, 0x54)[0])


@dataclass
class Grid:
    min_x: float
    min_y: float
    cell: float
    nx: int
    ny: int
    sub: float
    lights: List[Optional[List[List[int]]]]    # per cell: None or 16 sub-cell light index lists

    def sublist(self, p) -> List[int]:
        ix = math.floor((p[0] - self.min_x) / self.cell)
        iy = math.floor((p[1] - self.min_y) / self.cell)
        if not (0 <= ix < self.nx and 0 <= iy < self.ny):
            return []
        c = self.lights[iy * self.nx + ix]
        if c is None:
            return []
        sx = min(max(math.floor((p[0] - self.min_x - ix * self.cell) / self.sub), 0), SUBCELL_SIDE - 1)
        sy = min(max(math.floor((p[1] - self.min_y - iy * self.cell) / self.sub), 0), SUBCELL_SIDE - 1)
        return c[sy * SUBCELL_SIDE + sx]


@dataclass
class TrackAdjust:
    blend: float                              # table 0x80 f
    target: Tuple[float, float, float]        # table 0x84 C
    scale: float                              # table 0x90


@dataclass
class ProbeResult:
    ambient: Tuple[float, float, float]
    colours: List[Tuple[float, float, float]]     # LIGHT_SLOTS entries, strongest first (zero when unused)
    directions: List[Tuple[float, float, float]]  # unit, pointing toward the light
    modulator: Tuple[float, float, float]
    total: Tuple[float, float, float] = (0.0, 0.0, 0.0)  # render+0x470: ambient + sum of the 3 colours

    def game_slots(self):
        """(colours, directions) in the game's storage order (render+0x410.. / +0x440..)."""
        return list(reversed(self.colours)), list(reversed(self.directions))


def insert_slot(slots, colour, direction):
    """Slot insertion: ascending top-3 insertion. slots = [[score, colour, direction]] x 3 (game order)."""
    f = abs(colour[0] + colour[1] + colour[2])
    if not (slots[0][0] < f):
        return
    if slots[1][0] < f:
        slots[0] = slots[1]
        if slots[2][0] < f:
            slots[1] = slots[2]
            slots[2] = [f, colour, direction]
        else:
            slots[1] = [f, colour, direction]
    else:
        slots[0] = [f, colour, direction]


def falloff(kind: int, d: float) -> float:
    if kind == 0 or d < FALLOFF_KNEE:
        return 1.0
    if kind == 1:
        return 100.0 / d
    if kind == 2:
        return 10000.0 / (d * d)
    if kind == 3:
        return 1e6 / (d * d * d)
    return 1.0


def ipow(c: float, e: float) -> float:
    """Spot exponent used as an integer power; negative = reciprocal."""
    n = int(e)
    v = c ** abs(n)
    return (1.0 / v if v != 0.0 else 0.0) if n < 0 else v


def attenuation(l: Light, p) -> Tuple[Optional[float], Tuple[float, float, float]]:
    """(atten or None when outside a spot cone, unit vector toward the light)."""
    L = (l.position[0] - p[0], l.position[1] - p[1], l.position[2] - p[2])
    d = math.sqrt(L[0] * L[0] + L[1] * L[1] + L[2] * L[2])
    lh = (L[0] / d, L[1] / d, L[2] / d) if d > 0.0 else (0.0, 0.0, 1.0)
    a = falloff(l.falloff, d)
    if l.type == SPOT:
        c = -(lh[0] * l.direction[0] + lh[1] * l.direction[1] + lh[2] * l.direction[2])
        if c <= l.cos_cone:
            return None, lh
        a *= ipow(c, l.exponent)
    return a, lh


def probe(lights: Sequence[Light], indices: Sequence[int], p, adj: TrackAdjust,
          flash=(0.0, 0.0, 0.0)) -> ProbeResult:
    # 1. modulator pass
    m = [1.0, 1.0, 1.0]
    for i in indices:
        l = lights[i]
        if not (l.flags & FLAG_MODULATOR) or l.type not in (SPOT, POINT):
            continue
        a, _ = attenuation(l, p)
        if a is None:
            continue
        for k in range(3):
            m[k] += l.colour[k] * a
    m = [min(max(v, 0.0), 1.0) for v in m]

    # 2. lighting pass
    amb = [0.0, 0.0, 0.0]
    cands = []
    for i in indices:
        l = lights[i]
        if l.flags & FLAG_MODULATOR:
            continue
        mod = (1.0, 1.0, 1.0) if l.flags & FLAG_NOT_MODULATED else m
        if l.type == AMBIENT:
            for k in range(3):
                amb[k] += l.colour[k] * mod[k]
        elif l.type == DIRECTIONAL:
            cands.append((tuple(l.colour[k] * mod[k] for k in range(3)),
                          tuple(-v for v in l.direction)))
        elif l.type in (SPOT, POINT):
            a, lh = attenuation(l, p)
            if a is None:
                continue
            cands.append((tuple(l.colour[k] * a * mod[k] for k in range(3)), lh))
    slots = [[0.0, (0.0, 0.0, 0.0), EMPTY_DIRECTION] for _ in range(LIGHT_SLOTS)]
    for c, d in cands:                       # light-list order, as the game walks the sub-cell list
        insert_slot(slots, c, d)
    cands = [(c, d) for _, c, d in reversed(slots)]   # strongest first

    # 3. per-track adjustment (0x135EB8)
    f, s = adj.blend, adj.scale
    ambient = tuple((amb[k] + (adj.target[k] - amb[k]) * f) * s + flash[k] for k in range(3))
    colours = [tuple(c[k] * s for k in range(3)) for c, _ in cands]
    total = tuple(ambient[k] + sum(c[k] for c in colours) for k in range(3))
    return ProbeResult(ambient, colours, [d for _, d in cands], tuple(m), total)


def load_lights(pbd_bytes: bytes) -> List[Light]:
    counts = struct.unpack_from("<15i", pbd_bytes, 4)
    offs = struct.unpack_from("<18i", pbd_bytes, 64)
    n, o = counts[6], offs[6]
    return [Light.from_record(pbd_bytes[o + 92 * k:o + 92 * k + 92]) for k in range(n)]


def load_grid(ltg: bytes) -> Grid:
    min_x, min_y = struct.unpack_from("<2f", ltg, 4)
    cell = struct.unpack_from("<f", ltg, 0x28)[0]
    nx, ny, ncell = struct.unpack_from("<3i", ltg, 0x2C)
    sub = struct.unpack_from("<f", ltg, 0x40)[0]
    _side, sub_n, table = struct.unpack_from("<3i", ltg, 0x44)
    cells: List[Optional[List[List[int]]]] = []
    for co in struct.unpack_from(f"<{ncell}I", ltg, table):
        if not co:
            cells.append(None)
            continue
        sub0 = co + struct.unpack_from("<I", ltg, co + 0x38)[0]
        lists = []
        for k in range(sub_n):
            so = sub0 + 76 * k
            n = struct.unpack_from("<H", ltg, so + 0x2E)[0]
            p = struct.unpack_from("<I", ltg, so + 0x44)[0]
            lists.append(list(struct.unpack_from(f"<{n}i", ltg, co + p)) if (n and p) else [])
        cells.append(lists)
    return Grid(min_x, min_y, cell, nx, ny, sub, cells)


def load_track(models_dir, stem: str):
    d = Path(models_dir)
    return load_lights((d / f"{stem}.pbd").read_bytes()), load_grid((d / f"{stem}.ltg").read_bytes())


def adjust_from_table(rec: dict) -> TrackAdjust:
    return TrackAdjust(rec["rider_amb_blend"], tuple(rec["rider_amb_target"]), rec["rider_light_scale"])
