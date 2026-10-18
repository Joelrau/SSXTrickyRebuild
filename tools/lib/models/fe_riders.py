"""Front-end rider reference model.

Game space: Z up, cm, degrees where noted, 60 Hz FE frames.

What is modelled:
  * clip choice and the per-rider animation state machine;
  * the TRANS variant toggle shared with the FE speech bank;
  * rider placement on the character-select ring;
  * the selection light blend and the two light sets (section 5), the camera-space light directions and the
    per-vertex colour of the rider VU program, the carousel window FUN_00261B38.

Self-test (`python models/fe_riders.py`): places all 12 riders and checks that each one stands on
its mdl_Cplatform_* instance in ssxfe.pbd (read through the rebuild's tools/pbd.py), that the
speech event of every TRANS clip lives in the feselect bank of its variant (converted audio
manifest), and that the light blend reaches its end points in 15 frames.
"""
from __future__ import annotations

import json
import math
import os
import sys
from dataclasses import dataclass, field

# Internal character index -> two-letter clip prefix (feanim.afl, "fe<XX>FE...").
CLIP_PREFIX = ["ED", "KA", "LU", "MA", "MO", "ZO", "JP", "EL", "PS", "SE", "BR", "MR"]
CHAR_NAME = ["Eddie", "Kaori", "Luther", "Mac", "Moby", "Zoe", "JP", "Elise", "Psymon", "Seeiah",
             "Brodi", "Marisol"]
# Carousel slot -> internal character index.
CAROUSEL = [3, 1, 8, 10, 0, 7, 11, 9, 2, 6, 5, 4]

# Animation ids used by the FE rider (animation property table rows).
ANIM_INIT, ANIM_TRANS1, ANIM_TRANS2, ANIM_TRANS3, ANIM_TRANS4, ANIM_CYCLE = range(0x2F9, 0x2FF)
# id -> (clip suffix, loop mode, cross-fade in anim frames); loop 3 = cycle, 2 = hold last frame.
ANIM_PROPS = {
    ANIM_INIT: ("FEINIT", 3, 0),
    ANIM_TRANS1: ("FE_TRANS1", 2, 7),
    ANIM_TRANS2: ("FE_TRANS2", 2, 7),
    ANIM_TRANS3: ("FE_TRANS3", 2, 7),
    ANIM_TRANS4: ("FE_TRANS4", 2, 7),
    ANIM_CYCLE: ("FE_CYCLE", 3, 7),
}


def clip_name(anim_id: int, character: int) -> str:
    return "fe" + CLIP_PREFIX[character] + ANIM_PROPS[anim_id][0]


STATE_IDLE, STATE_PICKED, STATE_PICKED_LOOP, STATE_NONE = 0, 1, 2, 3


@dataclass
class FeSession:
    """Globals shared by all riders."""
    variant_pair: int = 0      # toggled on every front-end load, first load -> 1
    variant_flip: int = 0      # toggled on every TRANS start

    def on_fe_load(self) -> int:
        """Called once per front-end load; returns the speech bank variant (feselect<k>.bnk)."""
        self.variant_pair ^= 1
        return self.variant_pair

    def next_trans(self) -> int:
        """TRANS clip index 0..3 = variant_flip + 2 * variant_pair, then variant_flip toggles."""
        k = self.variant_flip + 2 * self.variant_pair
        self.variant_flip ^= 1
        return k


@dataclass
class FeRider:
    character: int
    slot: int
    current: int = STATE_NONE      # state whose clip was last started
    requested: int = STATE_NONE
    playing: int = -1              # animation id on channel 0
    loaded: bool = False
    log: list = field(default_factory=list)

    def request(self, state: int):
        self.requested = state

    def update(self, session: FeSession, clip_done: bool):
        """One FE frame. clip_done = the channel-0 clip reached its last frame."""
        if not self.loaded:
            return
        if clip_done and self.current == STATE_PICKED:
            self.requested = STATE_PICKED_LOOP
        if self.requested != self.current:
            if self.requested == STATE_PICKED:
                self._play(ANIM_TRANS1 + session.next_trans())
            elif self.requested == STATE_IDLE:
                self._play(ANIM_INIT)
            elif self.requested == STATE_PICKED_LOOP:
                self._play(ANIM_CYCLE)
            self.current = self.requested
        # the controller then advances every instance at speed 1 (0.5 anim frames per FE frame)

    def _play(self, anim_id: int):
        self.playing = anim_id
        self.log.append(clip_name(anim_id, self.character))


def game_frames_until_done(n_frames: int) -> int:
    """FE frames from start until a hold clip reports done (frame >= n - 1 at 0.5 per frame)."""
    return math.ceil((n_frames - 1) / 0.5)


def rider_matrix(slot: int, ring_radius: float, ring_z: float):
    """Row-vector 4x4 (v' = v M) of the rider root before the parent groups (identity)."""
    theta = math.radians(-30.0 * slot - 90.0)
    s, c = math.sin(theta), math.cos(theta)
    r0 = (c, -s, 0.0)                          # rider local +x -> radial, outward
    r1 = (s, c, 0.0)
    r2 = (0.0, 0.0, 1.0)
    pos = (ring_radius * r0[0], ring_radius * r0[1], ring_z)
    return [list(r0) + [0.0], list(r1) + [0.0], list(r2) + [0.0], list(pos) + [1.0]]


