"""Particle systems, rider effects, shadow and snow-flake size.

Clean-room behaviour model.

Units: cm, seconds, 60 fps frames. Colours are PS2 GS units (128 = 1.0, clamp 0..255) unless named
"_unit" (0..1). Vectors are plain 3/4-tuples; the rider is a dict of named fields (cBoarder
offsets).
"""
from __future__ import annotations

import math
import struct
from dataclasses import dataclass, field
from typing import Callable, Dict, List, Optional, Sequence, Tuple

FRAME = 1.0 / 60.0
U32 = (1 << 32) - 1

# Blending
# GS ALPHA register per renderer blend mode:
#   output = (A - B) * C + D, with Cs/Cd = source/dest colour, As = source alpha (128 = 1.0).
GS_BLEND = {
    0: "Cs (opaque)",
    1: "Cd (no colour write)",
    2: "Cd*As (multiply)",
    3: "(Cs-Cd)*As+Cd (alpha)",
    4: "Cd-Cd*As (darken)",
    5: "Cs*As+Cd (additive)",
    6: "(Cd-Cs)*As",
    7: "Cd-Cs*As (subtract)",
    8: "Cd*As+Cd",
    9: "(Cs-Cd)*FIX+Cd",
    10: "Cs*FIX+Cd",
}
# .ssf emitter word 0x32 -> renderer blend mode. Indices 8..10 read past the
# 8-entry table (into a string): unused by retail data (word 0x32 is 0, 1 or 4).
EMITTER_BLEND = (5, 3, 2, 1, 4, 0, 6, 7)

def emitter_blend_mode(word32: int) -> int:
    k = min(max(word32, 0), 10)
    if k >= len(EMITTER_BLEND):
        raise ValueError("emitter blend index %d reads past the table (TODO)" % k)
    return EMITTER_BLEND[k]

# VU random generator. Values are floats in [1, 2).
ONE_BITS = 127 << 23          # bit pattern of 1.0f
MANTISSA = (1 << 23) - 1

def vu_rinit(seed_float: float) -> int:
    bits = struct.unpack("<I", struct.pack("<f", seed_float))[0]
    return ONE_BITS | (bits & MANTISSA)

def vu_rnext(r: int) -> int:
    # PCSX2's documented LFSR; bit-exactness on hardware unverified (cosmetic only).
    x = (r >> 4) & 1
    y = (r >> 22) & 1
    r = ((r << 1) ^ x ^ y) & MANTISSA
    return r | ONE_BITS

def r_float(r: int) -> float:
    return struct.unpack("<f", struct.pack("<I", r))[0]

def advance_seed(seed: float) -> float:
    """seed := RNEXT(RINIT(seed)); used by the CPU and the VU program alike."""
    return r_float(vu_rnext(vu_rinit(seed)))


def _add(a, b): return tuple(x + y for x, y in zip(a, b))
def _sub(a, b): return tuple(x - y for x, y in zip(a, b))
def _mul(a, s): return tuple(x * s for x in a)
def _dot(a, b): return sum(x * y for x, y in zip(a, b))
def _norm(a):
    n = math.sqrt(_dot(a, a))
    return a if n == 0 else _mul(a, 1.0 / n)


# Emitter record (.ssf type 2, kind 0/2): 51 config words from record +0x0C
@dataclass
class EmitterConfig:
    count: int                 # 0x00 N particles
    copies: int                # 0x01 trail copies per particle (drawn max(1, copies) times)
    emit_time: float           # 0x02 s, < 0 = continuous
    time_scale: float          # 0x03 "ts": internal time units per second
    size: float                # 0x04 sprite half-size mean (cm)
    life: float                # 0x05 particle life mean (s)
    size_range: float          # 0x06 full range (size = mean +- range/2)
    life_range: float          # 0x07 full range
    copy_spacing: float        # 0x08 s between trail copies
    origin: Tuple[float, float, float]        # 0x09 local point
    pos_a: Tuple[float, float, float]         # 0x0C local jitter axis
    pos_b: Tuple[float, float, float]         # 0x0F local jitter axis
    vel: Tuple[float, float, float]           # 0x12 local velocity (cm/s)
    vel_a: Tuple[float, float, float]         # 0x15 local jitter axes
    vel_b: Tuple[float, float, float]         # 0x18
    vel_c: Tuple[float, float, float]         # 0x1B
    drift: Tuple[float, float, float]         # 0x1E world "gravity" (terminal drift), not rotated
    key0: Tuple[float, float, float, float]   # 0x21 (A, R, G, B) unit colour at birth
    key1: Tuple[float, float, float, float]   # 0x25 (A, R, G, B) at max life
    jit0: Tuple[float, float, float, float]   # 0x29 (A, R, G, B) jitter amplitude
    jit1: Tuple[float, float, float, float]   # 0x2D (A, R, G, B) jitter amplitude
    texture: int               # 0x31 PARTICLE.SSH image index (game+0x30 table is identity)
    blend: int                 # 0x32 index into EMITTER_BLEND

    @classmethod
    def from_words(cls, w: Sequence[int]) -> "EmitterConfig":
        f = [struct.unpack("<f", struct.pack("<I", x & U32))[0] for x in w]
        v3 = lambda k: (f[k], f[k + 1], f[k + 2])
        v4 = lambda k: (f[k], f[k + 1], f[k + 2], f[k + 3])
        return cls(w[0], w[1], f[2], f[3], f[4], f[5], f[6], f[7], f[8], v3(9), v3(12), v3(15),
                   v3(18), v3(21), v3(24), v3(27), v3(30), v4(33), v4(37), v4(41), v4(45),
                   w[49], w[50])

    @classmethod
    def from_record(cls, rec) -> "EmitterConfig":
        """rec: models.effects.Record of type 2 (payload word 0 = kind)."""
        return cls.from_words([rec.i(k + 1) & U32 for k in range(51)])

    @property
    def max_life(self) -> float:
        return self.life + self.life_range * 0.5

    @property
    def continuous(self) -> bool:
        return self.emit_time < 0

    def node_life(self) -> float:
        """Emitter node lifetime: emission + max life; negative = continuous."""
        return self.emit_time if self.continuous else self.emit_time + self.max_life


