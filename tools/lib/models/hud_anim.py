"""In-race HUD animation reference model: boost meter, pending trick preview, points number with
its tier pop and plate, flying point popups, the Tricky orb (bounce + coin spin), the TRICKY letters,
the uber hint, and the Tricky audio cue schedule.

Clean-room: behaviour only.
Frame = one game frame (60 Hz). Layout = layout 0 (one player).
"""
from __future__ import annotations

import math
from dataclasses import dataclass, field
from typing import List, Optional, Tuple

TRICK_TO_METER = 0.6786545
SLEW = 0.0050000004                   # per frame, displayed meter and pending preview
SEG = 0.071428575                     # 1/14
# segment table {scale_lo, scale_hi, lit sprite, dx, dy}. The ELF holds static values but the HUD
# constructor rebuilds the sprite / dy columns: tier by the segment's top fraction (i+1)/14 against 1/3 and
# 0.666 (yellow 0x14 / orange 0x13 / red 0x12), dy steps -18 per segment and -28 across a tier change.
def _seg_table():
    rows, y, prev_hi = [(0.2, 1.0, 0x14, 0.0, 288.0)], 288.0, SEG
    for i in range(1, 14):
        hi = prev_hi + SEG
        tier = 2 if hi >= 0.66599226 else (1 if hi >= 0.33362994 else 0)
        prev_tier = 2 if prev_hi >= 0.66599226 else (1 if prev_hi >= 0.33362994 else 0)
        y -= 28.0 if tier != prev_tier else 18.0
        rows.append((0.2, 1.0, (0x14, 0x13, 0x12)[tier], 0.0, y))
        prev_hi = hi
    return rows


SEG_TABLE = _seg_table()
SEG_EMPTY_SPRITE = 0x15
# layout 0 records used here: (x, y, flags, sx, sy)
L = {4: (620, 24, 0x8, 1.1, 1), 14: (570, 140, 0x0, 1, 1), 15: (0, 34, 0x20, 1, 254),
     22: (0, 10, 0x60, 1.25, 1.25), 23: (0, 5, 0x60, 1, 1),
     24: (-42, -25, 0x60, 1, 1), 25: (-27, -35, 0x60, 1, 1), 26: (-13, -43, 0x60, 1, 1),
     27: (2, -42, 0x60, 1, 1), 28: (20, -41, 0x60, 1, 1), 29: (35, -24, 0x60, 1, 1),
     30: (320, 80, 0xA0, 0.75, 1.4)}
#: points tiers {min, A, R, G, B}
TIERS = [(0, 0.85, 0.4706, 0.8627, 0.0), (1001, 0.7, 0.1765, 0.7686, 1.0), (2501, 0.85, 0.9961, 0.8627, 0.1176),
         (4001, 0.85, 0.9922, 0.5882, 0.1451), (7501, 0.85, 0.7412, 0.2353, 0.2353), (11501, 0.7, 0.1059, 0.1255, 0.2235)]
#: TRICKY letters {layout record, unlit sprite, lit sprite}
LETTERS = [(24 + k, 0x19 + k, 0x1F + k) for k in range(6)]


def pop(p: float, k: float) -> float:
    """HUD pop curve: damped sine pop, 2 cycles over the first quarter of the progress."""
    if p <= 0.25:
        a = (1.0 - p) * 0.5
        if a >= 0.05:
            return math.sin(p * math.pi * 4.0) * a * k
    return 0.0


def approach(cur: float, tgt: float, step: float) -> float:
    if tgt + step < cur:
        return cur - step
    if cur < tgt - step:
        return cur + step
    return tgt


def round10(x: float) -> int:
    """Points rounding: (int)(x + 5) minus its remainder mod 10 (0 for x < 0)."""
    if x < 0:
        return 0
    i = int(x + 5.0)
    return i - i % 10


