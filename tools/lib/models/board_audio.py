"""Board-sound model: SNOW.INF compiler + evaluator, rider inputs, surface map, voice callbacks,
big-air wind / music duck, 3D falloff and Doppler, event one-shot table.

Integer semantics follow the EE:
int32 accumulator, C truncating division, float->int truncation, sqrt of a negative = sqrt(|x|).
"""
import math
import struct

# SNOW.INF bytecode (compiler 0x21A7C8, evaluator 0x21AE90)
OP_LOAD, OP_LOADCONST, OP_MAP, OP_SQUARE, OP_SQRT, OP_LOG, OP_ADD, OP_SUB, OP_MUL, OP_DIV = range(10)
OP_NEG, OP_ABS, OP_BOUND, OP_PUSH, OP_POP, OP_SADD, OP_SSUB, OP_SMUL, OP_SDIV, OP_ASSIGNVOL, OP_ASSIGNBEND = range(10, 21)
OP_END = 0x7F
LOAD_SRC = {"SLIP": 0, "DIG": 1, "LEAN": 2, "BOARD": 3, "BEND": 4}
INT_OPS = {"LOADCONST": OP_LOADCONST, "MAP": OP_MAP, "ADD": OP_ADD, "SUB": OP_SUB, "MUL": OP_MUL, "DIV": OP_DIV}
BARE_OPS = {"SQUARE": OP_SQUARE, "SQRT": OP_SQRT, "LOG": OP_LOG, "NEG": OP_NEG, "ABS": OP_ABS, "PUSH": OP_PUSH,
            "POP": OP_POP, "SADD": OP_SADD, "SSUB": OP_SSUB, "SMUL": OP_SMUL, "SDIV": OP_SDIV,
            "ASSIGNVOL": OP_ASSIGNVOL, "ASSIGNBEND": OP_ASSIGNBEND}

# Program table order (parser 0x210C38): snd+0x745C + 0x50*surface + 0x320*mode
SURFACES = ["PACK", "POWDER", "LOOSE", "ICE", "METAL", "WOOD", "RAIL", "ROCK", "GLASS", "CHUTE"]
MODES = ["GLIDE", "AIGLIDE", "CARVE"]


def _i32(v):
    v &= (1 << 32) - 1
    return v - (1 << 32) if v >> 31 else v


def _tdiv(a, b):
    q = abs(a) // abs(b)
    return _i32(q if (a < 0) == (b < 0) else -q)


def _f2i(x):
    """cvt.w.s: truncate toward zero (EE), saturating."""
    if x != x:
        return 0
    return max(-(1 << 31), min((1 << 31) - 1, int(x)))


def _f32(x):
    return struct.unpack("<f", struct.pack("<f", x))[0]


class _Reader:
    """INF tokenizer: whitespace-delimited tokens, '[name]' section headers, case-insensitive compares."""

    def __init__(self, text):
        self.t = text
        self.p = 0

    def seek_section(self, name):
        """Scan forward to the next '[name]' header (any nesting); True if found."""
        while True:
            i = self.t.find("[", self.p)
            if i < 0:
                self.p = len(self.t)
                return False
            j = self.t.find("]", i)
            self.p = j + 1
            if self.t[i + 1:j].strip().upper() == name.upper():
                return True

    def _skip(self):
        while self.p < len(self.t) and self.t[self.p] in " \t\r\n":
            self.p += 1
        # '#' comment lines (FUN_002218C0 is not exported; comment words are non-keywords either way)
        while self.p < len(self.t) and self.t[self.p] == "#":
            e = self.t.find("\n", self.p)
            self.p = len(self.t) if e < 0 else e
            while self.p < len(self.t) and self.t[self.p] in " \t\r\n":
                self.p += 1

    def at_end(self):
        self._skip()
        return self.p >= len(self.t) or self.t[self.p] == "["

    def token(self):
        self._skip()
        s = self.p
        while self.p < len(self.t) and self.t[self.p] not in " \t\r\n":
            self.p += 1
        return self.t[s:self.p]

    def int_(self):
        tok = self.token()
        n = ""
        for ch in tok:                      # sscanf("%d"): optional sign then digits
            if ch.isdigit() or (not n and ch in "+-"):
                n += ch
            else:
                break
        return int(n) if n not in ("", "+", "-") else 0


def compile_program(text, surface, mode):
    """Bytecode for one (surface, mode): rewind, seek [surface], seek the next [mode], compile to the
    next header. Unknown tokens are skipped (so SNOW.INF's 'AddS' is a no-op)."""
    r = _Reader(text)
    r.seek_section(surface)
    r.seek_section(mode)
    code = []
    while not r.at_end():
        tok = r.token().upper()
        if tok == "LOAD":
            code.append(OP_LOAD)
            src = r.token().upper()
            if src in LOAD_SRC:
                code.append(LOAD_SRC[src])
            # else: no selector byte is emitted (compiler quirk; unused by the retail file)
        elif tok in INT_OPS:
            code += [INT_OPS[tok], r.int_()]
        elif tok == "BOUND":
            code += [OP_BOUND, r.int_(), r.int_()]
        elif tok in BARE_OPS:
            code.append(BARE_OPS[tok])
    code.append(OP_END)
    return code