def _argb_to_rgba128(k):
    a, r, g, b = k
    return (r * 128.0, g * 128.0, b * 128.0, a * 128.0)


# The analytic particle system (VU1 program 6)
DRAG_A, DRAG_B, DRAG_CLAMP = 0.73, 0.113, 2.7      # VU constants (LOI)

@dataclass
class ParticleSystem:
    n: int
    copies: int
    t: float                    # internal time (seconds x ts)
    interval: float             # internal time between births
    ts: float
    size_lo: float              # size = size_lo + size_span * u
    size_span: float
    life_lo: float              # internal time units
    life_span: float
    copy_dt: float              # copy_spacing x ts
    seeds: List[float]          # 9 seeds in [1, 2)
    drift: Tuple[float, float, float]  # world drift / ts^2
    p0: Tuple[float, float, float]     # spawn centre (world)
    pa: Tuple[float, float, float]     # spawn half-axes (world; position = p0 + (2u-1)...)
    pb: Tuple[float, float, float]
    pos_scale: float            # 0.5 (emitter nodes) or 1.0 (rider helpers)
    v0: Tuple[float, float, float]     # velocity / ts
    va: Tuple[float, float, float]
    vb: Tuple[float, float, float]
    vc: Tuple[float, float, float]
    c0: Tuple[float, float, float, float]     # RGBA (GS units)
    c_slope: Tuple[float, float, float, float]  # per internal time unit
    cj0: Tuple[float, float, float, float]
    cj1: Tuple[float, float, float, float]
    base_step: Tuple[float, float, float] = (0.0, 0.0, 0.0)   # q13 "drift per retire", 0 in retail
    continuous: bool = False
    texture: int = 0
    blend_mode: int = 3

    @classmethod
    def from_emitter(cls, cfg: EmitterConfig, inst_matrix=None, rng: Callable[[], float] = None,
                     origin_override=None, velocity_override=None) -> "ParticleSystem":
        """. inst_matrix: 4x4 row-vector matrix (rows X, Y, Z, T), default identity.
        rng: uniform [0, 1) draws of the game stream used for the 9 seeds."""
        m = inst_matrix or ((1, 0, 0, 0), (0, 1, 0, 0), (0, 0, 1, 0), (0, 0, 0, 1))
        rot = lambda v: tuple(v[0] * m[0][i] + v[1] * m[1][i] + v[2] * m[2][i] for i in range(3))
        pt = lambda v: _add(rot(v), m[3][:3])
        rng = rng or (lambda: 0.5)
        ts = cfg.time_scale
        cont = cfg.continuous
        span_t = cfg.max_life if cont else cfg.emit_time
        origin = origin_override if origin_override is not None else cfg.origin
        vel = velocity_override if velocity_override is not None else cfg.vel
        size_lo = cfg.size - cfg.size_range * 0.5
        life_lo = cfg.life - cfg.life_range * 0.5
        return cls(
            n=cfg.count, copies=cfg.copies,
            t=cfg.max_life * ts if cont else 0.0,
            interval=span_t * ts / cfg.count,
            ts=ts, size_lo=size_lo, size_span=cfg.size_range,
            life_lo=life_lo * ts, life_span=cfg.life_range * ts,
            copy_dt=cfg.copy_spacing * ts,
            seeds=[rng() + 1.0 for _ in range(9)],
            drift=_mul(cfg.drift, 1.0 / (ts * ts)),
            p0=pt(origin), pa=rot(cfg.pos_a), pb=rot(cfg.pos_b), pos_scale=0.5,
            v0=_mul(rot(vel), 1.0 / ts), va=_mul(rot(cfg.vel_a), 1.0 / ts),
            vb=_mul(rot(cfg.vel_b), 1.0 / ts), vc=_mul(rot(cfg.vel_c), 1.0 / ts),
            c0=_argb_to_rgba128(cfg.key0),
            c_slope=_mul(_sub(_argb_to_rgba128(cfg.key1), _argb_to_rgba128(cfg.key0)),
                         1.0 / (cfg.max_life * ts)),
            cj0=_argb_to_rgba128(cfg.jit0), cj1=_argb_to_rgba128(cfg.jit1),
            continuous=cont, texture=cfg.texture, blend_mode=emitter_blend_mode(cfg.blend))

    def update(self, dt: float = FRAME):
        self.t += dt * self.ts
        if self.continuous:
            while self.t > self.n * self.interval:
                self.t -= self.interval
                self.p0 = _add(self.p0, self.base_step)
                self.seeds = [advance_seed(s) for s in self.seeds]

    @staticmethod
    def drag(age: float) -> float:
        a = min(age, DRAG_CLAMP)
        return DRAG_A * a - DRAG_B * a * a

    def sprites(self) -> List[dict]:
        """Every visible sprite this frame: centre (world), half-size (cm, world units, screen
        aligned), rgba (GS units, clamped), texture, blend mode. Order = VU emission order."""
        out = []
        seeds = list(self.seeds)
        for k in range(self.n):
            seeds = [advance_seed(s) for s in seeds]
            r = [s - 1.5 for s in seeds]            # centred jitter in [-0.5, 0.5)
            age = self.t - k * self.interval
            ps = self.pos_scale
            # emitter path: centre +-0.5 x axis; rider helpers: centre +-1 x axis (2u-3 form)
            p0 = _add(self.p0, _add(_mul(self.pa, r[0] * 2 * ps), _mul(self.pb, r[1] * 2 * ps)))
            p0 = _add(p0, _mul(self.base_step, k))
            v = _add(self.v0, _add(_mul(self.va, r[2]), _add(_mul(self.vb, r[3]),
                                                             _mul(self.vc, r[4]))))
            col = tuple(self.c0[i] + self.cj0[i] * r[5] + self.cj1[i] * r[6]
                        + self.c_slope[i] * age for i in range(4))
            size = self.size_lo + self.size_span * seeds[7]   - self.size_span
            life = self.life_lo + self.life_span * seeds[8]   - self.life_span
            for j in range(max(1, self.copies)):
                aj = age - j * self.copy_dt
                if not (0.0 <= aj < life):           # boundary (<= / <) TODO-minor
                    continue
                q = self.drag(aj)
                pos = _add(p0, _add(_mul(v, q), _mul(self.drift, aj - q)))
                m = 1.0 - j / max(1, self.copies)
                rgba = tuple(min(max(c, 0.0), 255.0) for c in (col[0], col[1], col[2], col[3] * m))
                out.append(dict(pos=pos, half_size=size, rgba=rgba, particle=k, copy=j,
                                texture=self.texture, blend=self.blend_mode))
        return out

    def internal_fields(self) -> dict:
        """The PS record fields as the game stores them (RAM layout)."""
        return {
            "N": self.n, "copies": self.copies, "t": self.t, "interval": self.interval,
            "size_span": self.size_span, "life_span": self.life_span,
            "size_base": self.size_lo - self.size_span, "life_base": self.life_lo - self.life_span,
            "copy_dt": self.copy_dt, "ts": self.ts, "drift": self.drift,
        }


