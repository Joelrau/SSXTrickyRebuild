"""Prop controller nodes: UV scroll, texture flipbook, fence, cracked.

Clean-room reference model written from the behaviour spec; no game code. Used by the port's
SSX.World.Controllers test (Unreal/SSXTrickyRebuild Source/SSXTrickyRebuild/World/PropControllers.*).

UVScroll (factory subtype 10) [update 0x1422E0, ctor 0x141FF0], .ssf node record 36 bytes:
    +0x0C i32 mode, +0x10 f32 du, +0x14 f32 dv (UV units per 60 Hz frame), +0x18 f32 run time A (s),
    +0x1C f32 pause time B (s), +0x20 f32 start delay (s)
  - the start delay ((int)(s x 60) frames) counts down without holding the scroll; its expiry frame calls slot 22
    with 1 and does nothing else;
  - clock += 1/60 first; paused: at >= B the clock restarts and the run resumes the next frame (if A > 0); running:
    at >= A the clock restarts, a pause starts if B > 0 and modes 1 / 2 negate (du, dv), then the step (unless paused);
  - mode 0 / 2: offset += (du, dv); mode 1: eased, (du, dv) min(t, A - t) / A;
  - offsets wrap by one +-1 step when beyond [-1, 1].
TexFlip (11), record 32 bytes:
    +0x0C i32 start frame (< 0 = random start; -2 also = random frame at every step), +0x10 i32 direction
    (0 fwd, 1 back), +0x14 f32 frames per second, +0x18 f32 start delay (s), +0x1C i32 mode (0 step;
    1/2 = flicker: alternating steps 6 frames apart and base-rate x U[0.25, 1) apart).
  The frame list is the .pbd texture flipbook {i32 n; n x i32 texture index} bound to the model's meshes,
  so the frame count n is an input. Random numbers are stream A uniforms r in [0, 1) (models/rng.py).
UVScrollTexFlip (19) combines both.
Contact methods (touch handler slot 18 corrects step 4):
  - UVScroll / UVScrollTexFlip / TexFlip: if the touch timer is < 1, run the instance's slot-1 effect and set the
    timer to 30 frames (it counts down in the node update and starts at 30). Touches do NOT start the scroll;
  - Fence: if its +0x38 < 1: wobble amount (n . v_hat) a 0.001 and arm two 4-frame timers;
  - Cracked: if its cooldown is 0 and health > 0: cooldown = 30 frames, health -= |(v_hat . n) a| 0.036.
  n = the contact normal after the Ground tangent projection, v_hat = unit(v), a = -n . v.
Cracked (14), record 20 bytes: +0x0C f32 timer seconds, +0x10 f32 health. Per frame: a timer
  > 0 counts down, at 0 the node is done (slot 22 done(1)); health <= 0: run slot 4, or without one become a
  cDeadNode.

Timers count down by 1 per frame; Cracked timer frames = (int)(seconds x 60.0f) (ctor 0x145458).
Not decoded: what the fence timers drive (the wobble shape).
"""
from __future__ import annotations

import math
from dataclasses import dataclass, field
from typing import List, Tuple

FRAME_DT = 1.0 / 60.0
TOUCH_TIMER_FRAMES = 30
FENCE_TIMER_FRAMES = 4
FENCE_WOBBLE_K = 0.001
CRACKED_COOLDOWN_FRAMES = 30
CRACKED_DAMAGE_K = 0.036

SUBTYPE_UVSCROLL, SUBTYPE_TEXFLIP, SUBTYPE_FENCE, SUBTYPE_CRACKED, SUBTYPE_UVSCROLL_TEXFLIP = 10, 11, 12, 14, 19


