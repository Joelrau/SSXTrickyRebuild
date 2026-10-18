"""Prop / scene-node animation.

Clean-room behaviour model.  Covers:
  * the .pbd node keyframe format (cubic segments per channel) and its evaluation;
  * the AnimObject controller (factory subtypes 0x100..0x103) play state;
  * the CrowdBox sprite crowd and the crowd texture setup;
  * the Flag / Fence 8x2 vertex strips.
Constants are as compiled.
"""
from __future__ import annotations

import math
import struct
from dataclasses import dataclass, field
from typing import Callable, Dict, List, Optional, Sequence, Tuple

FRAME = 1.0 / 60.0
KEY_RATE = 30.0                      # keyframe times are frames / 30 (seconds)
DEG = 0.017453292

# Node keyframes (.pbd object header +0x10 -> animation record)
CHANNELS = ("tx", "ty", "tz", "rx", "ry", "rz")   # 0..5 used by cMeshAnimFrame; mask has 16 bits


@dataclass
class Segment:
    a: float
    b: float
    c: float
    d: float
    t0: float
    t1: float

    def value(self, t: float) -> float:
        return ((self.a * t + self.b) * t + self.c) * t + self.d


@dataclass
class NodeAnim:
    base: Tuple[float, ...]                    # 6 floats: tx, ty, tz, rx, ry, rz (degrees)
    mask: int                                  # bit k = channel k has a track
    tracks: Dict[int, List[Segment]]
    word1c: int                                # +0x1C: track count (== popcount(mask) on all data)

    def channel(self, k: int, t: float, cache: Optional[Dict[int, int]] = None) -> float:
        """Value of channel k at time t (seconds = frames/30).  Un-animated channels keep the base value.

        Segment search: the cached segment is reused when t0 <= t < t1, else the next one is tried, else a
        linear scan for the first segment with t < t1 (the last one if none).  Outside [t0, t1] the
        polynomial is evaluated at the clamped end (t0 below, t1 above)."""
        segs = self.tracks.get(k)
        if not segs:
            return self.base[k] if k < 6 else 0.0
        i = cache.get(k, 0) if cache is not None else 0
        s = segs[i] if i < len(segs) else segs[0]
        if not (s.t0 <= t < s.t1):
            i = i + 1
            if i < len(segs) and segs[i].t0 <= t < segs[i].t1:
                s = segs[i]
            else:
                i = 0
                while i < len(segs) - 1 and not (t < segs[i].t1):
                    i += 1
                s = segs[i]
            if cache is not None:
                cache[k] = i
        tt = s.t0 if t < s.t0 else (s.t1 if t > s.t1 else t)
        return s.value(tt)

    def matrix(self, t: float) -> List[List[float]]:
        """4x4 row-vector matrix built like cMeshAnimFrame (angles in degrees, negated)."""
        tx, ty, tz, rx, ry, rz = (self.channel(k, t) for k in range(6))
        sa, ca = math.sin(-rx * DEG), math.cos(-rx * DEG)
        sb, cb = math.sin(-ry * DEG), math.cos(-ry * DEG)
        sg, cg = math.sin(-rz * DEG), math.cos(-rz * DEG)
        return [[cb * cg, -cb * sg, sb, 0.0],
                [sa * sb * cg + ca * sg, ca * cg - sa * sb * sg, -sa * cb, 0.0],
                [sa * sg - ca * sb * cg, ca * sg * sb + sa * cg, ca * cb, 0.0],
                [tx, ty, tz, 1.0]]


def parse_node_anim(d: bytes, a: int) -> NodeAnim:
    """Record at file offset a (object header offset + header word +0x10).  All inner offsets are
    relative to the record start (track-pointer table) or to the track start (segments)."""
    base = struct.unpack_from("<6f", d, a)
    mask, ntr, tab = struct.unpack_from("<3I", d, a + 0x18)
    chans = [k for k in range(16) if mask >> k & 1]
    tracks = {}
    for j, c in enumerate(chans):
        tp = a + struct.unpack_from("<I", d, a + tab + 4 * j)[0]
        n, so = struct.unpack_from("<2I", d, tp)
        tracks[c] = [Segment(*struct.unpack_from("<6f", d, tp + so + 24 * s)) for s in range(n)]
    return NodeAnim(base, mask, tracks, ntr)