# size/life: the game stores base = lo - span and draws base + span * u with u in [1, 2),
# so the value is uniform in [lo, lo + span). sprites() above uses the same form.


# Rider effects. rider dict keys (cBoarder offsets):
#   pos(+0x140) vel(+0x150) fwd(+0x180) right(+0x190) up(+0x1A0) steer(+0x214) goofy(+0x1B4)
#   brake(+0x1FC) motion(+0x424) contact_kind(+0x2E0) surface(+0x290) contact(+0x2D0)
#   board_long(+0x4A60) board_up(+0x4A70) board_across(+0x4A80) board_origin(+0x4A90)
#   height(+0x1BC) boost_strength(+0x130) burst_timer(+0x134) launch_flag(+0x13C)
#   light_total (handler+0x470: lead, r, g, b)
MOTION_AIR, MOTION_GROUND, MOTION_RAIL = 1, 2, 3
SPARK_SURFACES = (9, 13, 18, 19)

SPRAY_RING = 40              # puffs kept (oldest overwritten)

@dataclass
class SurfaceSpray:
    """Surface table (racemgr+0x24, 100-byte rows) fields used by the effects."""
    rate: float        # +0x34
    size: float        # +0x38
    life_lo: float     # +0x3C
    life_hi: float     # +0x40
    texture: int       # +0x44 (int)
    desat: float       # +0x48
    brighten: float    # +0x4C
    alpha: float       # +0x50
    blend: int         # +0x54 (int, renderer blend mode, NOT via EMITTER_BLEND)
    curtain_width: float = 0.0   # +0x58
    curtain_offset: float = 0.0  # +0x5C
    curtain_speed: float = 0.0   # +0x60

    @classmethod
    def from_row(cls, row: bytes) -> "SurfaceSpray":
        f = lambda o: struct.unpack_from("<f", row, o)[0]
        i = lambda o: struct.unpack_from("<i", row, o)[0]
        return cls(f(0x34), f(0x38), f(0x3C), f(0x40), i(0x44), f(0x48), f(0x4C), f(0x50),
                   i(0x54), f(0x58), f(0x5C), f(0x60))