def board_matrix_held(rider_m):
    """With the 'held' flag set (always) the board uses the rider root matrix."""
    return rider_m


LIGHTS_UNSELECTED = {
    "ambient": (1.0, 0.25, 0.25, 0.25),
    "azimuth": (60.0, 120.0, 0.0), "elevation": (0.0, 0.0, 0.0),
    "colour": ((0.0, 1.25, 1.25, 1.25), (0.0, 1.25, 1.25, 1.25), (0.0, 0.0, 0.0, 0.0)),
}
LIGHTS_SELECTED = {
    "ambient": (1.0, 0.1, 0.1, 0.1),
    "azimuth": (75.0, 345.0, 285.0), "elevation": (58.0, 20.0, -20.0),
    "colour": ((0.0, 10.0, 10.0, 10.0), (0.0, 2.1, 1.9, 1.7), (0.0, 1.0, 1.0, 2.0)),
}
BLEND_STEP = 1.0 / 15.0


def step_blend(blend: float, selected: bool):
    """Returns (new blend, changed). Changes stop at the end points."""
    if selected:
        if blend >= 1.0:
            return blend, False
        blend += BLEND_STEP
    else:
        if blend <= 0.0:
            return blend, False
        blend -= BLEND_STEP
    return min(1.0, max(0.0, blend)), True


def _lerp(a, b, t):
    if isinstance(a, tuple):
        return tuple(_lerp(x, y, t) for x, y in zip(a, b))
    return a * (1.0 - t) + b * t


def light_set(blend: float) -> dict:
    out = {k: _lerp(LIGHTS_UNSELECTED[k], LIGHTS_SELECTED[k], blend) for k in LIGHTS_UNSELECTED}
    dirs = []
    for az, el in zip(out["azimuth"], out["elevation"]):
        a, e = math.radians(az), math.radians(el)
        v = (math.cos(a) * math.cos(e), math.sin(e), math.sin(a) * math.cos(e))
        n = math.sqrt(sum(x * x for x in v))
        dirs.append(tuple(x / n for x in v))
    out["direction"] = tuple(dirs)
    return out


# Light directions are in CAMERA space (cFE3DCharacter::vfunc_11 0x2663F8 multiplies by the transpose of the view
# matrix the FE camera node loaded, 0x26AC98): d_world = V_rot * d_cam, V_rot = Ry(rotY) Rz(rotZ) Rx(rotX - 90) B
# (B swaps y / z; right-handed rotations, camera channels in degrees). Camera space: x right, y up, z into the screen.
# FUN_001CE140 packs dir rows (d0.k, d1.k, d2.k, 0) and colour columns (r, g, b, lead) x 128 for lights 0..2 and the
# ambient. Rider VU image 0x318080: I_i = max(0, n_world . d_i), c = min(amb + sum I_i colour_i, 255), FTOI0.
def _rot(axis, deg):
    a = math.radians(deg)
    c, s = math.cos(a), math.sin(a)
    if axis == "x":
        return ((1, 0, 0), (0, c, -s), (0, s, c))
    if axis == "y":
        return ((c, 0, s), (0, 1, 0), (-s, 0, c))
    return ((c, -s, 0), (s, c, 0), (0, 0, 1))


def _mm(a, b):
    return tuple(tuple(sum(a[i][k] * b[k][j] for k in range(3)) for j in range(3)) for i in range(3))


def view_rotation(rot_deg):
    """V_rot (camera -> world, column form) of the FE camera with channels (rotX, rotY, rotZ) in degrees."""
    swap = ((1, 0, 0), (0, 0, 1), (0, 1, 0))
    m = _mm(_rot("x", rot_deg[0] - 90.0), swap)
    m = _mm(_rot("z", rot_deg[2]), m)
    return _mm(_rot("y", rot_deg[1]), m)


def camera_to_world(d, rot_deg):
    v = view_rotation(rot_deg)
    return tuple(sum(v[i][k] * d[k] for k in range(3)) for i in range(3))


def vertex_colour(ls: dict, n_world, rot_deg, truncate=False):
    """Vertex colour (r, g, b) in PS2 units (1.0 = 0x80) for a unit world-space normal."""
    out = []
    dirs = [camera_to_world(d, rot_deg) for d in ls["direction"]]
    for ch in range(1, 4):
        v = ls["ambient"][ch] * 128.0
        for d, c in zip(dirs, ls["colour"]):
            v += max(0.0, sum(n_world[k] * d[k] for k in range(3))) * c[ch] * 128.0
        v = min(v, 255.0)
        out.append((math.floor(v) if truncate else v) / 128.0)
    return tuple(out)