def model_objects(level, model_index: int):
    """(object index, header offset, header words) for every object of a .pbd model (rebuild tools/pbd)."""
    d, off = level.raw, level.offsets
    m = off["models"] + level.model_pointers[model_index]
    _total, oc, oo, _mb = struct.unpack_from("<4i", d, m)
    for k in range(oc):
        o = m + oo + 24 * k
        yield k, o, struct.unpack_from("<6i", d, o)


def model_anim_end_frame(level, model_index: int) -> float:
    """Model header +0x14 (f32): the default end frame used when an AnimObject record's end is < 0."""
    d, off = level.raw, level.offsets
    return struct.unpack_from("<f", d, off["models"] + level.model_pointers[model_index] + 0x14)[0]


def model_node_anims(level, model_index: int) -> Dict[int, NodeAnim]:
    out = {}
    for k, o, f in model_objects(level, model_index):
        if f[4] not in (0, -1):
            out[k] = parse_node_anim(level.raw, o + f[4])
    return out


# AnimObject controller
MODE_ONCE, MODE_LOOP, MODE_PINGPONG = 0, 1, 2


@dataclass
class AnimObjectRecord:
    mode: int            # payload word 1 (low i16): 0 once, 1 loop, 2 ping-pong
    start: float         # word 2, frames (clamped >= 0)
    end: float           # word 3, frames (< 0: model header +0x14)
    speed: float         # word 4, keyframes per second (30 = real time)
    speed_rand: float    # word 5: if != 0 speed = speed + speed_rand * U(-1, 1)
    cooldown_s: float    # word 6: touch cooldown (s)
    random_start: int    # word 7: != 0 -> start time uniform in [start, end)
    direction: int       # word 8: 4 = play backwards

    @staticmethod
    def from_payload(words_f: Sequence[float], words_i: Sequence[int]) -> "AnimObjectRecord":
        # words_* index 0 = subtype; the mode is the low 16 bits of word 1
        mode = struct.unpack("<h", struct.pack("<H", words_i[1] & 0xFFFF))[0]
        return AnimObjectRecord(mode, words_f[2], words_f[3], words_f[4], words_f[5], words_f[6], words_i[7], words_i[8])