def tier_of(points: float) -> int:
    """Points tier: highest tier whose min <= points (0 if none)."""
    for t in range(5, -1, -1):
        if TIERS[t][0] <= points:
            return t
    return 0


@dataclass
class HudPlayer:
    pts_roll: int = 0          # S[1]  rolling multiplied points (only drives the tick sound)
    pts_pending: int = 0       # S[2]  displayed pending points (unmultiplied)
    seg_scale: List[float] = field(default_factory=lambda: [0.2] * 14)   # S[3+2i]
    seg_lit: List[int] = field(default_factory=lambda: [0] * 14)         # S[4+2i]
    meter: float = 0.0         # S[0x1F] displayed boost
    pending: float = 0.0       # S[0x20] displayed pending reward
    pulse: float = 1.0         # S[0x21]
    chain_p: float = 0.0       # S[0x22]
    flash_ctr: int = 0         # S[0x27] (counter mod 54; the flag S[0x28] has no reader)
    flash: int = 0             # S[0x28]
    orb_drawn: int = 0         # S[0x2A] (never cleared: the chain bar can never draw once the orb did)
    bounce: float = 0.0        # S[0x2B]
    phase: float = 1.0         # S[0x2C]
    tier: int = -1             # S[0x2D]
    tier_pop: float = 0.0      # S[0x2E]
    tier_t0: float = 1.0       # S[0x2F] frame-counter value at pop start
    letters_drawn: int = 0     # S[0x30]

    def update(self, *, boost_meter: float, V: float, M_pts: float, control_state: int = 0, surface: int = 1,
               popups: Tuple[Tuple[int, float], ...] = (), points_bit: bool = True) -> List[str]:
        """One frame. popups = queue entries (type, progress) in slot order. Returns sound events."""
        ev: List[str] = []
        # air / wipe-out flash counter (S[0x27], S[0x28])
        self.flash = 0
        if control_state == 0x13 or surface == 0:
            self.flash_ctr = (self.flash_ctr + 1) % 54
            if self.flash_ctr > 18:
                self.flash = 1
        else:
            self.flash_ctr = 0
        # points: S[1] rolls +-50 toward the multiplied value; S[2] = unmultiplied pending
        tgt = round10(V * TRICK_TO_METER * M_pts * 10000.0)
        if tgt < 1 or tgt < self.pts_roll:
            self.pts_roll = 0
            self.pts_pending = 0
        else:
            old = self.pts_roll
            if old > tgt + 50:
                new = old - 50
            elif old < tgt - 50:
                new = old + 50
            else:
                new = tgt
            self.pts_roll = new
            self.pts_pending = round10(V * TRICK_TO_METER * 10000.0)
            if points_bit and old % 100 > new % 100:
                ev.append("tick")                       # points tick: zbxsfx #77, single voice
        # displayed meter and pending preview
        self.meter = approach(self.meter, boost_meter, SLEW)
        self.pending = approach(self.pending, max(0.0, V * TRICK_TO_METER), SLEW)
        r = self.meter
        for i in range(14):
            if r > 0.0:
                self.seg_lit[i] = 1
                t = r
            else:
                self.seg_lit[i] = 0
                t = self.pending + r
            t = min(1.0, max(0.0, t / SEG))
            lo, hi = SEG_TABLE[i][0], SEG_TABLE[i][1]
            self.seg_scale[i] = t * hi + (1.0 - t) * lo
            r -= SEG
        # pulse: the LAST live type 2/3 entry in slot order wins
        self.pulse, self.chain_p = 1.0, 0.0
        for typ, p in popups:
            if typ in (2, 3):
                self.pulse = 1.0 + pop(p, 1.4)
                self.chain_p = p
        return ev

    def draw_meter(self, hud_alpha: float = 1.0):
        """Returns (sprites, alpha_after) ; sprite = (id, x, y, sx, sy, flags, alpha)."""
        a = 0.5 if self.seg_scale[0] == SEG_TABLE[0][0] else hud_alpha
        bx, by = L[14][0], L[14][1]
        out = []
        for i, (lo, hi, spr, dx, dy) in enumerate(SEG_TABLE):
            s = self.seg_scale[i]
            sx = L[14][3] * (self.pulse if self.seg_lit[i] else 1.0) * s
            out.append((spr if self.seg_lit[i] else SEG_EMPTY_SPRITE, bx + dx, by + dy, sx, s, 0x60, a))
        out.append((0x17, bx + L[15][0], by + L[15][1], L[15][3], L[15][4], L[15][2], a))
        return out, a

    @staticmethod
    def fold(ph: float) -> float:
        """Orb width factor from the spin phase (pre-decrement value): 1 -> -1 -> 0 -> 1 triangle."""
        if ph <= -3.0:
            return 1.0 - abs(ph + 3.0)
        if ph <= -2.0:
            return abs(ph + 2.0)
        if ph < -1.0:
            return -(ph + 2.0)
        return ph

    def draw_orb(self, tricky_timer: float, racing: bool, meter_alpha: float):
        """Orb update + sprite. Runs only when HUD bit 0x2000 is on (state lives in the draw)."""
        self.orb_drawn = 1
        bx, by = L[14][0] + L[22][0], L[14][1] + L[22][1]
        if tricky_timer > 0.0:
            u = 20.0 - tricky_timer
            f = u / 0.7 if u < 0.7 else tricky_timer / 19.3
            old = self.phase
            self.bounce = f * 40.0
            if racing:
                self.phase = old - (1.0 - (f * 40.0) / 40.0) * 0.15
            w = self.fold(old)
            if old <= -3.0:
                self.phase = w                          # wrap overwrites this frame's decrement
            return (0x25, bx, by - self.bounce, L[22][3] * w, L[22][4], L[22][2], 1.0 * meter_alpha)
        self.phase, self.bounce = 1.0, 0.0
        return (0x26, bx, by, L[22][3], L[22][4], L[22][2], 0.7 * meter_alpha)

    def draw_letters(self, uber_count: int, hud_alpha: float = 1.0):
        self.letters_drawn = 1
        ox = L[14][0] + L[23][0] + L[22][0]
        oy = L[14][1] + L[23][1] + L[22][1] - self.bounce
        out = []
        for k, (rec, dark, lit) in enumerate(LETTERS):
            x, y, fl, sx, sy = L[rec]
            out.append((lit if k < uber_count else dark, ox + x, oy + y, sx, sy, fl, hud_alpha))
        return out

    # points number, tier pop (frame counter app+0x10)
    def points_scale(self, frame_ctr: int) -> Optional[Tuple[float, int]]:
        """Returns (number scale incl. pop, tier) or None when hidden (S[2] < 1)."""
        if self.pts_pending < 1:
            self.tier = -1
            return None
        base = L[30][3]
        if self.pts_pending < TIERS[0][0]:              # never true (tier 0 min = 0)
            self.tier, self.tier_pop = 0, 0.0
        t = tier_of(self.pts_pending)
        if t == self.tier:
            e = frame_ctr - self.tier_t0
            if e <= 36.0:
                self.tier_pop = base * 0.8 * e / 18.0 if e < 18.0 else base * 0.8 * (1.0 - (e - 18.0) / 36.0)
            else:
                self.tier_pop = 0.0
        else:
            if self.tier_pop == 0.0:
                self.tier_t0 = float(frame_ctr)
            else:
                self.tier_t0 = frame_ctr - (self.tier_pop / 0.8) * 18.0
            self.tier = t
        return base + self.tier_pop, t


