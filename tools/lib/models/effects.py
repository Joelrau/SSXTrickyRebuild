"""Track effect / event system reference model.

Units: cm, s; Z up; one call
of `World.frame()` is one game frame (1/60 s).

What it covers:
  * the `.ssf` effect side: header, 28-byte event records (seven effect slots per instance
    record), the effect table (lists of {type, size, payload} records), the named functions, the
    instance records and the spline table (`parse_ssf`, `decode`);
  * the script interpreter (cScriptNode): record execution, waits, conditions, abort, nesting
    (`World.run_effect`, `World.call_function`, `World.frame`);
  * who starts scripts: cell activation / deactivation (slots 0 / 2), rider touch (slot 1, with
    the controller forwarding rules), node completion (slots 3 / 4), the game-flow scripts by
    name (mode script, countdown scripts) (`CellTracker`, `World.touch`, `flow_scripts`);
  * the outcome of every record type, as `Outcome` tuples (rider effects are applied to the
    `Rider` objects; world effects are logged for the port's renderer / audio / particle code).

Path-event triggers (finish, lap, checkpoints, music events 0/9/10, breakdown zones, announcer
cues) are not `.ssf` effects; they live in `models/race.py` and `models/race_audio.py`
`PATH_EVENT_OUTCOMES` below only indexes them.
"""
from __future__ import annotations

import re
import struct
from dataclasses import dataclass, field
from pathlib import Path
from typing import Callable, Dict, List, Optional, Tuple

import numpy as np

F32 = np.float32
FRAME_DT = F32(1.0 / 60.0)          # script wait decrement per frame (float32)

MODE_KNOCKDOWN, MODE_FREERIDE, MODE_PRACTICE_RACE, MODE_PRACTICE_SHOWOFF = 0, 1, 2, 3
MODE_CIRCUIT_RACE, MODE_CIRCUIT_SHOWOFF, MODE_LESSON, MODE_TIMED_RACE, MODE_DEMO, MODE_TEST = 4, 5, 6, 7, 8, 9
SHOWOFF_MODES = (MODE_PRACTICE_SHOWOFF, MODE_CIRCUIT_SHOWOFF)
TRACK_UNTRACKED = 7
TRACK_MERQURY = 4

# mode -> name of the mode script run by the race flow (cAI slot 13)
MODE_SCRIPT = {0: "RaceMode", 1: "FreerideMode", 2: "RaceMode", 3: "ShowoffMode", 4: "RaceMode",
               5: "ShowoffMode", 6: "FreerideMode", 7: "RaceMode", 8: "FreerideMode", 9: "RaceMode"}


def mode_script(game_mode: int) -> str:
    return MODE_SCRIPT.get(game_mode, "FreerideMode")


def has_countdown(game_mode: int, track: int) -> bool:
    """The race-flow countdown test: never on Untracked; modes 0, 2, 3, 4, 5, 7, 9 count down."""
    if track == TRACK_UNTRACKED:
        return False
    return game_mode in (0, 2, 3, 4, 5, 7, 9)


def flow_scripts(phase: str, game_mode: int, track: int) -> List[str]:
    """Named scripts started by the race flow in a phase, in order.
    phase: 'prerace' (PreRace handler enter), 'startrace' (StartRace handler), 'countdown_enter',
    'countdown_end' (the countdown clock reached 0)."""
    cd = has_countdown(game_mode, track)
    m = mode_script(game_mode)
    if phase in ("prerace", "startrace"):
        return (["NoCountdown"] if not cd else []) + ([m] if (phase == "prerace" or not cd) else [])
    if phase == "countdown_enter":
        return ["StartCountdown", m] if cd else []
    if phase == "countdown_end":
        return ["EndCountdown", m] if cd else []
    raise ValueError(phase)


OP_NAMES = {0: "node", 1: "camera", 2: "emitter", 3: "message_checked", 4: "wait", 5: "condition",
            6: "showoff_points", 7: "run_on_instance", 8: "sound", 9: "message", 10: "enable_controller",
            11: "deactivate_controller", 12: "nop12", 13: "reset_rider", 14: "multiplier", 15: "add_boost",
            16: "add_time", 17: "boost_burst", 18: "launch_boost", 19: "nop19", 20: "nop20",
            21: "call_function", 22: "nop22", 23: "camera_shake", 24: "teleport_rider", 25: "set_rail",
            26: "call_function_no_rider"}
# fixed record sizes (header included) per type; 0 / 2 depend on the subtype
OP_SIZE = {1: None, 3: 16, 4: 12, 5: 20, 6: 12, 7: 16, 8: 12, 9: 16, 10: 12, 11: 12, 13: 12, 14: 12,
           15: 12, 16: 12, 17: 12, 18: 12, 21: 12, 23: 16, 24: 12, 25: 16, 26: 12}

NODE_CLASSES = {0: "Roller", 2: "Debounce", 5: "State", 6: "Counter", 7: "Boost", 8: "Timer", 9: "Rail",
                10: "UVScroll", 11: "TexFlip", 12: "Fence", 13: "Flag", 14: "Cracked", 15: "LapBoost",
                16: "RandomBoost", 17: "CrowdBox", 18: "ZBoost", 19: "UVScrollTexFlip", 20: "MeshAnim",
                21: "TrickTrigger", 22: "Particle", 23: "Movie", 24: "TubeEndBoost", 256: "AnimObject",
                257: "AnimDelta", 258: "AnimCombo", 259: "AnimTexFlip"}