class AnimObject:
    """Play state of one AnimObject node.  t is in seconds (frames / 30)."""

    def __init__(self, rec: AnimObjectRecord, model_end_frame: float = 0.0,
                 rand_range: Callable[[float, float], float] = None, created_by_touch: bool = False):
        self.mode = rec.mode
        self.t_start = max(rec.start, 0.0) / KEY_RATE
        end = rec.end if rec.end >= 0.0 else model_end_frame
        self.t_end = end / KEY_RATE
        self.t = self.t_start
        if rec.random_start and rand_range:
            self.t = rand_range(self.t_start, self.t_end)
        spd = rec.speed / KEY_RATE
        if rec.speed_rand != 0.0 and rand_range:
            spd = spd + (rec.speed_rand / KEY_RATE) * rand_range(-1.0, 1.0)
        self.rate = spd * FRAME
        if rec.direction == 4:
            self.rate = -self.rate
        self.cooldown_frames = int(rec.cooldown_s * 60.0)
        self.cooldown = self.cooldown_frames if created_by_touch else 0
        self.running = True          # AnimDelta starts stopped (budget model below)
        self.finished = False
        self.delay = 0               # message 1: frames before playing

    def message(self, msg: int, value: float):
        if msg == 1:
            self.delay = int(value * 60.0)
        elif msg == 4:
            t = value / KEY_RATE
            if self.t_start <= t <= self.t_end:
                self.t = t

    def touch(self) -> bool:
        """Rider touch (slot 18): runs the instance's slot-1 effect when the cooldown is 0."""
        if self.cooldown == 0:
            self.cooldown = self.cooldown_frames
            return True
        return False

    def frame(self) -> bool:
        """One 60 Hz update.  Returns True on the frame the node calls done(1) (play-once end):
        the base handler then runs the slot-3 effect, or restores the flags and destroys the node
        (the instance falls back to its static node matrices)."""
        if self.cooldown > 0:
            self.cooldown -= 1
        if self.finished:
            return False
        if self.running:
            if self.delay <= 0:
                self.t += self.rate
                if self.rate >= 0.0:
                    if self.t >= self.t_end:
                        if self.mode == MODE_PINGPONG:
                            self.t = self.t_end - (self.t - self.t_end)
                            self.rate = -self.rate
                        elif self.mode == MODE_LOOP:
                            self.t = self.t_start + (self.t - self.t_end)
                        else:
                            self.t = self.t_end
                            self.finished = self.mode == MODE_ONCE
                elif self.t <= self.t_start:
                    if self.mode == MODE_PINGPONG:
                        self.t = self.t_start + (self.t_start - self.t)
                        self.rate = -self.rate
                    elif self.mode == MODE_LOOP:
                        self.t = self.t_end - (self.t_start - self.t)
                    else:
                        self.t = self.t_start
                        self.finished = self.mode == MODE_ONCE
            if self.delay > 0:
                self.delay -= 1
        return self.finished


# CrowdBox
CROWD_ANIMS = (
    (0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15),
    (0, 1, 2, 3, 2, 1, 0, 7, 8, 9, 10, 11, 12, 13, 14, 15),
    (0, 1, 2, 3, 4, 5, 6, 7, 6, 5, 4, 3, 2, 1, 0, 0),
    (0, 0, 0, 0, 0, 1, 2, 3, 4, 5, 6, 5, 4, 3, 2, 1),
)
CROWD_PAUSE_CHANCE = (256, 0, 50, 100, 200, 100, 50)   # out of 256, by state 0..6 (state 7 never pauses)
CROWD_SLOTS = 16
CROWD_TICK = 5                                          # race frames between crowd steps
CROWD_RANGE = 20000.0                                   # cm, human riders that drive the state
CROWD_TEXTURES = ("data/textures/crowd.ssh", 16)        # images cd00..cd15, 128x128, loaded at track load


class CrowdBox:
    """rows x cols people (record words 2, 3); person i shows crowd texture frame(i)."""

    def __init__(self, rows: int, cols: int, rand_u32: Callable[[], int]):
        self.rows, self.cols = rows, cols
        self.state = 0
        self.counter = [0] * CROWD_SLOTS
        self.anim = [0] * CROWD_SLOTS
        for k in range(CROWD_SLOTS):
            self.counter[k] = rand_u32() & 0xF
            self.anim[k] = rand_u32() & 3
        self.rand = rand_u32
        self.frames = [0] * CROWD_SLOTS
        self._step()

    def update(self, race_frame: int, race_phase: int, humans: Sequence[Tuple[float, int]]):
        """humans: (distance to the instance origin, crowd-audio level of that player) per human rider.
        Runs only when race_frame % 5 == 0."""
        if race_frame % CROWD_TICK:
            return
        if race_phase == 1:
            self.state = 7
        else:
            self.state = 0
            for dist, level in humans:
                if dist <= CROWD_RANGE:
                    self.state = (level + 4) & 7
        self._step()

    def _step(self):
        if self.state != 0:
            for k in range(CROWD_SLOTS):
                self.counter[k] += 1
                c = self.counter[k]
                if c > 15 and (c & 0xF) == 0:
                    r = self.rand()
                    self.anim[k] = r & 3
                    if self.state < 7 and (r & 0xFF) < CROWD_PAUSE_CHANCE[self.state]:
                        self.counter[k] = -(r & 0x3F)
        for i in range(self.rows * self.cols):
            c = self.counter[i]
            seq = CROWD_ANIMS[self.anim[i]]
            self.frames[i] = seq[0] if c < 0 else seq[(c + i) & 0xF]