def plate_geometry(scale: float, text_w: float, font_h: float, cap_w: float):
    """Backing plate (sprites 0x3A middle, 0x38 left cap, 0x39 right cap) for a number at scale s.
    Returns (h, y_raise, cap_width, middle_width): the tier colour {A,R,G,B} tints the plate only."""
    h = font_h * scale + 14.0 * scale
    return h, 7.0 * scale, cap_w + h * 0.37, text_w


def flying_popup(p: float, pulse: float, typ: int, hud_alpha: float = 1.0):
    """Type 2/3 popup at progress p: (x, y, scale, alpha, (R,G,B) of the digits)."""
    k = p * p + p
    x = L[30][0] + (L[4][0] - L[30][0]) * k
    y = L[30][1] + (L[4][1] - L[30][1]) * k
    s = (1.0 - p * 0.5) * pulse * L[30][4] * L[30][3]
    rgb = (1.0, 0.5, 0.5) if typ == 3 else (1.0, 1.0, 1.0)
    return x, y, s, (1.0 - p) * hud_alpha, rgb


def uber_hint_alpha(tricky_timer: float, infinite: bool, hud_alpha: float = 1.0) -> float:
    return hud_alpha * (1.0 if infinite else min(tricky_timer / 5.0, 1.0))


def tricky_timer_step(t: float, motion: int, infinite: bool = False) -> float:
    if motion == 1 and 0.0 < t <= 0.016666668:
        t = 0.016666668                                 # held one frame while airborne
    else:
        t = max(0.0, t - 0.016666668)
    return 1.0 if infinite else t