NODE_SIZE = {0: 36, 2: 16, 5: 16, 6: 20, 7: 40, 8: 20, 9: 24, 10: 36, 11: 32, 12: 20, 13: 28, 14: 20,
             15: 32, 16: 52, 17: 24, 18: 40, 19: None, 20: 52, 21: 24, 22: None, 23: 12, 24: 88,
             256: 44, 257: 44, 258: 60, 259: None}
EMITTER_KINDS = {0: "Emitter", 1: "SplinePath", 2: "CollideEmitter"}
EMITTER_SIZE = {0: 216, 1: 56, 2: 216}

# controller type ids that are not factory subtypes
TYPE_DEAD = 5            # cDeadNode (State 2 / 3)
TYPE_RESTORE = 0x3EE     # cRestoreNode (State 4)
# controllers whose touch (slot 18) runs the instance's slot-1 effect, with a cooldown in frames
TOUCH_FORWARD_COOLDOWN = {10: 30, 11: 30, 19: 30}       # UVScroll, TexFlip, UVScrollTexFlip
ANIM_TYPES = (256, 257, 258, 259)                       # cooldown = node record +0x20 seconds x 60
INITIAL_TOUCH_COOLDOWN = {10: 30, 11: 30, 14: 30}       # UVScroll / TexFlip / Cracked start at 30

# event slots
SLOT_ACTIVATE, SLOT_TOUCH, SLOT_DEACTIVATE, SLOT_DISABLED, SLOT_EXPIRED = 0, 1, 2, 3, 4

# instance active-flag bits
FL_DRAWN, FL_DISPLAY, FL_NODE_DRAWN, FL_COLLIDABLE, FL_REACTS = 0x1, 0x2, 0x4, 0x20, 0x80
FL_CELL_ACTIVE, FL_HELD = 0x100, 0x800

CRACK_DAMAGE = 0.036               # Cracked health loss per |v_hat . n| a
RESET_DROP = 101.46                 # cm, op 13
KMH_TO_CMS = 27.777777              # op 5 kind 0 sub 1
TELEPORT_SPACING = 300.0            # cm, op 24 start-slot grid
TELEPORT_SLOTS = {0: (-1.0, 0.0), 1: (-1.0, 1.0), 2: (0.0, 1.0), 3: (1.0, 1.0), 4: (1.0, 0.0)}
MULTIPLIER_MOTIONS = (1, 3)         # op 14 needs motion Air (1) or RailSlide (3)
SHOWOFF_PICKUP_REWARD = 0.0397949   # meter reward of op 6

# active-cell set
CELL_SLOTS = 108
CELL_RADIUS = 2                     # cells i-2..i around trunc((x - min + cell) / cell)


@dataclass
class Record:
    type: int
    size: int
    payload: bytes
    offset: int                     # file offset of the record header

    def i(self, k):
        return struct.unpack_from("<i", self.payload, 4 * k)[0]

    def f(self, k):
        return struct.unpack_from("<f", self.payload, 4 * k)[0]

    @property
    def subtype(self):
        return self.i(0) if self.type in (0, 2) else None


@dataclass
class Ssf:
    path: Path
    header: Dict[str, int]
    events: List[Tuple[int, ...]]                 # 28-byte records: 7 effect indices (-1 = none)
    effects: List[List[Record]]
    functions: List[Tuple[str, List[Record]]]
    inst_records: List[tuple]                     # (mass, restitution, flags, surface, ctype, cidx, event, x16)
    inst_index: List[int]                         # instance -> record index
    splines: List[Tuple[int, int]]                # (flags, surface)
    data_base: int = 0
    data_end: int = 0

    def event_of(self, inst: int) -> int:
        return self.inst_records[self.inst_index[inst]][6]

    def effect_for(self, inst: int, slot: int) -> int:
        e = self.event_of(inst)
        return -1 if e < 0 else self.events[e][slot]

    def function_index(self, name: str) -> int:
        """Case-insensitive name lookup (the game uses stricmp); -1 if absent."""
        low = name.lower()
        for k, (n, _r) in enumerate(self.functions):
            if n.lower() == low:
                return k
        return -1

    def initial_flags(self, inst: int) -> int:
        return self.inst_records[self.inst_index[inst]][2]


def _records(d, base, n, off):
    out, q = [], base + off
    for _ in range(n):
        t, s = struct.unpack_from("<2I", d, q)
        out.append(Record(t, s, d[q + 8:q + s], q))
        q += s
    return out, q