AIR_DEFAULT = SurfaceSpray(0.0, 0.0, 0.4, 0.6, 13, 0.7, 0.2, 0.25, 5)


def spray_colour(light_total, desat, brighten):
    """Lighting tint (rider total light r, g, b), pulled toward grey by `desat`, plus `brighten`,
    clamped to [0, 1] per channel."""
    rgb = light_total[1:4]
    m = sum(rgb) / 3.0
    return tuple(min(max(c + desat * (m - c) + brighten, 0.0), 1.0) for c in rgb)


class SprayEmitter:
    """Board snow spray and rail/metal sparks (handler FX block +0x4B60)."""

    def __init__(self):
        self.ring: List[ParticleSystem] = []
        self.air_frames = 0          # param +0x08 "was in air" flag
        self.air_rate = 70.0         # +0x0C
        self.flash = [0.0, 0.0, 0.0, 0.0]   # +0x24..+0x30, added to the rider light ambient
        self.prev_point = None       # +0x10
        self.spark_active = False    # +0x20

    def _push(self, ps):
        self.ring.append(ps)
        if len(self.ring) > SPRAY_RING:
            self.ring.pop(0)

    # rate and base velocity per motion
    @staticmethod
    def spray_setup(r: dict, surf: Optional[SurfaceSpray]):
        speed = math.sqrt(_dot(r["vel"], r["vel"]))
        mot = r["motion"]
        A, U, L = r["board_across"], r["board_up"], r["board_long"]
        cfg = dict(spread_a=400.0, spread_u=50.0, spread_l=400.0, size=(2.0, 4.0),
                   life=(0.4, 0.6), desat=0.7, brighten=0.2, alpha=0.25, blend=5, texture=0,
                   rate=0.0, v0=(0.0, 0.0, 0.0))
        if mot == MOTION_GROUND and r["contact_kind"] != 2:
            if r["surface"] in SPARK_SURFACES:
                cfg.update(v0=r["vel"], texture=13)
            else:
                e = -r["steer"] if r["goofy"] else r["steer"]
                v0 = _add(r["vel"], _mul(A, e * 1.1 * speed))
                rate = (e * e * 149.0 * speed * 0.006 + speed * 0.006) * surf.rate
                d = _dot(v0, A)
                if abs(d) < 450.0:
                    spread = 300.0
                else:
                    spread = abs(d) - 150.0
                    v0 = _sub(v0, _mul(A, (d + 450.0) * 0.5)) if d < 0 else \
                        _add(v0, _mul(A, (450.0 - d) * 0.5))
                dl = _dot(v0, L)
                v0 = _sub(v0, _mul(L, dl * 0.5))
                cfg.update(v0=v0, rate=rate, spread_a=spread, spread_u=0.0, spread_l=dl,
                           size=(surf.size * 0.8, surf.size * 1.2), life=(surf.life_lo, surf.life_hi),
                           desat=surf.desat, brighten=surf.brighten, alpha=surf.alpha,
                           blend=surf.blend, texture=surf.texture)
        elif mot == MOTION_AIR:
            cfg.update(v0=r["vel"], texture=13, rate=None)   # rate from the air decay below
        elif mot == MOTION_RAIL:
            cfg.update(v0=r["vel"], texture=13,
                       rate=0.0 if r["surface"] in (13, 18, 19) else 30.0)
        return speed, cfg

    def frame(self, r: dict, surf: Optional[SurfaceSpray], rng: Callable[[], float]) -> int:
        """One frame. Returns the number of particles in the new main-spray puff (0 = none)."""
        for ps in self.ring:
            ps.t += FRAME * ps.ts                    #  on every ring entry
        speed, cfg = self.spray_setup(r, surf)
        if r["motion"] == MOTION_AIR:
            if self.air_frames == 0:
                rate = 70.0
                self.air_rate = 70.0
            else:
                rate = self.air_rate
                if rate > 10.0:
                    rate *= 0.94666666
                    self.air_rate = rate
            self.air_frames = 1
            if speed / 277.77777 <= 1.0:
                rate *= speed / 277.77777
            cfg["rate"] = rate
        else:
            self.air_frames = 0
        n = int(cfg["rate"] * FRAME + rng())
        if n:
            point = r["contact"] if r["motion"] == MOTION_GROUND else r["board_origin"]
            rgb = spray_colour(r["light_total"], cfg["desat"], cfg["brighten"])
            key0 = tuple(c * 128.0 for c in rgb) + (cfg["alpha"] * 128.0,)
            key1 = tuple(c * 128.0 for c in rgb) + (0.0,)
            self._push(self._make(n, copies=1, emit=0.025, copy_spacing=0.005, ts=5.0,
                                  drift=(0.0, 0.0, -100.0), size=cfg["size"], life=cfg["life"],
                                  centre=point, ax=_mul(r["board_across"], 15.0),
                                  bx=_mul(r["board_long"], 90.0), v0=cfg["v0"],
                                  va=_mul(r["board_across"], cfg["spread_a"]),
                                  vb=_mul(r["board_up"], cfg["spread_u"]),
                                  vc=_mul(r["board_long"], cfg["spread_l"]),
                                  key0=key0, key1=key1, j0=(0, 0, 0, 0), j1=(0, 0, 0, 0),
                                  texture=cfg["texture"], blend=cfg["blend"], rng=rng))
        self._launch_flash(r)
        self._sparks(r, speed, rng)
        for i in range(1, 4):
            self.flash[i] = min(max(self.flash[i] - 1.0 / 12.0, 0.0), 1.0)
        self.flash[0] = min(max(self.flash[0], 0.0), 1.0)
        return n

    def _launch_flash(self, r):
        # launch boost (+0x13C): flash = (0, v, v, v) from handler+0x0C (value, rate, target);
        # the rate writer is TODO, so v is taken from the rider dict if given.
        if r.get("launch_flag"):
            v = r.get("launch_glow", 0.0)
            self.flash = [0.0, v, v, v]

    def _sparks(self, r, speed, rng):
        mot = r["motion"]
        if mot not in (MOTION_GROUND, MOTION_RAIL) or r["surface"] not in SPARK_SURFACES:
            self.spark_active = False
            return
        point = _add(r["contact"], _mul(r["vel"], 0.02))
        if mot == MOTION_RAIL:
            point = _add(point, _mul(r["vel"], 0.02))
        if not self.spark_active:
            self.prev_point = point
        self.spark_active = True
        rate, life_hi, k = 50.0, 0.25, 2.0
        if rng() < FRAME:                           # big burst, 1 frame in 60 on average
            rate, life_hi, k = 1050.0, 0.65, 3.0
            self.flash = [0.0, 0.8, 0.6, 0.2]
        n = int(rate * FRAME + rng())
        if n:
            s = min(speed, 1666.6666) * k
            side = min(s * 0.35, 1111.1111)
            v0 = _add(_mul(r["up"], s * 0.05), _mul(r["fwd"], -0.1 * s))
            if mot == MOTION_RAIL:
                seg = (point, self.prev_point)      # current -> previous
            else:
                seg = None
            self._push(self._make(n, copies=7, emit=0.025, copy_spacing=0.007, ts=12.0,
                                  drift=(0.0, 0.0, -2000.0), size=(0.75, 1.25), life=(0.1, life_hi),
                                  centre=point, ax=_mul(r["board_across"], 15.0),
                                  bx=_mul(r["board_long"], 90.0), segment=seg, v0=v0,
                                  va=_mul(r["right"], side), vb=_mul(r["fwd"], s * 0.2),
                                  vc=_mul(r["up"], s * 0.2),
                                  key0=(200.0, 90.0, 0.0, 80.0), key1=(200.0, 90.0, 90.0, 0.0),
                                  j0=(0.0, 50.0, 0.0, 0.0), j1=(0.0, 0.0, 50.0, 0.0),
                                  texture=0, blend=5, rng=rng, t0=0.03 * 12.0))
        self.prev_point = point

    @staticmethod
    def _make(n, copies, emit, copy_spacing, ts, drift, size, life, centre, ax, bx, v0, va, vb, vc,
              key0, key1, j0, j1, texture, blend, rng, segment=None, t0=0.0):
        """Build a puff the way the board-spray update does.
        Colour keys here are RGBA in GS units; size/life are (lo, hi)."""
        lo, hi = size
        llo, lhi = life
        if segment is not None:      # rail: on the segment from the previous point
            a, b = segment
            p0, pa, pb, scale = a, _sub(b, a), (0.0, 0.0, 0.0), None
        else:
            p0, pa, pb, scale = centre, ax, bx, 1.0
        ps = ParticleSystem(
            n=n, copies=copies, t=t0, interval=emit * ts / n, ts=ts,
            size_lo=lo, size_span=hi - lo, life_lo=llo * ts, life_span=(lhi - llo) * ts,
            copy_dt=copy_spacing * ts, seeds=[rng() + 1.0 for _ in range(9)],
            drift=_mul(drift, 1.0 / (ts * ts)), p0=p0, pa=pa, pb=pb, pos_scale=1.0,
            v0=_mul(v0, 1.0 / ts), va=_mul(va, 1.0 / ts), vb=_mul(vb, 1.0 / ts),
            vc=_mul(vc, 1.0 / ts), c0=key0, c_slope=_mul(_sub(key1, key0), 1.0 / (lhi * ts)),
            cj0=j0, cj1=j1, texture=texture, blend_mode=blend)
        if scale is None:
            # segment form: position = a + (u - 1) (b - a)... with u in [1, 2): handled by
            # mapping the centred jitter r in [-0.5, 0.5) -> (r + 0.5) along pa.
            ps.p0 = _add(a, _mul(pa, 0.5))
            ps.pos_scale = 0.5
        return ps