@dataclass
class TrickyAudio:
    """Focus-player Tricky cue state on the sound manager. Times in scheduler units (ms)."""
    arcade_sfx: bool = True        # snd+0x3844 (Options > Sound > Arcade SFX)
    tricky_rider: bool = False     # snd+0x6E80 set
    saved_ubers: int = 0           # snd+0x6E94
    flag_end: int = 0              # snd+0x6E88 -> tricky.bnk #2 when the timer reaches 0
    flag_warn: int = 0             # snd+0x6E8C -> tricky.bnk #1 when the timer < 2.7
    flag_uber: int = 0             # snd+0x6E90 -> tricky.bnk #0 when the uber count rises
    phrase_on: bool = False        # snd+0x3708 (Tricky loop phrase playing)

    def frame(self, timer: float, ubers: int) -> List[str]:
        """Per-frame Tricky cue update, also run at the start of every meter-full event."""
        ev: List[str] = []
        if self.phrase_on and timer <= 0.0:
            self.phrase_on = False; ev.append("stop tricky phrase")
        if not self.tricky_rider:
            return ev
        if self.flag_uber and self.saved_ubers < ubers:
            if self.phrase_on:
                self.phrase_on = False; ev.append("stop tricky phrase")
            ev.append("tricky.bnk #0 (class 6)")
            self.flag_warn = self.flag_end = self.flag_uber = 0
            self.saved_ubers = ubers; self.tricky_rider = False
            return ev
        if self.flag_end and timer <= 0.0:
            ev.append("tricky.bnk #2 (class 6)")
            self.flag_warn = self.flag_end = 0; self.tricky_rider = False
            return ev
        if self.flag_warn and timer < 2.7:
            if self.phrase_on:
                self.phrase_on = False; ev.append("stop tricky phrase")
            ev.append("tricky.bnk #1 (class 6)")
            self.flag_warn = 0
        return ev

    def meter_full(self, timer_before: float, ubers: int, timer_now: float) -> List[str]:
        """Meter reached 1.0 with a positive reward."""
        ev = self.frame(timer_now, ubers)
        self.tricky_rider, self.saved_ubers = True, ubers
        ev.append("+500: zbxsfx #107 (class 4, vol 127)")
        if self.arcade_sfx:
            if ubers < 6:
                self.flag_warn = self.flag_end = 1
            self.flag_uber = 1
        if timer_before <= 0.0 and self.arcade_sfx:
            if not self.phrase_on:
                self.phrase_on = True; ev.append("tricky phrase at next beat (last loop phrase)")
            if ubers < 6:
                ev.append("MC 0x204C (class 19 gate; not mode 6)")
        return ev