def parse_ssf(path) -> Ssf:
    d = Path(path).read_bytes()
    w = struct.unpack_from("<19I", d, 0)
    names = ("version n_event p_event n_tree p_tree n_mesh p_mesh n_effect p_effect n_func p_func "
             "n_irec p_irec n_inst p_iidx n_spline p_spline").split()
    h = dict(zip(names, (w[1] >> 16,) + w[3:19]))
    h["magic"] = w[0]
    events = [struct.unpack_from("<7i", d, h["p_event"] + 28 * k) for k in range(h["n_event"])]
    base = h["p_effect"] + 8 * h["n_effect"]
    effects, end = [], base
    for k in range(h["n_effect"]):
        n, off = struct.unpack_from("<2I", d, h["p_effect"] + 8 * k)
        recs, q = _records(d, base, n, off)
        effects.append(recs)
        end = max(end, q)
    functions = []
    for k in range(h["n_func"]):
        n, off = struct.unpack_from("<2I", d, h["p_func"] + 24 * k)
        name = d[h["p_func"] + 24 * k + 8:h["p_func"] + 24 * k + 24].split(b"\0")[0].decode("latin-1")
        recs, q = _records(d, base, n, off)
        functions.append((name, recs))
        end = max(end, q)
    irec = [struct.unpack_from("<ffIi4h", d, h["p_irec"] + 24 * k) for k in range(h["n_irec"])]
    iidx = list(struct.unpack_from(f"<{h['n_inst']}i", d, h["p_iidx"])) if h["n_inst"] else []
    spl = [struct.unpack_from("<2I", d, h["p_spline"] + 8 * k) for k in range(h["n_spline"])]
    return Ssf(Path(path), h, events, effects, functions, irec, iidx, spl, base, end)


def decode(r: Record) -> dict:
    """Named fields of one record."""
    t = r.type
    out = dict(op=OP_NAMES.get(t, f"op{t}"))
    if t == 0:
        s = r.i(0)
        out["node"] = NODE_CLASSES.get(s, s)
        if s == 5:
            out["state"] = r.i(1)
        elif s == 2:
            out["seconds"] = r.f(1)
        elif s == 6:
            out.update(count=r.i(1) & 0xFFFF, seconds=r.f(2))
        elif s == 8:
            out.update(delay=r.f(1), seconds=r.f(2))
        elif s == 14:
            out.update(seconds=r.f(1), health=r.f(2))
        elif s in (256, 257, 258):
            out["touch_cooldown_s"] = r.f(6)
    elif t == 2:
        out["kind"] = EMITTER_KINDS.get(r.i(0), r.i(0))
        if r.i(0) in (0, 2):
            # config word k = payload word k + 1
            out.update(count=r.i(1), copies=r.i(2), emit_time=r.f(3), time_scale=r.f(4),
                       size=r.f(5), life=r.f(6), size_range=r.f(7), life_range=r.f(8),
                       texture=r.i(50), blend_index=r.i(51))
    elif t == 4:
        out["seconds"] = r.f(0)
    elif t == 5:
        out.update(kind=r.i(0), sub=r.i(1), value=r.f(2))
    elif t in (3, 9):
        out.update(message=r.i(0), value=r.f(1))
    elif t == 7:
        out.update(instance=r.i(0), effect=r.i(1))
    elif t == 8:
        out["sound"] = r.i(0)
    elif t in (10, 11):
        out["arg"] = r.i(0)
    elif t in (6,):
        out["points"] = r.i(0)
    elif t in (14, 15, 16, 17, 18):
        out["value"] = r.f(0)
    elif t in (21, 26):
        out["function"] = r.i(0)
    elif t == 23:
        out.update(a=r.f(0), b=r.i(1))
    elif t == 24:
        out["instance"] = r.i(0)
    elif t == 25:
        out.update(spline=r.i(0), enable=r.i(1))
    return out


def expected_size(r: Record) -> Optional[int]:
    if r.type == 0:
        return NODE_SIZE.get(r.i(0))
    if r.type == 2:
        return EMITTER_SIZE.get(r.i(0))
    return OP_SIZE.get(r.type)


def map_instance_names(map_path) -> Dict[int, str]:
    """Instance index -> name from the INTERNAL INSTANCES section of the track .map."""
    out, sec = {}, None
    p = Path(map_path)
    if not p.exists():
        return out
    for line in p.read_text(errors="replace").splitlines():
        if "BEGIN" in line or "END" in line:
            sec = line.strip("# ").strip()
            continue
        if sec == "INTERNAL INSTANCES BEGIN":
            parts = line.split()
            if len(parts) >= 3 and parts[1].isdigit():
                out[int(parts[1])] = parts[0]
    return out


def base_name(name: str) -> str:
    return re.sub(r"_\d+$", "", name)


Outcome = tuple


@dataclass
class Rider:
    slot: int = 0                    # player slot (rider +0x460), 0..4 used by op 24
    human: bool = False              # rider +0x41C == 1
    motion: int = 2                  # rider +0x424
    speed: float = 0.0               # |velocity| cm/s
    burst_timer: float = 0.0         # +0x134
    launch_timer: float = 0.0        # +0x138
    launch_flag: int = 0             # +0x13C
    multiplier: int = 1              # stats M_pts
    reset_requested: bool = False    # +0x304
    log: List[Outcome] = field(default_factory=list)


@dataclass
class Controller:
    type: int
    timer: int = -1                  # Debounce / Timer / Cracked countdown frames
    count: int = 0                   # Counter
    touch_cooldown: int = 0
    touch_cooldown_reset: int = 30
    roller_live: bool = False        # Roller slot 12 (+0x140; meaning TODO)
    health: float = 0.0              # Cracked
    mask: int = 0                    # Counter message bits
    boost_mode: int = 0              # Boost / TubeEnd: record s16 +0x0C (node +0x60)
    boost_frames: int = 0            # Boost / TubeEnd: trunc(record +0x10 s x 60) (+0x58)
    boost_active: int = 0            # Boost / TubeEnd: window frames left (+0x5C)