def compile_all(text):
    return {(s, m): compile_program(text, s, m) for s in SURFACES for m in MODES}


def evaluate(code, inputs, vol=0, bend=0x40):
    """Run a program; inputs = (slip, dig, lean, board, bend_in). Returns (vol, bend); a program that
    never assigns leaves the caller's defaults (0 and 0x40 in the callbacks)."""
    acc, stack, pc = 0, [], 0
    while True:
        op = code[pc]; pc += 1
        if op == OP_END:
            return vol, bend
        if op == OP_LOAD:
            sel = code[pc]; pc += 1
            if 0 <= sel <= 4:
                acc = inputs[sel]
        elif op == OP_LOADCONST:
            acc = _i32(code[pc]); pc += 1
        elif op == OP_MAP:
            n = code[pc]; pc += 1
            acc = max(0, min(127, _tdiv(_i32(acc * 127), n)))
        elif op == OP_SQUARE:
            acc = _i32(acc * acc)
        elif op == OP_SQRT:
            acc = _f2i(_f32(math.sqrt(abs(float(acc)))))   # EE sqrt.s of a negative = sqrt(|x|)
        elif op == OP_ADD:
            acc = _i32(acc + code[pc]); pc += 1
        elif op == OP_SUB:
            acc = _i32(acc - code[pc]); pc += 1
        elif op == OP_MUL:
            acc = _i32(acc * code[pc]); pc += 1
        elif op == OP_DIV:
            acc = _tdiv(acc, code[pc]); pc += 1
        elif op == OP_NEG:
            acc = _i32(-acc)
        elif op == OP_ABS:
            acc = abs(acc)
        elif op == OP_BOUND:
            lo, hi = code[pc], code[pc + 1]; pc += 2
            acc = max(lo, min(hi, acc))
        elif op == OP_PUSH:
            stack.append(acc)
        elif op == OP_POP:
            acc = stack.pop()
        elif op in (OP_SADD, OP_SSUB, OP_SMUL, OP_SDIV):
            top, second = stack.pop(), stack.pop()
            stack.append({OP_SADD: lambda: _i32(top + second), OP_SSUB: lambda: _i32(top - second),
                          OP_SMUL: lambda: _i32(top * second), OP_SDIV: lambda: _tdiv(top, second)}[op]())
        elif op == OP_ASSIGNVOL:
            vol = acc
        elif op == OP_ASSIGNBEND:
            bend = acc
        # OP_LOG (5) is accepted by the compiler but does nothing at run time


# rider inputs (0x21A6A8..0x21A788)
def inputs(contact_forward4, velocity4, ground_drag, steer_cur):
    """Slip = |trunc(dot4(contact_forward, velocity))| (cm/s along the board, w included),
    Dig = |trunc(ground drag Fr)| (+0x160), Lean = |trunc(steer_cur * 127)|,
    Board = (Slip >> 2) + 2*Dig, Bend = Board + Slip."""
    d = _f32(sum(_f32(a * b) for a, b in zip(contact_forward4, velocity4)))
    slip = abs(_f2i(d))
    dig = abs(_f2i(ground_drag))
    lean = abs(_f2i(_f32(steer_cur * 127.0)))
    board = (slip >> 2) + dig * 2
    return slip, dig, lean, board, board + slip


# surface id -> SNOW index (0x20F908)
_SURF = {0: 1, 3: 1, 4: 1, 2: 2, 5: 3, 6: 3, 9: 7, 10: 7, 11: 7, 12: 5, 13: 4, 14: 8, 18: 9, 19: 9}


def snow_index(surface_id, motion=2, rail_surface=0):
    """On a rail (motion 3) the global snd+0x6E40 (surface of the last rail anyone caught) is used."""
    return _SURF.get(rail_surface if motion == 3 else surface_id, 0)


# piecewise-linear curves (0x20D928, table 0x343098: 4 x then 4 y, int16)
CURVES = [
    ((100, 300, 800, 1200), (22, 60, 100, 127)),    # 0 land volume, player (impact speed)
    ((100, 300, 800, 2000), (11, 30, 60, 120)),     # 1 land volume, AI
    ((200, 400, 550, 800), (33, 70, 100, 127)),     # 2 prop impact volume (|v|)
    ((100, 1000, 2000, 4000), (20, 70, 100, 127)),  # 3 (no caller found)
    ((500, 1000, 1500, 2000), (0, 10, 20, 40)),     # 4 (no caller found)
    ((10, 60, 200, 300), (0, 127, 100, 0)),         # 5 speed voice volume (Slip)
    ((10, 30, 500, 900), (0, 30, 80, 127)),         # 6 speed voice bend (Slip)
    ((100, 800, 1200, 1600), (80, 90, 105, 127)),   # 7 wipe-out crash / slide volume (impact speed)
]