# Carve clouds (FX block +0x0000)
CARVE_CLOUD_RING = 64
CARVE_CLOUD_SPACING = 70.0      # cm of board-origin travel between clouds
CARVE_CLOUD_LIFE = 2.0
CARVE_CLOUD_SIZE_MIN, CARVE_CLOUD_SIZE_END = 80.0, 400.0

def carve_cloud_spawn(r: dict, last_origin, rng_bit: Callable[[], int]) -> Optional[dict]:
    """New cloud or None. rng_bit: game PRNG draws (only bits used)."""
    if r["contact_kind"] == 2 or r["motion"] != MOTION_GROUND:
        return None
    ice = r["surface"] in (3, 4)
    if not ice and abs(r["steer"]) <= 0.2:
        return None
    if last_origin is not None and math.dist(r["board_origin"], last_origin) <= CARVE_CLOUD_SPACING:
        return None
    rgb = spray_colour(r["light_total"], 0.5, 0.0)
    return dict(age=0.0, pos=r["board_origin"], up=r["up"],
                flip_u=rng_bit() & 1, flip_v=rng_bit() & 1, variant=rng_bit() & 3,
                intensity=(1.0 if ice else abs(r["steer"])) * 0.2,
                rgb=tuple(min(c, 1.0) for c in rgb),
                fade_in=0.0 if ice else 0.2, life=CARVE_CLOUD_LIFE,
                size_min=CARVE_CLOUD_SIZE_MIN, size_end=CARVE_CLOUD_SIZE_END)