@dataclass
class Script:
    records: List[Record]
    inst: Optional[int]
    rider: Optional[Rider]
    function: Optional[int] = None   # named-function index when started by name / op 21
    index: int = 0
    delay: F32 = F32(0.0)
    alive: bool = True


class World:
    """One track's effect state.  rng() must return the game's uniform [0, 1) draw (stream A) for op 5 kind 1."""

    def __init__(self, ssf: Ssf, game_mode: int, track: int, rng: Callable[[], float] = None,
                 names: Optional[Dict[int, str]] = None):
        self.ssf, self.game_mode, self.track = ssf, game_mode, track
        self.rng = rng or (lambda: 0.0)
        self.names = names or {}
        n = len(ssf.inst_index)
        self.flags = [((f >> 16) & 0xFFFF) for f in (ssf.initial_flags(i) for i in range(n))]
        self.controllers: Dict[int, Controller] = {}
        self.rail_enabled = [(fl >> 16) & 1 for fl, _s in ssf.splines]
        self.rail_requests: Dict[int, int] = {}         # rail manager (128 slots)
        self.scripts: List[Script] = []
        self.log: List[Outcome] = []
        self.showoff = game_mode in SHOWOFF_MODES
        # node helper 0x199030 (every AnimObject / AnimDelta / AnimCombo update): cooldown - 1 while > 0, when the
        # reload value (record +0x20 s x 60) is >= 0
        self.anim_cooldown_decrements = True

    def run_effect(self, effect: int, inst: Optional[int], rider: Optional[Rider] = None):
        if effect is None or effect < 0 or effect >= len(self.ssf.effects):
            return None
        return self._start(Script(self.ssf.effects[effect], inst, rider))

    def call_function(self, name: str):
        """Start a named function as the race flow does: any script already running that
        function is stopped first; no rider."""
        k = self.ssf.function_index(name)
        if k < 0:
            return None
        self.scripts = [s for s in self.scripts if s.function != k]
        return self._start(Script(self.ssf.functions[k][1], None, None, function=k))

    def call_function_index(self, k: int, rider: Optional[Rider]):
        if k < 0 or k >= len(self.ssf.functions):
            return None
        return self._start(Script(self.ssf.functions[k][1], None, rider, function=k))

    def _start(self, s: Script):
        self._update(s)                       # a new script runs at once, in the same frame
        if s.alive:
            self.scripts.append(s)
        return s

    def frame(self):
        for s in list(self.scripts):
            if s.alive:
                self._update(s)
        self.scripts = [s for s in self.scripts if s.alive]
        for inst, c in list(self.controllers.items()):
            self._controller_frame(inst, c)

    def _update(self, s: Script):
        if s.delay > 0.0:
            s.delay = F32(s.delay - FRAME_DT)
            if s.delay >= 0.0:
                return
            s.delay = F32(0.0)
        while s.index < len(s.records):
            r = self._exec(s, s.records[s.index])
            if r == -1:
                s.alive = False
                return
            s.index += 1
            if r == 0:
                if s.index < len(s.records):
                    return
                break
        s.alive = False

    def _exec(self, s: Script, r: Record) -> int:
        t, rider, inst = r.type, s.rider, s.inst
        if t == 0:
            self._factory(s, r)
        elif t == 1:
            self._out(("camera", inst, r.offset))
        elif t == 2:
            self._emitter(s, r)
        elif t in (3, 9):
            if t == 3 and inst is None:
                return 1
            c = self.controllers.get(inst) if inst is not None else None
            if c is not None:
                self._message(inst, c, r.i(0), r.f(1))
        elif t == 4:
            s.delay = F32(r.f(0))
            return 0
        elif t == 5:
            return self._condition(s, r)
        elif t == 6:
            if self.showoff and rider is not None:
                self._rider(rider, ("showoff_points", r.i(0)))
                self._rider(rider, ("popup", 6, r.i(0), 2.5))
                self._rider(rider, ("meter_reward", SHOWOFF_PICKUP_REWARD))
        elif t == 7:
            self.run_effect(r.i(1), r.i(0), rider)
        elif t == 8:
            variant = self.track == TRACK_MERQURY and r.i(0) == 0x7A
            self._out(("sound3d", 2, r.i(0), inst, "variant" if variant else "plain"))
        elif t == 10:
            c = self.controllers.get(inst) if inst is not None else None
            if c is not None and r.i(0) in (0, 1):
                self._set_enabled(inst, c, r.i(0))
        elif t == 11:
            c = self.controllers.get(inst) if inst is not None else None
            if c is not None and r.i(0) == 0:
                self._deactivate_controller(inst, c)
        elif t == 13:
            if rider is not None:
                rider.reset_requested = True
                self._rider(rider, ("reset", RESET_DROP))
        elif t == 14:
            if rider is not None:
                m = int(r.f(0))
                if self.showoff and rider.motion in MULTIPLIER_MOTIONS and m > rider.multiplier:
                    rider.multiplier = m
                    self._rider(rider, ("popup", 0xC, m, -1.0))
                if rider.human:
                    self._rider(rider, ("multiplier_sound", r.f(0)))
        elif t == 15:
            if self.showoff and rider is not None:
                self._rider(rider, ("meter_reward", r.f(0)))
        elif t == 16:
            if rider is not None:
                if self.showoff:
                    self._rider(rider, ("add_clock", r.f(0)))
                    self._rider(rider, ("popup", 9, int(r.f(0)), 2.5))
                self._rider(rider, ("time_sound", r.f(0)))
        elif t == 17:
            if rider is not None:
                rider.burst_timer = max(rider.burst_timer, r.f(0))
                self._rider(rider, ("boost_burst", r.f(0)))
                if rider.human:
                    self._rider(rider, ("boost_sound",))
        elif t == 18:
            if rider is not None:
                rider.launch_timer = max(rider.launch_timer, r.f(0))
                rider.launch_flag = 1
                self._rider(rider, ("launch_boost", r.f(0)))
                if rider.human:
                    self._rider(rider, ("launch_sound",))
        elif t in (21, 26):
            if r.i(0) >= 0:
                self.call_function_index(r.i(0), rider if t == 21 else None)
        elif t == 23:
            if rider is not None:
                self._rider(rider, ("camera_shake", r.f(0), r.i(1)))
        elif t == 24:
            if rider is not None and 0 <= r.i(0) < len(self.flags):
                off = TELEPORT_SLOTS.get(rider.slot, (0.0, 0.0))
                self._rider(rider, ("teleport", r.i(0), off[0] * TELEPORT_SPACING, off[1] * TELEPORT_SPACING))
        elif t == 25:
            self._set_rail(r.i(0), r.i(1))
        return 1

    def _condition(self, s: Script, r: Record) -> int:
        kind, sub, value = r.i(0), r.i(1), r.f(2)
        if kind == 0:
            if s.rider is None:
                return 1
            if sub == 0:
                return -1 if s.rider.speed > value else 1
            if sub == 1:
                return -1 if s.rider.speed < value * KMH_TO_CMS else 1
            return 1
        if kind == 1:
            if sub == 0:
                return -1 if self.rng() > value else 1
            if sub == 1:
                return -1 if self.rng() < value else 1
            return 1
        if kind == 2:
            return 1 if (s.rider is not None and s.rider.human) else -1
        if kind == 3:
            return -1 if (s.inst is not None and s.inst in self.controllers) else 1
        return 1

    def _factory(self, s: Script, r: Record):
        inst, sub = s.inst, r.i(0)
        if inst is None:
            return
        c = self.controllers.get(inst)
        if c is not None:
            if c.type == TYPE_DEAD:
                return
            live = self._slot12(c)
            if c.type == sub:
                if live:
                    self._out(("node_restart", inst, NODE_CLASSES.get(sub, sub)))
                return
            self._destroy(inst)
        if sub == 5:
            self._state(inst, r.i(1))
            return
        if sub == 21 and s.rider is None:
            return
        nc = Controller(sub)
        if sub == 2:
            nc.timer = int(F32(r.f(1)) * F32(60.0))
        elif sub == 8:
            nc.timer = int(F32(r.f(1)) * F32(60.0))          # Timer A (record +0x0C)
            nc.count = int(F32(r.f(2)) * F32(60.0))          # Timer B (record +0x10)
        elif sub == 6:
            nc.count = struct.unpack_from("<h", r.payload, 4)[0]
            nc.timer = int(F32(r.f(2)) * F32(60.0))
        elif sub == 14:
            nc.timer = int(F32(r.f(1)) * F32(60.0))
            nc.health = r.f(2)
            nc.touch_cooldown = 30
        elif sub in (7, 24):
            # cBoostNode ctor 0x140498 (TubeEnd 0x141688 calls it): mode, window loaded at once for mode 1
            nc.boost_mode = struct.unpack_from("<h", r.payload, 4)[0]
            nc.boost_frames = int(F32(r.f(2)) * F32(60.0))
            nc.boost_active = nc.boost_frames if nc.boost_mode == 1 else 0
        elif sub in ANIM_TYPES:
            nc.touch_cooldown_reset = int(F32(r.f(6)) * F32(60.0))
            nc.touch_cooldown = nc.touch_cooldown_reset if s.rider is not None else 0
        nc.touch_cooldown = INITIAL_TOUCH_COOLDOWN.get(sub, nc.touch_cooldown)
        self.controllers[inst] = nc
        if sub in (10, 11, 17, 23, 14, 19):
            self.flags[inst] = (self.flags[inst] & ~FL_DISPLAY) | FL_NODE_DRAWN
        self._out(("node", inst, NODE_CLASSES.get(sub, sub), r.offset))

    def _slot12(self, c: Controller) -> bool:
        return c.type == 0 and c.roller_live

    def _state(self, inst: int, k: int):
        if k == 0:
            self._restore_flags(inst, keep_cell_bit=True)
        elif k == 1:
            c = self.controllers.get(inst)      # already destroyed by the factory: no effect
            if c is not None:
                self._set_enabled(inst, c, 1)
        elif k in (2, 3):
            fl = (self.flags[inst] & 0xFF0D) | FL_NODE_DRAWN
            if k == 3:
                fl |= FL_HELD
            self.flags[inst] = fl
            self.controllers[inst] = Controller(TYPE_DEAD)
            self._out(("hide", inst, "dead"))
        elif k == 4:
            c = self.controllers.get(inst)
            if c is not None and c.type == TYPE_RESTORE:
                return
            self.flags[inst] = (self.flags[inst] & 0xFF0D) | FL_NODE_DRAWN
            self.controllers[inst] = Controller(TYPE_RESTORE)
            self._out(("hide", inst, "until_cell_unload"))

    def _restore_flags(self, inst: int, keep_cell_bit: bool):
        init = (self.ssf.initial_flags(inst) >> 16) & 0xFFFF
        keep = self.flags[inst] & FL_CELL_ACTIVE if keep_cell_bit else 0
        self.flags[inst] = init | keep | FL_DISPLAY

    def _destroy(self, inst: int):
        c = self.controllers.pop(inst, None)
        if c is not None and c.type == TYPE_DEAD:
            self.flags[inst] = (self.flags[inst] & 0xFF0D) | FL_NODE_DRAWN

    def _message(self, inst: int, c: Controller, msg: int, value: float):
        """Controller slot 24 (ops 3 / 9).  7 / 8 clear / set the instance's 0x800 bit for every
        class; the class-specific messages that the retail data uses are modelled."""
        self._out(("message", inst, NODE_CLASSES.get(c.type, c.type), msg, value))
        if c.type in (TYPE_DEAD, TYPE_RESTORE, 17, 23, 13, 12, 20):
            return                                   # these classes ignore messages
        if c.type == 6:
            self._counter_message(c, msg, value)
            return
        if msg == 7:
            self.flags[inst] &= ~FL_HELD
        elif msg == 8:
            self.flags[inst] |= FL_HELD
        elif msg == 1 and c.type in (8, 14):
            if c.type == 8:
                c.count = int(F32(value) * F32(60.0))
            else:
                c.timer = int(F32(value) * F32(60.0))
        elif msg == 2 and c.type == 14:
            c.health = value
        elif msg == 1 and c.type in (7, 24):
            c.boost_active = c.boost_frames            # cBoostNode::vfunc_24 0x1408D0: re-arm the window

    def _counter_message(self, c: Controller, msg: int, value: float):
        bit = {float(k): 1 << (k - 1) for k in range(1, 11)}.get(float(F32(value)), 0)
        hit = False
        if msg == 1:
            if not c.mask & bit:
                c.mask |= bit
                hit = True
        elif msg == 2:
            if not c.mask & bit and (float(F32(value)) == 1.0 or c.mask & (bit >> 1)):
                c.mask |= bit if float(F32(value)) != 1.0 else 1
                hit = True
        elif msg == 3:
            hit = True
        if hit and c.count > 0:
            c.count -= 1

    def _set_enabled(self, inst: int, c: Controller, on: int):
        """Controller slot 22.  on = 1 ('done'): run the instance's slot-3 effect if it has one
        and stop there; otherwise (and always for on = 0) the class default."""
        if c.type in (TYPE_DEAD, TYPE_RESTORE):
            return
        if c.type == 15:                             # cLapBoostNode::vfunc_22 0x141198: only the slot-3 effect
            if on:
                self._run_slot(inst, SLOT_DISABLED, None)
            return
        if on and self._run_slot(inst, SLOT_DISABLED, None):
            return
        if c.type == 0:
            self._out(("roller_stop", inst))         # Roller slot 13: TODO
            return
        if c.type in (2, 8, 14) and c.timer < 0:
            return
        if c.type not in (2, 8):
            self._restore_flags(inst, keep_cell_bit=True)
        self.controllers.pop(inst, None)

    def _deactivate_controller(self, inst: int, c: Controller):
        """Controller slot 23 (cell unloaded and no slot-2 effect)."""
        if c.type in (TYPE_DEAD, 15):                # cLapBoostNode::vfunc_23 0x141190 is empty
            return
        if c.type in (2, 8, 14) and c.timer < 0:
            return
        if c.type not in (2, 8):
            self._restore_flags(inst, keep_cell_bit=False)
        self.controllers.pop(inst, None)

    def _controller_frame(self, inst: int, c: Controller):
        if c.touch_cooldown > 0 and (c.type not in ANIM_TYPES
                                     or (self.anim_cooldown_decrements and c.touch_cooldown_reset >= 0)):
            c.touch_cooldown -= 1
        if c.type == 2:                                 # Debounce
            if c.timer > 0:
                c.timer -= 1
                if c.timer == 0:
                    self._set_enabled(inst, c, 1)
        elif c.type == 8:                               # Timer (no retail record)
            c.count -= 1
            if c.count < 1:
                self._run_slot(inst, SLOT_EXPIRED, None)
                self._set_enabled(inst, c, 0)
                if inst not in self.controllers:
                    return
            if c.timer > 0:
                c.timer -= 1
                if c.timer == 0:
                    self._set_enabled(inst, c, 1)
        elif c.type == 6:                               # Counter
            if c.count < 1:
                self._run_slot(inst, SLOT_EXPIRED, None)
                self._set_enabled(inst, c, 0)
            elif c.timer > 0:
                c.timer -= 1
                if c.timer == 0:
                    self._set_enabled(inst, c, 1)
        elif c.type == 14:                              # Cracked
            if c.timer > 0:
                c.timer -= 1
                if c.timer == 0:
                    self._set_enabled(inst, c, 1)
                    return
            if c.health <= 0.0 and self.controllers.get(inst) is c:
                if not self._run_slot(inst, SLOT_EXPIRED, None):
                    self._destroy(inst)
                    self._state(inst, 2)
        elif c.type in (7, 24, 18) and self.controllers.get(inst) is c:
            # Boost / TubeEnd (0x140688) and ZBoost (0x141CD8) without riders in the volume (the model's riders have no
            # position; the rebuild's FEffectWorld has the rider queries): alive only while mode 0 or the window runs,
            # else done(1). LapBoost (15) never ends by itself.
            alive = c.type != 18 and c.boost_mode == 0
            if c.type != 18 and c.boost_active > 0:
                c.boost_active -= 1
                if c.boost_active > 0:
                    alive = True
            if not alive:
                self._set_enabled(inst, c, 1)

    def _run_slot(self, inst: int, slot: int, rider: Optional[Rider]) -> bool:
        e = self.ssf.effect_for(inst, slot)
        if e < 0:
            return False
        self.run_effect(e, inst, rider)
        return True

    def _emitter(self, s: Script, r: Record):
        kind = r.i(0)
        c = self.controllers.get(s.inst) if s.inst is not None else None
        if c is not None and c.type == TYPE_DEAD:
            return
        if kind == 0:
            self._out(("emitter", s.inst, r.offset))
        elif kind == 1 and r.i(1) != -1:
            self._out(("spline_path", s.inst, r.i(1), r.offset))
        elif kind == 2 and s.rider is not None:
            self._out(("collide_emitter", s.inst, r.offset))

    def _set_rail(self, spline: int, enable: int):
        self.rail_requests[spline] = enable
        if 0 <= spline < len(self.rail_enabled):
            self.rail_enabled[spline] = enable & 1
        self._out(("rail", spline, enable))

    def touch(self, inst: int, rider: Rider, impact: Optional[Tuple[float, float]] = None):
        """Rider contact with an unsurfaced collidable prop.
        impact = (v_hat . n, a) of the contact record, used by Cracked."""
        c = self.controllers.get(inst)
        if c is None or self._slot12(c):
            self._run_slot(inst, SLOT_TOUCH, rider)
            return
        if c.type in TOUCH_FORWARD_COOLDOWN or c.type in ANIM_TYPES:
            ready = c.touch_cooldown < 1 if c.type in TOUCH_FORWARD_COOLDOWN else c.touch_cooldown == 0
            if ready:
                self._run_slot(inst, SLOT_TOUCH, rider)
                c.touch_cooldown = c.touch_cooldown_reset if c.type in ANIM_TYPES else TOUCH_FORWARD_COOLDOWN[c.type]
        elif c.type == 14 and impact is not None and c.touch_cooldown == 0 and c.health > 0.0:
            c.touch_cooldown = 30
            c.health -= abs(impact[0] * impact[1]) * CRACK_DAMAGE
        elif c.type == 12:
            self._out(("fence_wobble", inst))
        # every other controller class ignores touches

    def activate(self, inst: int):
        """Cell activation for one instance (slot 0)."""
        self.flags[inst] |= FL_CELL_ACTIVE
        c = self.controllers.get(inst)
        if c is None:
            self._run_slot(inst, SLOT_ACTIVATE, None)
        elif self._slot12(c):
            self._out(("node_restart", inst, NODE_CLASSES.get(c.type, c.type)))

    def deactivate(self, inst: int):
        """Cell deactivation for one instance (slot 2, or the controller's slot 23)."""
        self.flags[inst] &= ~FL_CELL_ACTIVE
        c = self.controllers.get(inst)
        if c is not None and c.type == TYPE_DEAD:
            return
        if not self._run_slot(inst, SLOT_DEACTIVATE, None) and c is not None:
            self._deactivate_controller(inst, c)

    def _out(self, o: Outcome):
        self.log.append(o)

    def _rider(self, rider: Rider, o: Outcome):
        rider.log.append(o)
        self.log.append(("rider",) + o)