def crowd_frame(counter: int, anim: int, i: int) -> int:
    seq = CROWD_ANIMS[anim]
    return seq[0] if counter < 0 else seq[(counter + i) & 0xF]


# Flag / Fence strips
STRIP_COLS = 8                       # 8 columns x 2 rows = 16 vertices, lerp k/7 between the mesh corners
FENCE_PROFILE = (0.0, 0.3827, 0.7071, 0.9239, 0.9239, 0.7071, 0.3827, 0.0)


@dataclass
class Flag:
    """Record: word1 direction (0: offsets on columns 1..7, 1: reversed), word2 Hz (-1 -> U(0.5, 2)),
    word3 amplitude (-1 -> U(5, 50)), word4 seconds (0 = forever; > 0 -> done(1) at the end)."""
    direction: int
    rate: float                      # cycles per frame
    amplitude: float                 # after the model-scale division
    phase: float
    timer: int

    def frame(self) -> bool:
        if self.timer > 0:
            self.timer -= 1
            if self.timer == 0:
                return True          # done(1)
        self.phase += self.rate
        if self.phase >= 1.0:
            self.phase -= 1.0
        return False

    def offsets(self) -> List[float]:
        """Local-X offset of strip column 0..7 (both rows move together).  Column 0 is the pole."""
        out = [0.0] * STRIP_COLS
        for k in range(1, STRIP_COLS):
            v = self.amplitude * (k / 8.0) * math.sin(-2.0 * math.pi * self.phase + 2.0 * math.pi * k / 8.0)
            # 0x144EC8: dir 1 writes k = 1..7 to columns 6..0 and column 7 (the mirrored pole) stays 0
            out[k if self.direction == 0 else STRIP_COLS - 1 - k] = v
        return out


def flag_from_record(w1: int, hz: float, amp: float, seconds: float, model_scale: float,
                     rand01: Callable[[], float]) -> Flag:
    rate = hz * FRAME
    if amp == -1.0:                  # as compiled: the random amplitude is also scaled by 1/60
        amp = (rand01() * 45.0 + 5.0) * FRAME
    if rate == -1.0:                 # as compiled: compares hz/60 with -1 (only hz = -60 randomises)
        rate = (rand01() * 1.5 + 0.5) * FRAME
    s = max(model_scale, 1.0) * 250.0
    return Flag(w1, rate, amp / s, rand01(), int(seconds * 60.0))


@dataclass
class Fence:
    """theta (+0x34), amplitude A (+0x38), rate (+0x3C): a triangular swing that halves each half-cycle."""
    scale: float                     # record word 2 / (model scale * 250)
    theta: float = 0.0
    amp: float = 0.0
    rate: float = 0.0

    def hit(self, amount: float):
        """Wobble start: amount = (n . v_hat) a 0.001."""
        self.amp = min(self.amp + abs(amount), 1.0)
        sgn = (amount < 0.0) if self.rate == 0.0 else (self.rate < 0.0)
        self.rate = self.amp * (-0.1 if sgn else 0.1)

    def frame(self) -> bool:
        self.theta += self.rate
        if self.amp < 0.01 and abs(self.theta) < 0.01:
            return True              # done(1): node destroyed, fence static again
        if self.rate >= 0.0:
            if self.theta >= self.amp:
                self.theta = self.amp
                self.amp *= 0.5
                self.rate = -self.amp * 0.1
        elif self.theta <= -self.amp:
            self.theta = -self.amp
            self.amp *= 0.5
            self.rate = self.amp * 0.1
        return False

    def offsets(self) -> List[float]:
        return [self.theta * self.scale * 200.0 * p for p in FENCE_PROFILE]