@dataclass
class UVScroll:
    """cUVScrollNode update 0x1422E0 (single precision, EE FPU adds), ctor 0x141FF0."""
    mode: int = 0
    du: float = 0.0
    dv: float = 0.0
    run_time: float = 0.0      # A
    pause_time: float = 0.0    # B
    delay: float = 0.0
    # state
    active: bool = False       # message 1 (not read by the update)
    delay_frames: int = -1     # +0x38 = (int)(delay x 60.0f)
    done_calls: int = 0        # slot 22 done(1) at the delay expiry
    running: bool = True       # +0x48 starts 1
    clock: float = 0.0         # +0x44
    offset: List[float] = field(default_factory=lambda: [0.0, 0.0])
    touch_timer: int = TOUCH_TIMER_FRAMES

    def activate(self) -> None:
        self.active = True

    def on_touch(self) -> bool:
        """True when the instance's slot-1 effect runs."""
        if self.touch_timer < 1:
            self.touch_timer = TOUCH_TIMER_FRAMES
            return True
        return False

    def step(self) -> None:
        from fpu import fpu_add, fpu_sub, f32
        if self.touch_timer > 0:
            self.touch_timer -= 1
        if self.delay_frames < 0:
            self.delay_frames = int(f32(self.delay) * f32(60.0))
        if self.delay_frames > 0:
            self.delay_frames -= 1
            if self.delay_frames == 0:
                self.done_calls += 1
                return
        a, b = f32(self.run_time), f32(self.pause_time)
        if a <= 0.0 and b <= 0.0:
            return
        self.clock = float(fpu_add(self.clock, f32(0.016666668)))
        c = f32(self.clock)
        if not self.running:
            if c < b:
                return
            self.clock = 0.0
            if a > 0.0:
                self.running = True
            return
        if a <= c:
            self.clock = 0.0
            c = f32(0.0)
            if b > 0.0:
                self.running = False
            if self.mode in (1, 2):
                self.du, self.dv = float(-f32(self.du)), float(-f32(self.dv))
        if not self.running:
            return
        u, v = f32(self.offset[0]), f32(self.offset[1])
        du, dv = f32(self.du), f32(self.dv)
        if self.mode == 1:
            if du < 0.0:
                t = c if c < a * f32(0.5) else fpu_sub(a, c)
            else:
                r = fpu_sub(a, c)
                t = r if r < a * f32(0.5) else fpu_sub(a, r)
            inv = f32(1.0) / a
            u = fpu_add(u, du * f32(t) * inv)
            v = fpu_add(v, dv * f32(t) * inv)
        else:
            u = fpu_add(u, du)
            v = fpu_add(v, dv)
        u = fpu_sub(u, 1.0) if u > 1.0 else (fpu_add(u, 1.0) if u < -1.0 else u)
        v = fpu_sub(v, 1.0) if v > 1.0 else (fpu_add(v, 1.0) if v < -1.0 else v)
        self.offset = [float(u), float(v)]


@dataclass
class TexFlip:
    """. frame_count = n of the bound .pbd flipbook. rng() -> stream A uniform in [0, 1)."""
    start_frame: int = 0
    direction: int = 0
    fps: float = 0.0
    delay: float = 0.0
    mode: int = 0
    frame_count: int = 1
    rng: object = None         # callable returning [0, 1); defaults to random.random (stream A in the game)
    # state
    rate: float = 0.0          # frames advanced per 60 Hz frame
    base_rate: float = 0.0
    phase: float = 0.0
    frame: int = 0
    random_steps: bool = False
    delay_frames: int = 0
    done_calls: int = 0        # times the delay expiry called the node's slot-22 handler with 1
    touch_timer: int = TOUCH_TIMER_FRAMES

    def __post_init__(self):
        import random
        if self.rng is None:
            self.rng = random.random
        self.delay_frames = int(self.delay * 60.0)
        self.rate = self.base_rate = self.fps / 60.0
        f = self.start_frame
        if f < 0:                                   # random start: ceil(8 r), then clamped by set_frame
            f = math.ceil(8.0 * self.rng())
        self.random_steps = (self.start_frame == -2)
        self.set_frame(f)

    def set_frame(self, f: int) -> None:
        """Bind the frame (also message 2 / 3): negative -> random in 1..n-1 (1 when n <= 2); >= n -> n - 1."""
        n = self.frame_count
        if f < 0:
            f = math.ceil((n - 1.0) * self.rng() + 1.0) if n > 2 else 1
        self.frame = min(f, n - 1)

    def on_touch(self) -> bool:
        """True when the instance's slot-1 effect runs."""
        if self.touch_timer < 1:
            self.touch_timer = TOUCH_TIMER_FRAMES
            return True
        return False

    def step(self) -> None:
        if self.touch_timer > 0:
            self.touch_timer -= 1
        if self.delay_frames > 0:                   # the delay does not hold the animation
            self.delay_frames -= 1
            if self.delay_frames == 0:
                self.done_calls += 1
                return
        if self.rate <= 0.0:
            return
        self.phase += self.rate
        if self.phase < 1.0:
            return
        self.phase -= 1.0
        n, f = self.frame_count, self.frame
        if self.mode == 0:
            if self.random_steps and n > 1:
                f = math.ceil((n - 1.0) * self.rng())
            elif self.direction == 0:
                f = f + 1 if f + 1 < n else 0
            else:
                f = f - 1 if f - 1 >= 0 else n - 1
        elif self.mode == 1:
            self.mode = 2
            f = f + 1 if f + 1 < n else 0
            self.rate = 1.0 / 6.0
        elif self.mode == 2:
            self.mode = 1
            f = f + 1 if f + 1 < n else 0
            self.rate = self.base_rate * (0.25 + 0.75 * self.rng())
        self.set_frame(f)