# Carousel window FUN_00261B38(fe, pad, _, p): riders p-2..p+2 drawn (vslot 0x2C -> +0x14; the ctor sets +0x14 = 1,
# so before the first call all 12 are drawn), selected flag (+0x154) on p only, TexFlip message 2 (FUN_00269BE8
# channel 2): rider signs mdl_cs<Name>_0 (table 0x3470C8): the pad's previous slot (fe+0x7D+2 pad) -> 0, p -> 1;
# platforms mdl_Cplatform_* (table 0x347100): all -> 0, p -> 1; then fe+0x7D+2 pad = p. Flipbook frame 1 = lit texture.
# Callers set p = 4 BEFORE the swing into the char room (request-6 hooks via FUN_00263270); CharacterSelect
# vfunc_15 0x278598 / vfunc_23 0x2791F0, CharacterModify vfunc_15 0x279A80.
def carousel_window(state: dict, pad: int, p: int) -> dict:
    """state = {"slot": [s0, s1], "sign": [12 frames], "platform": [12], "drawn": [12], "selected": [12]}."""
    st = {k: list(v) for k, v in state.items()}
    st["drawn"] = [min((k - p) % 12, (p - k) % 12) <= 2 for k in range(12)]
    st["selected"] = [k == p for k in range(12)]
    st["sign"][st["slot"][pad]] = 0
    st["sign"][p] = 1
    st["platform"] = [1 if k == p else 0 for k in range(12)]
    st["slot"][pad] = p
    return st


REBUILD = os.path.normpath(os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "..", ".."))
FEANIM_SPEECH_INDEX = ["BR", "ED", "EL", "LU", "MA", "MR", "MO", "PS", "SE", "ZO", "KA", "JP"]


def _platforms():
    sys.path.insert(0, os.path.join(REBUILD, "tools"))
    import pbd  # rebuild tool, read only
    base = os.path.join(REBUILD, "unpacked", "MODELS", "SSXFE", "data", "models", "ssxfe")
    p = pbd.load(base + ".pbd")
    names, sec = {}, False
    for line in open(base + ".map", encoding="latin-1"):
        if "INTERNAL INSTANCES BEGIN" in line:
            sec = True
            continue
        if "INTERNAL INSTANCES END" in line:
            break
        if sec and not line.startswith("#") and len(line.split()) >= 2:
            names[line.split()[0]] = int(line.split()[1])
    return {n: p.instances[u].matrix[12:15] for n, u in names.items()}


def selftest() -> int:
    bad = 0
    inst = _platforms()
    p01 = inst["mdl_Cplatform_01"]
    radius, z = math.hypot(p01[0], p01[1]), p01[2]
    names = ["01", "02", "03", "04", "05", "06", "07", "08", "09", "010", "011", "012"]
    for slot in range(12):
        m = rider_matrix(slot, radius, z)
        px, py = m[3][0], m[3][1]
        tx, ty, _ = inst["mdl_Cplatform_" + names[slot]]
        err = math.hypot(px - tx, py - ty)
        ok = err < 3.0   # platform 02 is authored 2.4 cm off the ring
        bad += not ok
        print(f"slot {slot:2d} {CHAR_NAME[CAROUSEL[slot]]:8s} rider ({px:8.2f},{py:8.2f}) "
              f"platform ({tx:8.2f},{ty:8.2f}) err {err:5.2f} {'ok' if ok else 'FAIL'}")
    # speech events vs feselect banks
    man = json.load(open(os.path.join(REBUILD, "converted", "audio", "manifest.json")))
    have = {0: set(), 1: set()}
    for e in man["entries"]:
        for k in (0, 1):
            if e["source"].startswith(f"SPEECH.BIG:feselect{k}.bnk"):
                have[k].add(e["slot"])
    sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "scripts"))
    import afl
    bank = afl.Bank()
    for ch in range(12):
        for t in range(4):
            recs = bank.resolve(ANIM_TRANS1 + t, 0, ch)
            ev = recs[0].events[0][1]
            variant = t // 2
            expect = 8 * FEANIM_SPEECH_INDEX.index(CLIP_PREFIX[ch]) + t
            ok = ev == expect and ev in have[variant]
            bad += not ok
            if not ok:
                print("speech event mismatch", CLIP_PREFIX[ch], t, ev, expect)
    print("speech events: 48 checked")
    # light blend
    b, n = 0.0, 0
    while True:
        b, changed = step_blend(b, True)
        if not changed:
            break
        n += 1
    ok = abs(b - 1.0) < 1e-9 and n in (15, 16)   # 15 steps of 1/15; float rounding may add one clamp step
    bad += not ok
    print(f"light blend 0->1 in {n} frames: {'ok' if ok else 'FAIL'}")
    s = FeSession()
    s.on_fe_load()
    r = FeRider(character=3, slot=0, loaded=True)
    r.request(STATE_IDLE); r.update(s, False)
    r.request(STATE_PICKED); r.update(s, False); r.update(s, True)
    print("Mac log:", r.log)
    print("fe_riders: OK" if not bad else f"fe_riders: {bad} failure(s)")
    return bad


if __name__ == "__main__":
    sys.exit(1 if selftest() else 0)