def carve_cloud_draw(c: dict) -> Optional[dict]:
    """Sprite for one cloud (vfunc_72 screen sprite, additive, image 24 + variant)."""
    age = c["age"]
    if age > c["life"]:
        return None
    size = max(c["size_min"], c["size_end"] / c["life"] * age)
    if age < c["fade_in"]:
        a = c["intensity"] * age / c["fade_in"]
    else:
        a = (c["life"] - age) * c["intensity"] / (c["life"] - c["fade_in"])
    pos = _add(c["pos"], _mul(c["up"], size * 0.5))
    return dict(pos=pos, size=size, alpha_unit=a, rgb_unit=c["rgb"], texture=24 + c["variant"],
                blend=5)


# Landing / impact puffs (FX block +0x99B0: landing, cBoarder vfunc 12,
#   braking and trailing puffs; the puff draw is TODO)
PUFF_RING, PUFF_LIFE = 30, 2.0

def landing_puff_count(impact: float) -> int:
    """impact: the value the Air update passes (negative on landing; units TODO)."""
    return min(max(int(impact * -0.00090000004), 2), 8)

def landing_intensity_add(impact: float) -> float:
    return (0.2 - impact * 0.00051428576) * 1.5

class PuffEmitter:
    def __init__(self):
        self.acc = 0.0         # +0x968
        self.intensity = 0.0   # +0x96C
        self.puffs: List[dict] = []

    def _spawn(self, size):
        self.puffs.append(dict(age=0.0, size=size))
        if len(self.puffs) > PUFF_RING:
            self.puffs.pop(0)
        self.acc += 1.0

    def land(self, impact: float, contact_kind: int):
        if contact_kind == 2:
            return 0
        n = landing_puff_count(impact)
        for _ in range(n):
            self._spawn(0.1)
        self.intensity = min(self.intensity + landing_intensity_add(impact), 1.5)
        self.acc = 1.0
        return n

    def impact(self, contact_kind: int):
        if contact_kind == 2:
            return
        if self.acc < 0.7:
            self._spawn(0.1)
        self.intensity = min(self.intensity + 0.5, 2.0)

    def frame(self, r: dict):
        speed = math.sqrt(_dot(r["vel"], r["vel"]))
        if (r["contact_kind"] != 2 and abs(r["brake"]) > 0.7 and r["motion"] == MOTION_GROUND
                and speed > 277.77777):
            self.intensity = min(self.intensity + 0.05, 1.0)
        if self.intensity > 0.0:
            if self.acc < self.intensity * 0.1 + 0.5:
                self._spawn(self.intensity * 0.1 * 0.5)
            self.intensity -= FRAME
        self.acc *= 0.6666666
        for p in self.puffs:
            p["age"] += FRAME
        self.puffs = [p for p in self.puffs if p["age"] <= PUFF_LIFE]


# Boost afterimages (handler +0x1E0.. ring with image 29 'tral'). These are NOT rider copies:
# each ring entry is a pair of arm segments drawn as two fading ribbons (BoostRibbons below).
GHOST_RING = 7
GHOST_TINTS = {          # (pad, R, G, B) unit, RAM /240/250
    "low": (0.0, 1.0, 0.9645, 0.0),
    "mid": (0.0, 1.0, 0.4746, 0.0),
    "high": (0.0, 1.0, 0.0758, 0.002),
}

def ghost_tint(boost_strength: float, burst_timer: float) -> Optional[Tuple[float, ...]]:
    if not (boost_strength > 0.0 or burst_timer > 0.0):
        return None
    if burst_timer > 0.0 or boost_strength > 0.66599226:
        return GHOST_TINTS["high"]
    if boost_strength > 0.33362994:
        return GHOST_TINTS["mid"]
    return GHOST_TINTS["low"]