@dataclass
class Grid:
    min_x: float
    min_y: float
    cell: float
    nx: int
    ny: int
    cell_instances: Dict[int, List[int]]       # cell index -> instances over its 16 sub-cells


def grid_from_ltg(ltg_bytes: bytes, showoff: bool) -> Grid:
    """Cells and their instance lists (normal count, or the alternate count in ShowOff)."""
    b = ltg_bytes
    minv = struct.unpack_from("<3f", b, 4)
    cell = struct.unpack_from("<f", b, 0x28)[0]
    nx, ny, ncell = struct.unpack_from("<3i", b, 0x2C)
    _sub_side, sub_n, table = struct.unpack_from("<3i", b, 0x44)
    offs = struct.unpack_from(f"<{ncell}I", b, table)
    cells = {}
    for ci, co in enumerate(offs):
        if not co:
            continue
        sub0 = co + struct.unpack_from("<I", b, co + 0x38)[0]
        insts = []
        for k in range(sub_n):
            so = sub0 + 76 * k
            n_norm, n_alt = struct.unpack_from("<2H", b, so + 0x26)
            p_inst = struct.unpack_from("<I", b, so + 0x38)[0]
            n = n_alt if showoff else n_norm
            if p_inst and n:
                insts.extend(struct.unpack_from(f"<{n}i", b, co + p_inst))
        cells[ci] = insts
    return Grid(minv[0], minv[1], cell, nx, ny, cells)