@dataclass
class Fence:
    gate38: float = 0.0        # node +0x38 (TODO: what writes it)
    wobble: float = 0.0
    timers: Tuple[int, int] = (0, 0)

    def on_touch(self, n, v_hat, a: float) -> bool:
        if self.gate38 < 1.0:
            self.wobble = (n[0] * v_hat[0] + n[1] * v_hat[1] + n[2] * v_hat[2]) * a * FENCE_WOBBLE_K
            self.timers = (FENCE_TIMER_FRAMES, FENCE_TIMER_FRAMES)
            return True
        return False

    def step(self) -> None:
        self.timers = tuple(max(t - 1, 0) for t in self.timers)


@dataclass
class Cracked:
    health: float = 0.0        # record +0x10
    timer: int = -60           # record +0x0C seconds x 60 (retail -1 s: never expires)
    cooldown: int = CRACKED_COOLDOWN_FRAMES   # starts at 30 (models/effects.py INITIAL_TOUCH_COOLDOWN)
    done: bool = False         # timer ran out: done(1) (slot 3 effect, else the node goes)
    broken: bool = False       # health <= 0: slot 4 effect, else the node becomes a cDeadNode

    def on_touch(self, n, v_hat, a: float) -> bool:
        if self.cooldown == 0 and self.health > 0.0:
            self.cooldown = CRACKED_COOLDOWN_FRAMES
            self.health -= abs((v_hat[0] * n[0] + v_hat[1] * n[1] + v_hat[2] * n[2]) * a) * CRACKED_DAMAGE_K
            return True
        return False

    def step(self) -> None:
        if self.cooldown > 0:
            self.cooldown -= 1
        if self.timer > 0:
            self.timer -= 1
            if self.timer == 0:
                self.done = True
        if self.health <= 0.0:
            self.broken = True


def uvscroll_from_record(words, floats) -> UVScroll:
    """words / floats: the 36-byte record as i32 / f32 arrays (index 3 = +0x0C)."""
    return UVScroll(mode=int(words[3]), du=float(floats[4]), dv=float(floats[5]), run_time=float(floats[6]),
                    pause_time=float(floats[7]), delay=float(floats[8]))


def texflip_from_record(words, floats, frame_count: int = 1) -> TexFlip:
    return TexFlip(start_frame=int(words[3]), direction=int(words[4]), fps=float(floats[5]), delay=float(floats[6]),
                   mode=int(words[7]), frame_count=frame_count)


def scan_ssf(path):
    """Structural scan of the UVScroll / TexFlip node records in an .ssf ({0, size, subtype, payload})."""
    import numpy as np
    d = open(path, "rb").read()
    w = np.frombuffer(d[:len(d) // 4 * 4], "<i4")
    f = w.view("<f4")
    out = []
    for i in np.nonzero((w[:-3] == 0) & np.isin(w[2:-1], (SUBTYPE_UVSCROLL, SUBTYPE_TEXFLIP)))[0]:
        size, sub = int(w[i + 1]), int(w[i + 2])
        if (sub == SUBTYPE_UVSCROLL and size == 36) or (sub == SUBTYPE_TEXFLIP and size == 32):
            out.append((int(i) * 4, sub, w[i:i + size // 4].tolist(), f[i:i + size // 4].tolist()))
    return out


def trace_uvscroll(s: UVScroll, frames: int):
    rows = []
    for k in range(frames):
        s.step()
        rows.append((k + 1, s.offset[0], s.offset[1]))
    return rows


if __name__ == "__main__":
    import sys
    for p in sys.argv[1:]:
        recs = scan_ssf(p)
        print(p, len(recs), "records")
        for off, sub, w, f in recs:
            if sub == SUBTYPE_UVSCROLL:
                s = uvscroll_from_record(w, f)
                print(hex(off), "UVScroll", s.mode, s.du, s.dv, s.run_time, s.pause_time, s.delay,
                      [tuple(round(x, 6) for x in r[1:]) for r in trace_uvscroll(s, 120)[59::60]])
            else:
                t = texflip_from_record(w, f)
                print(hex(off), "TexFlip", t.start_frame, t.direction, t.fps, t.delay, t.mode)