class Afterimages:
    def __init__(self):
        self.head = 0
        self.count = 0
        self.ring: List[Optional[tuple]] = [None] * GHOST_RING

    def frame(self, frame_no: int, boost_strength: float, burst_timer: float, pose=None):
        if frame_no % 3 != 0:
            return
        tint = ghost_tint(boost_strength, burst_timer)
        if tint is not None:
            self.ring[self.head] = (pose, tint)
            self.head = (self.head + 1) % GHOST_RING
            if self.count < GHOST_RING:
                self.count += 1
        elif self.count > 0:
            self.count -= 1

    @property
    def drawn(self) -> bool:
        return self.count > 1                     # drawn only when more than one snapshot


# Launch-boost trail ("PowerFX")
POWERFX_SLOTS, POWERFX_POOL = 12, 250
POWERFX_CONFIG = dict(
    rate=0.4455, rate_jitter=0.0, life_frames=60, life_jitter=0,
    colour0_argb=(1.0, 1.3134, 1.2892, 1.0417), colour0_jitter=(0, 0, 0, 0),
    colour1_argb=(0.0, 0.0, 0.0, 1.2399), colour1_jitter=(0, 0, 0, 0),
    size0=22.386, size0_jitter=0.0, size1=2.5005, size1_jitter=0.0,
    damping=0.99035, drift_per_frame=(0.0, 0.0, -52.3157 / 60.0),
    particle_texture=20, glow_texture=28, glow_size=18.872,
    glow_uv=((0.0, 0.0), (0.5, 0.5)), blend=3, emit_offset=3.068,
)

class PowerFXSlot:
    """One trail (two per rider: bone pairs +0x2F0->+0x330 and +0x1F0->+0x230 of the pose)."""

    def __init__(self, cfg=POWERFX_CONFIG, rng: Callable[[], float] = None):
        self.cfg = cfg
        self.rng = rng or (lambda: 0.5)
        self.acc = 0.0
        self.parts: List[dict] = []
        self.cur = None
        self.prev = None
        self.vel = (0.0, 0.0, 0.0)

    @staticmethod
    def emit_point(bone_a, bone_b, offset=POWERFX_CONFIG["emit_offset"]):
        d = _sub(bone_b, bone_a)
        return _add(bone_b, _mul(_norm(d), offset))

    def frame(self, point, rider_vel_per_frame, pool_free: int = POWERFX_POOL) -> int:
        c = self.cfg
        self.prev = self.cur if self.cur is not None else point
        self.cur = point
        d = _sub(_sub(self.cur, self.prev), rider_vel_per_frame)
        self.vel = _add(rider_vel_per_frame, _mul(d, 0.14930999))
        alive = []
        for p in self.parts:
            if p["frames"] == 0:
                continue
            p["rgba"] = _add(p["rgba"], p["drgba"])
            p["prev"] = p["pos"]
            p["size"] += p["dsize"]
            p["vel"] = _mul(p["vel"], c["damping"])
            p["pos"] = _add(p["pos"], _add(p["vel"], c["drift_per_frame"]))
            p["frames"] -= 1
            alive.append(p)
        self.parts = alive
        u = lambda: 2.0 * self.rng() - 1.0
        x = c["rate"] + c["rate_jitter"] * u() + self.acc
        n = int(x)
        self.acc = x - n
        made = 0
        for _ in range(n):
            if pool_free - made <= 0:
                break
            life = c["life_frames"] + int(c["life_jitter"] * u())
            s0 = c["size0"] + c["size0_jitter"] * u()
            s1 = c["size1"] + c["size1_jitter"] * u()
            c0 = _add(c["colour0_argb"], _mul(c["colour0_jitter"], u()))
            c1 = _add(c["colour1_argb"], _mul(c["colour1_jitter"], u()))
            self.parts.insert(0, dict(pos=self.cur, prev=self.cur, vel=self.vel, frames=life,
                                      size=s0, dsize=(s1 - s0) / life, rgba=c0,
                                      drgba=_mul(_sub(c1, c0), 1.0 / life)))
            made += 1
        return made


# Rider shadow (render-to-texture)
def shadow_texture_size(racer_count: int) -> int:
    return 256 if racer_count == 1 else 128

def shadow_silhouette_level(motion: int, height_above_ground: float) -> float:
    """k in the silhouette colour (1, k, k, k): 0.3 normally, down to 0 as the board sinks
    5 cm below the surface on the ground. Which of the two colours is fill vs edge: TODO."""
    h = 0.0
    if motion == MOTION_GROUND and height_above_ground < 0.0:
        h = max(height_above_ground, -5.0)
    return 0.3 - (h / -5.0) * 0.3

SHADOW_PROJECTION_DIR = (0.0, 0.0, -1.0)      # straight down (not the sun)
SHADOW_BOUNDS_MARGIN = 20.0                   # cm added around the bone extents