def cells_around(g: Grid, x: float, y: float) -> List[int]:
    i = int((x - g.min_x + g.cell) / g.cell)
    j = int((y - g.min_y + g.cell) / g.cell)
    out = []
    for ix in range(max(i - CELL_RADIUS, 0), min(i, g.nx - 1) + 1):
        for iy in range(max(j - CELL_RADIUS, 0), min(j, g.ny - 1) + 1):
            out.append(iy * g.nx + ix)
    return out


class CellTracker:
    """cAIWorld's active-cell slots: 108 fixed slots {cell, count, new}.
    One update: every occupied slot's count -= 1; the 3x3 block around each point is marked in order
    (ix outer, iy inner; a known cell gets count 1, a new one takes the lowest free slot with the new
    flag); then the slots in order: new -> activate (slot 0), count < 1 -> deactivate (slot 2), freed."""

    def __init__(self, grid: Grid):
        self.grid = grid
        self.slots = [None] * CELL_SLOTS        # [cell, count, new] or None

    @property
    def active(self):
        return {s[0]: 1 for s in self.slots if s is not None}

    def clear(self):
        """0x115DF0: every slot freed, no deactivation."""
        self.slots = [None] * CELL_SLOTS

    def _mark(self, c):
        if c not in self.grid.cell_instances:
            return
        free = None
        for k, s in enumerate(self.slots):
            if s is not None and s[0] == c:
                s[1] = 1
                return
            if free is None and s is None:
                free = k
        if free is not None:
            self.slots[free] = [c, 1, True]

    def step(self, world: World, points: List[Tuple[float, float]]):
        new, gone = [], []
        for s in self.slots:
            if s is not None:
                s[1] -= 1
        for x, y in points:
            for c in cells_around(self.grid, x, y):
                self._mark(c)
        for k, s in enumerate(self.slots):
            if s is None:
                continue
            if s[2]:
                for i in self.grid.cell_instances[s[0]]:
                    world.activate(i)
                s[2] = False
                new.append(s[0])
            elif s[1] < 1:
                for i in self.grid.cell_instances[s[0]]:
                    world.deactivate(i)
                gone.append(s[0])
                self.slots[k] = None
        return new, gone