def curve(i, v):
    xs, ys = CURVES[i]
    for k in range(3):
        if xs[k] <= v < xs[k + 1]:
            return ys[k] + _tdiv((v - xs[k]) * (ys[k + 1] - ys[k]), xs[k + 1] - xs[k])
    if v < xs[0]:
        return ys[0]
    if v <= xs[3]:
        return 0
    return ys[3]


# voice callbacks (table 0x39EB38)
AIR, GROUND, RAIL = 1, 2, 3


def cb_speed(motion, slip):           # 0x21B440, player zboard #0
    return (curve(5, slip), curve(6, slip)) if motion == GROUND else (0, 0x40)


def cb_glide(progs, motion, idx, inp):    # 0x21B508, player zboard idx*8+4
    return evaluate(progs[(SURFACES[idx], "GLIDE")], inp) if motion != AIR else (0, 0x40)


def cb_aiglide(progs, motion, idx, inp):  # 0x21B628, AI zboard idx*8+6
    return evaluate(progs[(SURFACES[idx], "AIGLIDE")], inp) if motion in (GROUND, RAIL) else (0, 0x40)


def cb_carve(progs, motion, idx, inp):    # 0x21B718, player zboard idx*8+3
    return evaluate(progs[(SURFACES[idx], "CARVE")], inp) if motion != AIR else (0, 0x40)


def board_voices(is_player, idx):
    """(bank, sound index, callback, cull/falloff radius in m) started per rider at race start."""
    if is_player:
        return [(1, 0, "speed", 40.0), (1, idx * 8 + 4, "glide", 40.0), (1, idx * 8 + 3, "carve", 40.0)]
    return [(1, idx * 8 + 6, "aiglide", 50.0)]


# big-air wind and music duck (0x21B828, player only)
def big_air(t_land, t_elapsed, t_apex, mode7=False):
    """Returns None (inactive: music byte 127, wind stopped) or (music_byte, wind_level_factor).
    t_land = +0x57F4 (only while motion is Air), t_elapsed = +0x57FC, t_apex = +0x5800.
    The wind sound (zbxsfx #32, 2D) plays at wind_level_factor * SFX level."""
    if t_land <= 1.5:
        return None
    f = 50.0 if mode7 else 16.0
    x = min(127.0, max(f, (1.0 - (1.0 - f / 127.0) * t_land * 0.5) * 127.0))
    total = t_apex * 0.5 + t_elapsed
    if total <= t_elapsed:
        duck = _f2i(x)
    else:
        r = t_elapsed / total
        z = 127.0 - (127.0 - x) * r
        duck = _f2i(min(127.0, max(x, z)))
    return duck, (127 - duck) / 127.0


# 3D (0x20E730 listener distance, 0x20AAB8 falloff, 0x20AB48 Doppler, 0x20B688 cull)
def listener_offset(rel_cm):
    """rel_cm = listener-camera-space position (cm). Returns metres after the fixed ear offset."""
    x, y, z = rel_cm
    return x * 0.01, (y + 25.0) * 0.01, (z - 100.0) * 0.01


def falloff(vol, dist_m, radius_m):
    """vol * ((R - d')/R)^2 with d' = max(dist - 0.5, 0); 0 outside R. R = 30 when the voice has none."""
    r = radius_m or 30.0
    d = max(dist_m - 0.5, 0.0)
    if not (d < r and r > 0.0):
        return 0
    return _f2i(((r - d) / r) ** 2 * vol)


def doppler(prev_dist_m, dist_m, dt):
    """Pitch factor (4096 = 1.0 in the voice API): (clamp((prev - dist)/dt, +-894.08) + 8000) / 8000."""
    v = max(-894.07996, min(894.07996, (prev_dist_m - dist_m) / dt))
    return (v + 8000.0) / 8000.0


EVENTS = {
    "land": "bank 1, idx*8+1 (player, vol curve 0) / idx*8+5 (AI within 40 m, vol curve 1) of impact speed",
    "wall_hit": "bank 0 #49, 3D, 40 m, blocked while the rider's previous one plays",
    "rider_bump": "bank 0 #90/#91 random, 30-frame global cooldown, only if a human is involved",
    "wipeout_crash": "bank 0 #48..#50 random (#64 if pre-wipeout surface 3/4), vol curve 7",
    "wipeout_slide": "bank 0 #51 once per wipe-out, vol curve 7, muted while handler+8 > 0.1",
    "boost_start": "bank 0 #120/#121/#122 by meter > 0.666 / > 0.3336 / else, player, held voice",
    "uber_start": "bank 0 #119, player",
    "reset": "bank 0 #123",
    "lead_taken": "bank 0 #109 when the rider's new rank is 0",
    "trick_book_first": "bank 0 #95 x3 at 250/500/750 ms",
    "boost_special": "bank 0 #108 (0x11D038 branch, TODO condition)",
}