# Snowfall flake size (VU1 program 7)
def flake_half_size(a: float, b: float, u: float) -> float:
    """Slot stores 2a - b and b - a; the VU draws (2a - b) + (b - a) x r, r in [1, 2):
    uniform in [a, b) cm (world-space half-size of a screen-aligned square)."""
    return (2 * a - b) + (b - a) * (1.0 + u)

SNOW_BLEND_MODE = 5           # additive
SNOW_TEXTURE = 19


# Ground trail ("CircFIFOBuff", FX +0x8020) and the boost ribbons
TRAIL_ENTRIES = 100
TRAIL_FADE_FROM = 90                 # entries past 90: alpha -= 0.1 per push
TRAIL_PUSH_DIST = 110.141884         # cm: no push closer than this while ...
TRAIL_PUSH_SPEED = 138.88889         # ... the speed is at most 5 km/h, or ...
TRAIL_PUSH_DWIDTH = 10.0799465       # ... the width changed by <= this and
TRAIL_PUSH_COS = 0.99604887          # ... the direction turned by less than ~5.1 deg
TRAIL_LIFT = 10.0                    # cm along the ground normal (points) / toward the camera (draw)
TRAIL_SIDE_STEP = 1.0 / 30.0         # lit-side factor slew per frame
TRAIL_ALPHA_REF = 0.08
TRAIL_PASSES = ((4, -64), (8, 128))  # (ALPHA mode, alpha multiplier): darken Cd(1-As), then Cd As + Cd


def trail_surface_scale(surface: int) -> Optional[float]:
    """U per cm across the trail (S = width x scale); None = no trail on this surface."""
    if surface in (1, 2, 8, 15, 16):
        return 0.02855919
    if surface in (3, 4):
        return 0.016661247
    if surface == 5:
        return 0.19974777
    return None


def trail_active(surface: int, motion: int, contact_kind: int) -> bool:
    return (trail_surface_scale(surface) is not None and motion in (2, 5, 6)
            and not (contact_kind in (2, 3)))


def trail_side_weights(side: float, steer: float) -> Tuple[float, float]:
    """(k_A, k_B) for an entry: +-side x 2/3, the outer one stretched by steer (signed)."""
    k0, k1 = side * 0.6666667, -side * 0.6666667
    if steer > 0.0:
        k0 = steer * 0.5 * k0 + k0
    else:
        k1 = k1 * (1.0 - steer * 0.5)
    return k0, k1


def trail_side_step(side: float, lit_a: float, lit_b: float, active_before: bool) -> float:
    """side -> +-1 at once when the trail starts, else slews 1/30 per frame toward the lit edge."""
    if not active_before:
        return 1.0 if lit_b > lit_a else -1.0
    side += TRAIL_SIDE_STEP if lit_b > lit_a else -TRAIL_SIDE_STEP
    return min(max(side, -1.0), 1.0)


def trail_should_push(dist: float, speed: float, dwidth: float, cos_dir: float, active: bool) -> bool:
    if dist <= TRAIL_PUSH_DIST and active:
        if speed <= TRAIL_PUSH_SPEED:
            return False
        if abs(dwidth) <= TRAIL_PUSH_DWIDTH and cos_dir >= TRAIL_PUSH_COS:
            return False
    return True


def trail_vertex_alpha(k: float, alpha: float, pass_mult: int) -> int:
    return max(int(k * alpha * pass_mult), 0)


def trail_fade_tail(alphas: List[float]) -> List[float]:
    """After a push: every entry older than the newest 90 loses 0.1 alpha (clamped at 0)."""
    out = list(alphas)
    for j in range(TRAIL_FADE_FROM, len(out)):
        out[j] = max(out[j] - 0.1, 0.0)
    return out


def trail_stop_ramp(alphas: List[float]) -> List[float]:
    """Trail end (surface/motion no longer allows it): the newest entries get alpha x 0, 0.3, 0.6, 0.9
    (newest first) until the ramp reaches 1.  The live head is cleared but the count is kept: the old
    entries stay in the FIFO (tapered) and the next trail appends after them (RAM: 54 such entries)."""
    out, r = list(alphas), 0.0
    for j in range(len(out)):
        out[j] *= r
        r += 0.3
        if r >= 1.0:
            break
    return out


# Boost ribbons: one ring sample = two arm segments; per segment the outer point is the hand bone
# plus 15 cm along (hand - forearm) and the inner point is the upper bone.
RIBBON_SAMPLE_BONES = (((0x230, 0x1F0), 0x1B0), ((0x330, 0x2F0), 0x2B0))   # pose offsets (TODO names)
RIBBON_EXTEND = 15.001
RIBBON_ALPHA = (0.65, 0.455, 0.3185, 0.2229, 0.1561, 0.1092, 0.0765, 0.0)   # RAM 0x33F398
RIBBON_BLEND = 3                     # alpha blend, no depth write, alpha test off, sort mode 2


def ribbon_alphas(count: int) -> List[float]:
    """Outer-edge alpha of the live sample, then of the ring samples newest first (the newest ring
    sample repeats the live value); inner-edge alpha = the tint's pad byte = 0."""
    start = 7 - count
    return [RIBBON_ALPHA[start]] + [RIBBON_ALPHA[start + j] for j in range(count)]