def cell_marker(g: Grid, x: float, y: float) -> int:
    """Rider +0x16C (0x11D438): the cell one step down-left of the block corner, clamped."""
    i = min(max(int((x - g.min_x + g.cell) / g.cell) - 1, 0), g.nx - 1)
    j = min(max(int((y - g.min_y + g.cell) / g.cell) - 1, 0), g.ny - 1)
    return j * g.nx + i


def cell_update_points(markers, prev, phase_requested, racer_points, camera_point):
    """cAIWorld 0x115E28 skip rule: None = skip the update, else the points to mark (racers, then the
    camera point before StartRace when racer 0 kept its cell)."""
    if not markers:
        return None
    if prev[0] != markers[0]:
        return list(racer_points)
    if phase_requested < 2:
        return list(racer_points) + ([camera_point] if camera_point is not None else [])
    if any(p != m for p, m in zip(prev, markers)):
        return list(racer_points)
    return None


def trigger_instances(ssf: Ssf) -> List[dict]:
    """Instances a rider can touch (collidable 0x20, no surface) that have a slot-1 effect, with
    the reaction class (0x80) and the model type (2 = box volume)."""
    out = []
    for i in range(len(ssf.inst_index)):
        rec = ssf.inst_records[ssf.inst_index[i]]
        fl = (rec[2] >> 16) & 0xFFFF
        if not (fl & FL_COLLIDABLE) or rec[3] != -1:
            continue
        e = ssf.effect_for(i, SLOT_TOUCH)
        if e < 0:
            continue
        out.append(dict(instance=i, effect=e, reacts=bool(fl & FL_REACTS), model_type=rec[4],
                        activate=ssf.effect_for(i, SLOT_ACTIVATE)))
    return out


def effect_summary(ssf: Ssf, e: int) -> str:
    parts = []
    for r in ssf.effects[e]:
        d = decode(r)
        op = d.pop("op")
        parts.append(op + ("(" + ",".join(f"{k}={v:g}" if isinstance(v, float) else f"{k}={v}"
                                          for k, v in d.items()) + ")" if d else ""))
    return " ".join(parts)


# Path-event outcomes (not .ssf; models/race.py, models/race_audio.py): internal type -> outcome
PATH_EVENT_OUTCOMES = {
    1: "music event 0 (humans) + announcer cue 0", 2: "breakdown zone 1 enter (music 1)",
    3: "breakdown zone 1 exit line", 4: "breakdown zone 2 enter (music 3)", 5: "breakdown zone 2 exit line",
    6: "breakdown zone 3 enter (music 5)", 7: "breakdown zone 3 exit line", 9: "music event 9 + announcer cue 9",
    10: "finish (laps left 0: finish time, music event 10)", 12: "checkpoint (popup 0xA / 0xB, announcer rank)",
    13: "lap line (popup 0xD)", 14: "audio zone A", 18: "final-lap cue (music event 10)",
    19: "announcer", 20: "announcer", 21: "announcer", 22: "announcer", 23: "audio zone B", 24: "audio zone C",
    31: "re-pick marker"}
