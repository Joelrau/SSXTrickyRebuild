"""Race-mode model: track paths and events, race phases, progress, ranking, catch-up, finish,
results, respawn placement, rivalry and track-trigger effects.

Named quantities only. Units: cm, s, rad; the
game runs at a fixed 60 Hz. Z is up.

Pieces, in the order the game runs them each frame:
  * race handler (phase RACE): Tricky timer / passive drain per racer (models/boarder.py), rider
    pair distances and bearings (`update_rival_geometry`), ranking and catch-up targets every 6th
    frame (`rank_racers`, `assign_catchup`), race clock.
  * per racer, inside the boarder update (phase RACE only): `TrackProgress.update` projects the rider
    on its line path, emits trigger enter/leave events, and `on_trigger` applies them.
  * AI riders read `catchup_time_scale` / `select_skill` (models/ai.py).
"""
from __future__ import annotations

import math
import struct
from dataclasses import dataclass, field
from enum import IntEnum
from typing import List, Optional

import numpy as np

from ee_math import atan2_ee, cos_ee as _cos_ee
from fpu import fpu_add, fpu_sub

FRAME_DT = 1.0 / 60.0
TWO_PI = 2.0 * math.pi
KMH_TO_CMS = 27.777777                  # event params in km/h -> cm/s


def wrap_angle(a: float) -> float:
    """Wrap to [-pi, pi) the way the game does (floor of a/2pi + 0.5)."""
    return a - math.floor(a / TWO_PI + 0.5) * TWO_PI


class EventMode(IntEnum):
    """Global event mode (names from the mode string table)."""
    PRACTICE_KNOCKDOWN = 0
    PRACTICE_FREERIDE = 1
    PRACTICE_RACE = 2          # single-event race
    PRACTICE_SHOWOFF = 3       # single-event showoff (score)
    CIRCUIT_RACE = 4           # World Circuit race (heats / finals)
    CIRCUIT_SHOWOFF = 5        # World Circuit showoff (score)
    LESSON = 6
    PRACTICE_TIMEDRACE = 7
    DEMO = 8
    TEST = 9


SCORE_MODES = (EventMode.PRACTICE_SHOWOFF, EventMode.CIRCUIT_SHOWOFF)
GAP_MODES = (EventMode.PRACTICE_RACE, EventMode.CIRCUIT_RACE)


class RacePhase(IntEnum):
    """race_state+0x1C; each value selects a handler object."""
    NONE = 0
    PRE_RACE = 1
    START_RACE = 2
    COUNTDOWN = 3              # the start handler switches straight to RACE (3 -> handler 'Race')
    RACE = 4
    END_RACE = 5
    LOAD_LESSON = 6
    PRE_LESSON = 7
    RESTART_LESSON = 8
    SELECT_LESSON = 9
    END_LESSON = 10
    REPLAY_END_RACE = 11


# File event type -> internal event type: internal = index of the file type in this table.
# So file -1 -> 0, file 0..23 -> 1..24, file 100..105 -> 25..30, file 300 -> 31; unknown -> 0.
EVENT_TYPE_TABLE = [-1] + list(range(0, 24)) + [100, 101, 102, 103, 104, 105, 300]


def internal_event_type(file_type: int) -> int:
    for i, t in enumerate(EVENT_TYPE_TABLE):
        if t == file_type:
            return i
    return 0


class Ev(IntEnum):
    """Internal event types (bit index in the trigger mask)."""
    MARKER = 0                 # file -1 (announcer slot 0 cue)
    CUE_1 = 1                  # file 0: announcer cue 0
    ZONE_2 = 2                 # file 1: long zones (audio / ambience), generic enter/leave cue only
    ZONE_3 = 3                 # file 2
    ZONE_4 = 4                 # file 3
    ZONE_5 = 5                 # file 4
    CUE_9 = 9                  # file 8: announcer cue 9
    FINISH = 10                # file 9: finish line
    CHECKPOINT = 12            # file 11: checkpoint (param = showoff points)
    LAP = 13                   # file 12: lap line
    AUDIO_ZONE_A = 14          # file 13: audio zone A (global blend target 0 while inside)
    FINAL_LAP = 18             # file 17: final-lap / finish-approach cue
    CUE_19 = 19                # file 18
    CUE_20 = 20                # file 19
    CUE_21 = 21                # file 20
    CUE_22 = 22                # file 21
    AUDIO_ZONE_B = 23          # file 22
    AUDIO_ZONE_C = 24          # file 23
    AI_JUMP = 25               # file 100: AI take-off zone (param: bit0 flip, bit1 spin, >>2 km/h)
    AI_SPEED = 26              # file 101: AI speed request (param km/h)
    AI_102 = 27                # file 102: unused by the decoded AI code
    AI_105 = 30                # file 105: unused by the decoded AI code
    MARK_300 = 31              # file 300: point markers (no handler found)


@dataclass
class PathEvent:
    type: int                  # internal type
    param: int
    start: float               # cm along the path (horizontal length)
    end: float
    file_type: int = 0


@dataclass
class Path:
    """One polyline path. Segments are (dir_x, dir_y, slope, horizontal_length); the point at
    horizontal length t inside a segment is a + (dir_x, dir_y, slope) * t."""
    start: np.ndarray
    segs: np.ndarray            # (n, 4)
    events: List[PathEvent]
    bbox_min: np.ndarray
    bbox_max: np.ndarray
    props: dict = field(default_factory=dict)
    index: int = -1
    kind: str = "ai"            # "ai" (racing line) or "line" (event / progress path)

    def __post_init__(self):
        # 0x197478 / 0x1979F8 walk the knots in single precision: knot + dir x len (0x102F50, VADD), cum + len (add.s)
        d = np.c_[self.segs[:, 0], self.segs[:, 1], self.segs[:, 2]].astype(np.float32)
        self.dirs = d.astype(np.float64)
        self.lens = self.segs[:, 3].astype(np.float32).astype(np.float64)
        n = len(self.lens)
        knots = np.zeros((n + 1, 3), dtype=np.float32)
        cum = np.zeros(n + 1, dtype=np.float32)
        knots[0] = np.asarray(self.start, dtype=np.float32)
        for i in range(n):
            ln = np.float32(self.lens[i])
            knots[i + 1] = knots[i] + ln * d[i]
            cum[i + 1] = fpu_add(cum[i], ln)
        self.knots = knots.astype(np.float64)
        self.cum = cum.astype(np.float64)

    # path properties: AI paths carry key 100 (line class, default 50) and key 101 (flag, default 1);
    # line paths carry key 0 = distance to go at the path start.
    @property
    def line_class(self) -> int:
        return int(self.props.get(100, 50))

    @property
    def flag(self) -> int:
        return int(self.props.get(101, 1))

    @property
    def dist_to_go_at_start(self) -> float:
        return float(self.props.get(0, 0.0))

    def length(self) -> float:
        return float(self.cum[-1])

    def point_at(self, d: float) -> np.ndarray:
        """Point at horizontal length d; before the start it extrapolates the first segment,
        beyond the end it returns the end point."""
        if len(self.lens) == 0:
            return self.start.copy()
        if d >= self.cum[-1]:
            return self.knots[-1].copy()
        # 0x1979F8: d -= len per passed segment (sub.s), then knot + dir x d
        df = np.float32(d)
        i = 0
        while i < len(self.lens) - 1 and not (df < np.float32(self.lens[i])):
            df = fpu_sub(df, np.float32(self.lens[i]))
            i += 1
        return (self.knots[i].astype(np.float32) + df * self.dirs[i].astype(np.float32)).astype(np.float64)

    def tangent_at(self, d: float) -> np.ndarray:
        i = min(max(0, int(np.searchsorted(self.cum, d, side="right")) - 1), len(self.lens) - 1)
        t = self.dirs[i]
        return t / np.linalg.norm(t)

    def project(self, p: np.ndarray, cache: Optional[dict] = None):
        """Closest point on the path, all in the horizontal plane: returns (param, closest_point,
        horizontal lateral distance).

        With a cache (the per-rider query record) the game's incremental search is used: start at the
        cached segment, never look back, stop at the first improving segment whose projection is not
        clamped, or 3000 cm past the cached segment start. Without a valid cache: global minimum."""
        start = cache.get("seg", -1) if cache is not None else -1
        # 0x197478 in single precision: r = p - knot (VSUB), raw = r.x d.x + r.y d.y (FPU), t clamped to [0, len],
        # q = knot + d t, d2 = (p - q).x^2 + (p - q).y^2; the lateral by sqrt.s, the param cum + t (add.s)
        f = np.float32
        pf = np.asarray(p, dtype=np.float32)

        def seg(k):
            a = self.knots[k].astype(np.float32)
            dd = self.dirs[k].astype(np.float32)
            r = pf - a
            raw = fpu_add(r[0] * dd[0], r[1] * dd[1])
            ln = f(self.lens[k])
            t = f(0.0) if raw < 0.0 else (ln if ln < raw else raw)
            q = a + t * dd
            r2 = pf - q
            return fpu_add(r2[0] * r2[0], r2[1] * r2[1]), raw, t, q

        if start < 0 or start >= len(self.lens):
            best = None
            for k in range(len(self.lens)):
                d2, raw, t, q = seg(k)
                if best is None or d2 < best[0]:
                    best = (d2, k, t, q)
            d2, k, t, q = best
            if cache is not None:
                cache["seg"] = k
            return float(fpu_add(f(self.cum[k]), t)), q.astype(np.float64), float(f(math.sqrt(float(d2))))
        limit = fpu_add(f(self.cum[start]), f(3000.0))
        best = None
        for k in range(start, len(self.lens)):
            d2, raw, t, q = seg(k)
            if best is None or d2 < best[0]:
                best = (d2, k, t, q)
                if t == raw:
                    break
            if limit < f(self.cum[k]):
                break
        d2, k, t, q = best
        cache["seg"] = k
        return float(fpu_add(f(self.cum[k]), t)), q.astype(np.float64), float(f(math.sqrt(float(d2))))

    def bbox_distance(self, p: np.ndarray) -> float:
        """Chebyshev distance from p to the path's bounding box (0 inside)."""
        out = np.maximum(self.bbox_min - p, 0.0) + np.maximum(p - self.bbox_max, 0.0)
        return float(np.max(out))

    def events_in(self, d0: float, d1: float):
        """Events whose range overlaps [d0, d1]. Returns (event, edge) pairs; edge is True when the
        event starts after d0 or ends after d1 (the mask bit the game reports)."""
        if d1 < d0:
            return []
        out = []
        for e in self.events:
            if d1 < e.start or e.end < d0:
                continue
            out.append((e, d0 < e.start or d1 < e.end))
        return out


@dataclass
class Track:
    ai_paths: List[Path]
    line_paths: List[Path]
    start_paths: List[int]       # AI path index per start slot (6)
    name: str = ""

    def main_line(self) -> Path:
        """The line path with the largest distance to go at its start (the race start)."""
        return max(self.line_paths, key=lambda p: p.dist_to_go_at_start)

    def race_length(self) -> float:
        return self.main_line().dist_to_go_at_start


def _read_path(data: bytes, off: int, kind: str, index: int):
    nprops, = struct.unpack_from("<I", data, off)
    off += 4
    props = {}
    for _ in range(nprops):
        key, size = struct.unpack_from("<2I", data, off)
        raw = data[off + 8: off + 8 + size]
        props[key] = struct.unpack("<f", raw)[0] if (kind == "line" and key == 0) else struct.unpack("<i", raw)[0]
        off += 8 + size
    nseg, nev = struct.unpack_from("<2I", data, off)
    off += 8
    start = np.array(struct.unpack_from("<3f", data, off))
    bmin = np.array(struct.unpack_from("<3f", data, off + 12))
    bmax = np.array(struct.unpack_from("<3f", data, off + 24))
    off += 36
    segs = np.frombuffer(data, "<f4", nseg * 4, off).reshape(nseg, 4).astype(np.float64)
    off += 16 * nseg
    events = []
    for i in range(nev):
        t, prm, s, e = struct.unpack_from("<iI2f", data, off + 16 * i)
        events.append(PathEvent(internal_event_type(t), prm, s, e, t))
    off += 16 * nev
    return Path(start, segs, events, bmin, bmax, props, index, kind), off


AIP_MAGIC = int.from_bytes(bytes([10, 10, 10, 10]), "little")     # four newline bytes


def load_track(path_file: str) -> Track:
    """Parse an .aip / .sop file: magic, chunk count, then chunks {kind, size, body}. Kind 0 holds the
    AI racing lines (with the 6 start-slot indices), kind 1 the line (event/progress) paths."""
    data = open(path_file, "rb").read()
    magic, nchunks = struct.unpack_from("<2I", data, 0)
    if magic != AIP_MAGIC:
        raise ValueError("not an AI path file")
    off = 8
    ai, line, starts = [], [], []
    for _ in range(nchunks):
        kind, size = struct.unpack_from("<2I", data, off)
        body = off + 8
        if kind == 0:
            n, nstart = struct.unpack_from("<2I", data, body)
            starts = list(struct.unpack_from("<%dI" % nstart, data, body + 8))
            o = body + 8 + 4 * nstart
            for i in range(n):
                p, o = _read_path(data, o, "ai", i)
                ai.append(p)
        elif kind == 1:
            n, = struct.unpack_from("<I", data, body)
            o = body + 8
            for i in range(n):
                p, o = _read_path(data, o, "line", i)
                line.append(p)
        off = body + size
    return Track(ai, line, starts, path_file)


LINE_REPICK_LATERAL = 500.0          # cm off the line path -> re-pick (checked every 60 frames)
LINE_END_MARGIN = 200.0              # cm before the end of a path -> re-pick (excluding the current)
LINE_REPICK_PROBE = 800.0            # probe point = pos + 800 * v_hat
LINE_REPICK_RESET_GAP = 1600.0       # cm: branch flag reset when the re-projected distance jumps


@dataclass
class TrackProgress:
    """Progress of one racer along the line (event) paths: distance to go, best distance, trigger mask."""
    line: Path
    dist_to_go: float = 0.0
    best: float = 1e30               # smallest distance to go reached (trigger queries run from here)
    mask: int = 0
    mask_prev: int = 0
    lap_count: int = 0
    cache: dict = field(default_factory=dict)

    def update(self, pos, vel, track: Track, frame: int, finished: bool, lap_length: float = 0.0):
        """One frame of the track scan. Returns [(type, param, entering)] in the order the game calls
        the trigger handler (enter events for every overlapping event, then leave events)."""
        self.mask_prev, self.mask = self.mask, 0
        param, _, lateral = self.line.project(pos, self.cache)
        self.dist_to_go = self.line.dist_to_go_at_start - param
        if self.lap_count > 0 and self.dist_to_go - self.best > 0.5 * lap_length > 0.0:
            self.best = self.dist_to_go          # wrapped round a lap
        progressed = self.dist_to_go < self.best
        lo, hi = (self.dist_to_go, self.best) if progressed else (self.best, self.best)
        hits = self._query(lo, hi)
        if progressed:
            self.best = self.dist_to_go
        if not finished:
            old = self.line
            near_end = self.line.dist_to_go_at_start - self.line.length() + LINE_END_MARGIN > self.dist_to_go
            if (lateral > LINE_REPICK_LATERAL and frame % 60 == 0) or near_end:
                self.line = pick_line_path(track, pos, vel, self.line if near_end else None)
            if self.line is not old:
                self.cache = {}
                param, _, _ = self.line.project(pos, self.cache)
                self.dist_to_go = self.line.dist_to_go_at_start - param
                if self.dist_to_go < self.best:
                    hits += self._query(self.dist_to_go, self.best)
                    self.best = self.dist_to_go
        out = []
        for e in hits:
            self.mask |= 1 << (e.type & 31)
            out.append((e.type, e.param, True))
        for b in range(32):
            if (self.mask_prev & ~self.mask) >> b & 1:
                out.append((b, 0, False))
        return out

    def _query(self, lo_to_go, hi_to_go):
        base = self.line.dist_to_go_at_start
        return [e for e, _ in self.line.events_in(base - hi_to_go, base - lo_to_go)]


def pick_line_path(track: Track, pos, vel, exclude: Optional[Path]):
    """Re-pick the line path: probe 800 cm ahead along the velocity and take the candidate whose
    point 800 cm beyond the probe projection is closest (plus the rider's own offset)."""
    sp = np.linalg.norm(vel)
    probe = pos + (vel / sp * LINE_REPICK_PROBE if sp > 1e-6 else 0.0)
    cands = sorted(track.line_paths, key=lambda p: p.bbox_distance(probe))[:3]
    best, best_cost = exclude if exclude is not None else cands[0], 1e37
    for p in cands:
        if p is exclude:
            continue
        t, q, _ = p.project(probe)
        ahead = p.point_at(fpu_add(np.float32(t), np.float32(LINE_REPICK_PROBE)))
        cost = float(np.sum((probe - ahead) ** 2) + np.sum((pos - q) ** 2))
        if cost < best_cost:
            best, best_cost = p, cost
    return best


@dataclass
class RacerRace:
    """Race-side fields of one racer (cBoarder +0x20 .. +0x41C)."""
    index: int
    is_human: bool
    progress: TrackProgress
    finish_cs: int = -1                    # finish time in centiseconds (the score in showoff modes)
    rank: int = 0
    prev_rank: int = 0                     # rank announced at the last checkpoint
    checkpoints: int = 0
    score: float = 0.0                     # race_stats score (showoff)
    catchup_rival: int = -1
    catchup_near: float = 0.0
    catchup_far: float = 0.0
    time_scale: float = 1.0
    race_total: float = 0.0                # whole race distance (all laps)
    best_lap_mark: float = 0.0             # dist-to-go value that decides whether a lap is still pending
    gap_cs: int = 0                        # last checkpoint time gap to the adjacent-rank racer
    cues: list = field(default_factory=list)


def on_trigger(r: RacerRace, race: "Race", ev_type: int, param: int, entering: bool):
    """Apply one trigger event (enter or leave) to a racer. Only racing-relevant effects are
    modelled; audio / announcer cues are appended to r.cues."""
    if not entering:
        r.cues.append(("leave", ev_type))
        return
    if ev_type == Ev.FINISH and r.progress.lap_count == 0:
        finish_racer(r, race)
    if ev_type == Ev.LAP and r.progress.lap_count > 0:
        r.progress.lap_count -= 1
        r.cues.append(("lap", r.progress.lap_count))
    if ev_type == Ev.CHECKPOINT and r.finish_cs < 0:
        checkpoint(r, race, param)
        r.cues.append(("rank", r.prev_rank, r.rank))
        r.prev_rank = r.rank
    r.cues.append(("enter", ev_type, param))


def finish_racer(r: RacerRace, race: "Race"):
    if r.finish_cs >= 0:
        return
    r.finish_cs = int(race.clock * 100.0)
    if race.mode in SCORE_MODES and r.finish_cs == 0:
        r.cues.append(("timed_out",))
    r.cues.append(("finish", r.finish_cs))


def checkpoint(r: RacerRace, race: "Race", param: int):
    """Showoff: count the checkpoint; the first racer to reach this count adds `param` seconds to
    the shared clock and to its stats. Race modes: record the time gap to the adjacent rank."""
    if race.mode in SCORE_MODES:
        r.checkpoints += 1
        if all(o is r or o.checkpoints < r.checkpoints for o in race.racers):
            race.clock += float(param)
            r.cues.append(("time_bonus", param))
    elif race.mode in GAP_MODES:
        r.gap_cs = checkpoint_gap_cs(r, race)


def remaining_distance(r: RacerRace, lap_length: float) -> float:
    """Distance still to ride over all laps."""
    pr = r.progress
    if pr.lap_count == 0:
        return pr.dist_to_go
    laps = pr.lap_count if pr.dist_to_go < r.best_lap_mark else pr.lap_count - 1
    return pr.dist_to_go + laps * max(lap_length, 1.0)


def elapsed_cs(r: RacerRace, race: "Race") -> int:
    if r.finish_cs >= 0:
        return max(r.finish_cs, 0)
    return max(int(race.clock * 100.0), 0)


def checkpoint_gap_cs(r: RacerRace, race: "Race") -> int:
    """Time gap to the racer of adjacent rank (leader: to 2nd; others: to the leader), estimated by
    scaling elapsed time with remaining distance."""
    for o in race.racers:
        if (r.rank == 0 and o.rank == 1) or (r.rank > 0 and o.rank == 0):
            total = r.race_total
            done_o = total - remaining_distance(o, race.lap_length)
            done_r = total - remaining_distance(r, race.lap_length)
            if done_r <= 0.0:
                if done_o <= 0.0:
                    return 0
                pace = elapsed_cs(o, race) / done_o
            else:
                pace = elapsed_cs(r, race) / done_r
            return int(pace * (done_o - done_r)) + elapsed_cs(r, race) - elapsed_cs(o, race)
    return 0


def estimated_finish_cs(r: RacerRace, race: "Race") -> int:
    """Finished: the finish time. Otherwise elapsed * total / (total - remaining)."""
    if r.finish_cs >= 0:
        return elapsed_cs(r, race)
    done = r.race_total - remaining_distance(r, race.lap_length)
    if done <= 0.0:
        return elapsed_cs(r, race)
    return int(elapsed_cs(r, race) * r.race_total / done)


RANK_PERIOD = 6                       # frames
RANK_HYSTERESIS = 20.0                # cm per current rank added to the remaining distance

# Catch-up targets (World Circuit and single race). Per racer index (0..5).
# 0x365060 / 0x365078 / 0x365090 / 0x3650A8 (.data, single precision, as stored)
CATCHUP_NEAR_RACE = (-800.169677734375, -1101.80322265625, -1415.9346923828125, -1809.329833984375, -3506.75390625, -3911.97998046875)
CATCHUP_FAR_RACE = (6030.36328125, 5509.6865234375, 5008.2197265625, 5000.6826171875, 4017.697998046875, 1508.1605224609375)
CATCHUP_NEAR_CIRCUIT = (-1869.0435791015625, -2147.75439453125, -2427.93994140625, -2937.53173828125, -3560.376708984375, -4086.20751953125)
CATCHUP_FAR_CIRCUIT = (5014.83935546875, 4519.08154296875, 4034.012939453125, 4097.28466796875, 3029.933837890625, 2022.4232177734375)
# per difficulty: (keep fraction of the human's remaining distance, scale of the track offset)
CATCHUP_DIFFICULTY_RACE = {0: (1.0, 0.600729), 1: (0.5, 0.4006506), 2: (0.0, 0.3501341)}
CATCHUP_DIFFICULTY_CIRCUIT = {0: (1.0, 0.60011744), 1: (0.5, 0.39999962), 2: (0.0, 0.35052386)}
# per track: (late-race distance threshold, offset added to the table values)
CATCHUP_TRACK = {0: (140005.72, 1532.3651), 1: (125085.09, 907.67194), 2: (159083.5, 421.18005),
                 3: (138040.55, -504.2323), 4: (142028.88, 503.4722), 5: (131061.625, 46.695477),
                 8: (40011.445, 0.0), 11: (160036.53, 0.0)}
SHOWOFF_TIME_LIMIT = {0: 120.0, 1: 90.0, 2: 90.0, 3: 90.0, 4: 90.0, 5: 90.0, 6: 90.0, 7: 0.0, 8: 90.0, 9: 90.0,
                      10: 0.0, 11: 135.0}   # s, per track id
KNOCKDOWN_CATCHUP = float(np.float32(517.34393))  # 0x44015603              # +-cm in knockdown mode (everyone chases the racer ahead)

AI_CATCHUP_GAIN, AI_CATCHUP_FAR_GAIN = 1.1359849, 2.0487268
AI_TIME_SCALE_MIN, AI_TIME_SCALE_MAX = 0.70005476, 1.5022597
PLAYER_CATCHUP_GAIN, PLAYER_TIME_SCALE_MAX = 1.0941, 1.1657
TIME_SCALE_STEP = 0.008446341           # per frame


@dataclass
class Race:
    mode: EventMode
    track_id: int
    racers: List[RacerRace]
    lap_length: float
    difficulty: int = 1                 # single-event difficulty / circuit difficulty (0..2)
    phase: RacePhase = RacePhase.PRE_RACE
    clock: float = 0.0                  # s; counts down in showoff modes
    frame: int = 0
    showoff_time_limit: float = 0.0

    def enter_race(self, controls):
        """RACE handler enter: racers still in GateAnticipate get GateLaunch (control 8 -> 9); the clock
        starts at 0, or at the track's time limit in showoff modes."""
        self.phase = RacePhase.RACE
        self.clock = self.showoff_time_limit if self.mode in SCORE_MODES else 0.0
        return [9 if c == 8 else c for c in controls]

    def tick_clock(self):
        """RACE handler update. Returns True when the race should end (all humans finished)."""
        self.frame += 1
        if self.mode in SCORE_MODES:
            self.clock -= FRAME_DT
            if self.clock <= 0.0:
                self.clock = 0.0
                for r in self.racers:
                    finish_racer(r, self)
        else:
            self.clock += FRAME_DT
        humans = [r for r in self.racers if r.is_human] or self.racers
        return all(r.finish_cs >= 0 for r in humans)


def update_rival_geometry(positions):
    """Pairwise horizontal distance and bearing (direction from i toward j, radians in [-pi, pi)), in the single precision
    of 0x115A80: VU0 difference, sqrt(dx dx + dy dy), 0x2510F8, the reverse bearing wrapped by (a + pi) - floor(.) 2pi."""
    f = np.float32
    n = len(positions)
    dist = np.zeros((n, n))
    bearing = np.zeros((n, n))
    for i in range(n):
        for j in range(i + 1, n):
            dx = f(positions[i][0]) - f(positions[j][0])
            dy = f(positions[i][1]) - f(positions[j][1])
            h = f(math.sqrt(float(fpu_add(dx * dx, dy * dy))))
            a = atan2_ee(dy, dx)
            ap = fpu_add(a, f(3.1415927))
            kf = fpu_add(ap * f(0.15915494), f(0.5))
            fl = f(int(kf))
            if kf < fl:
                fl = fpu_sub(fl, f(1.0))
            dist[i, j] = dist[j, i] = h
            bearing[j, i] = a                                  # from j toward i
            bearing[i, j] = fpu_sub(ap, fl * f(6.2831855))     # from i toward j
    return dist, bearing


def rank_key(r: RacerRace, race: Race, knockdown_score: int = 0) -> float:
    if race.mode == EventMode.PRACTICE_KNOCKDOWN:
        return float(knockdown_score)
    if race.mode in SCORE_MODES:
        return r.score
    if race.mode == EventMode.LESSON:
        return float(r.index)
    if r.finish_cs >= 0:
        return float(2 ** 31 - 1 - r.finish_cs)
    return -(remaining_distance(r, race.lap_length) + r.rank * RANK_HYSTERESIS)


def rank_racers(race: Race, knockdown_scores=None):
    """Every 6th frame: sort by key (largest first) and store the rank."""
    if race.frame % RANK_PERIOD:
        return
    keys = [rank_key(r, race, (knockdown_scores or {}).get(r.index, 0)) for r in race.racers]
    order = sorted(range(len(race.racers)), key=lambda i: -keys[i])
    for rank, i in enumerate(order):
        race.racers[i].rank = rank
    return order


def assign_catchup(race: Race, order):
    """Every 6th frame (after ranking): pick each racer's catch-up rival and near/far thresholds."""
    if order is None or len(race.racers) < 2:
        return
    humans = [i for i in order if race.racers[i].is_human]
    if not humans:
        return
    if race.mode == EventMode.PRACTICE_KNOCKDOWN:
        for r in race.racers:
            r.catchup_near, r.catchup_far = -KNOCKDOWN_CATCHUP, KNOCKDOWN_CATCHUP
        race.racers[order[0]].catchup_rival = order[1]
        for k in range(1, len(order)):
            race.racers[order[k]].catchup_rival = order[k - 1]
        return
    if race.mode not in GAP_MODES:
        for r in race.racers:
            r.catchup_rival = -1
        return
    ais = [i for i in order if not race.racers[i].is_human]
    L = race.lap_length if race.lap_length > 0.0 else 1.0
    circuit = race.mode == EventMode.CIRCUIT_RACE
    keep, offset_scale = (CATCHUP_DIFFICULTY_CIRCUIT if circuit else CATCHUP_DIFFICULTY_RACE).get(
        race.difficulty, (1.0, 1.0))
    late, offset = CATCHUP_TRACK.get(race.track_id, (0.0, 0.0))
    near_tab = CATCHUP_NEAR_CIRCUIT if circuit else CATCHUP_NEAR_RACE
    far_tab = CATCHUP_FAR_CIRCUIT if circuit else CATCHUP_FAR_RACE
    per_human = max(1, len(race.racers) // len(humans))
    h = 0
    for k, ai_index in enumerate(ais):
        ai = race.racers[ai_index]
        hu = race.racers[humans[h]]
        ai.catchup_rival = humans[h]
        ai.catchup_far, ai.catchup_near = L, -L
        human_left = hu.progress.best
        # 0x115100 in single precision (EE FPU mul.s / add.s / sub.s)
        f = np.float32
        ofs = f(offset) * f(offset_scale)
        if ai.rank < hu.rank:                      # AI ahead of its human: how far ahead it may get
            far = fpu_add(f(far_tab[ai_index]), ofs) if far_tab[ai_index] >= 0.0 else f(L)
            extra = f(human_left) * fpu_sub(f(1.0), f(keep)) if f(human_left) < f(late) else f(0.0)
            ai.catchup_far = float(fpu_add(far, extra))
        elif not (f(human_left) < f(late)):        # AI behind, early in the race: how far behind
            ai.catchup_near = float(fpu_add(f(near_tab[ai_index]), ofs) if near_tab[ai_index] <= 0.0 else -f(L))
        if (k + 1) % per_human == 0:
            h = min(h + 1, len(humans) - 1)


_F = np.float32


def cos_ee(x: float) -> float:
    """0x250E98: the game's single-precision cosine (ee_math.cos_ee)."""
    return float(_cos_ee(x))


def catchup_lead(rival_dist: float, rival_bearing: float, path_heading: float) -> float:
    """How far this racer is ahead of its rival, measured along the race path heading (0x13A0F0, single precision)."""
    return float(-_F(rival_dist) * _F(cos_ee(fpu_sub(_F(rival_bearing), _F(path_heading)))))


def catchup_factor_ai(lead: Optional[float], near: float, far: float) -> float:
    """0x13A0F0 in single precision: behind near ((lead - near) / near + 1) x gain, past far 1 / (lead - far) x gain."""
    if lead is None:
        return 1.0
    l, n, fr = _F(lead), _F(near), _F(far)
    if l < n:
        f = fpu_add(fpu_sub(l, n) / n, _F(1.0)) * _F(AI_CATCHUP_GAIN)
    elif fr < l:
        f = (_F(1.0) / fpu_sub(l, fr)) * _F(AI_CATCHUP_FAR_GAIN)
    else:
        f = _F(1.0)
    if f < _F(AI_TIME_SCALE_MIN):
        f = _F(AI_TIME_SCALE_MIN)
    elif _F(AI_TIME_SCALE_MAX) < f:
        f = _F(AI_TIME_SCALE_MAX)
    return float(f)


def catchup_factor_player(lead: Optional[float], near: float) -> float:
    if lead is None or lead >= near:
        return 1.0
    f = ((lead - near) / near + 1.0) * PLAYER_CATCHUP_GAIN
    return min(max(f, 1.0), PLAYER_TIME_SCALE_MAX)


def step_time_scale(current: float, target: float) -> float:
    """0x11CFF0 in single precision (+0x12C; every live step matches the float sums)."""
    c, t, st = _F(current), _F(target), _F(TIME_SCALE_STEP)
    if fpu_add(t, st) < c:
        return float(fpu_sub(c, st))
    if c < fpu_sub(t, st):
        return float(fpu_add(c, st))
    return float(t)


# (skill while the AI is ahead of the human, skill while the human is ahead); both <= 1.
SKILL_CIRCUIT = {0: (0.404814, 0.754853), 1: (0.708402, 0.909216), 2: (0.951308, 1.0)}
SKILL_RACE = {0: (0.274136, 0.606922), 1: (0.656647, 0.872325), 2: (0.949089, 1.0)}
SKILL_OTHER = (0.50875, 1.0)
SKILL_PERIOD = 60                         # frames between skill re-selection


def skill_levels(mode: EventMode, difficulty: int):
    if mode == EventMode.CIRCUIT_RACE:
        lv = SKILL_CIRCUIT[min(max(difficulty, 0), 2)]
    elif mode == EventMode.PRACTICE_RACE:
        lv = SKILL_RACE[min(max(difficulty, 0), 2)]
    else:
        lv = SKILL_OTHER
    return min(lv[0], 1.0), min(lv[1], 1.0)


def select_skill(current: float, levels, frame: int, has_rival: bool, my_rank: int, human_rank: int):
    """Every 60 frames: the 'human ahead' skill when there is no rival or the human leads this AI,
    else the 'AI ahead' skill."""
    if frame % SKILL_PERIOD:
        return current
    ahead_skill, behind_skill = levels
    if not has_rival or human_rank < my_rank:
        return behind_skill
    return ahead_skill


def results(race: Race):
    """End of race: unfinished racers get an estimated time (race modes) and the order is by time
    (race), score (showoff) or index (lesson)."""
    rows = []
    for r in race.racers:
        if race.mode in SCORE_MODES:
            key = r.score
        elif race.mode == EventMode.LESSON:
            key = -r.index
        else:
            key = -estimated_finish_cs(r, race)
        rows.append((key, r.index))
    rows.sort(reverse=True)
    return [i for _, i in rows]


RESPAWN_DROP = 101.46                 # cm above the path
RESPAWN_SPEED = 833.3333              # cm/s along the horizontal tangent
RESPAWN_CROWD_LATERAL = 199.0         # cm: another racer this close sideways -> step 200 cm aside
RESPAWN_STEP = 200.0
RESPAWN_FALLBACK_AHEAD = 500.0
RESPAWN_RUBBER_MIN_LEFT = 10000.0     # cm to go: below this no advance
RESPAWN_RUBBER_FACTOR = 0.8003195


def respawn_advance(r: RacerRace, race: Race, adjacent_speed: float, others_dist_to_go=None) -> float:
    """Distance added to the path parameter at respawn. Race modes (single, circuit, timed): humans
    are advanced by 0.8 x the speed of the racer one rank behind (one ahead when last) when more than
    100 m remain; AI riders get 0. Knockdown: the mean distance to go of the other racers (clamped
    >= 0, as the code passes it)."""
    if race.mode == EventMode.PRACTICE_KNOCKDOWN and others_dist_to_go:
        return max(0.0, float(np.mean(others_dist_to_go)))
    if len(race.racers) < 2 or r.progress.dist_to_go <= RESPAWN_RUBBER_MIN_LEFT or race.track_id == 8:
        return 0.0
    if race.mode in (EventMode.PRACTICE_RACE, EventMode.CIRCUIT_RACE, EventMode.PRACTICE_TIMEDRACE):
        return 0.0 if not r.is_human else adjacent_speed * RESPAWN_RUBBER_FACTOR
    return 0.0


GRUDGE_ANGER_PER_HIT = 30             # anger added to an AI per knock-down by a human
GRUDGE_LEVEL_STEP = 15                # rivalry level added when a grudge starts
GRUDGE_KIND2_LEVEL = 80.98926         # level >= this: grudge kind 2 (always attacks)
GRUDGE_KIND1_LEVEL = 60.0             # level >= this: kind 1 (attacks until cooled)
GRUDGE_COOL_FRAMES = 360


@dataclass
class Rivalry:
    """Per (AI, other racer) record; bytes in cBoarder's rival table."""
    level: int = 0
    level_max: int = 100
    anger_limit: int = 30
    anger: int = 0
    friend: bool = False
    hits: int = 0
    score: int = 0


@dataclass
class Grudge:
    active: bool = False
    frame: int = 0
    rival: int = -1
    kind: int = 0
    cooled: bool = False


def on_knockdown(victim_grudge: Grudge, victim_rivals, attacker_grudge: Grudge, attacker_rivals,
                 victim_index: int, attacker_index: int, victim_is_ai: bool, attacker_is_ai: bool,
                 frame: int):
    rec = victim_rivals[attacker_index]
    rec.hits += 1
    if attacker_is_ai and not victim_is_ai:
        if attacker_grudge.active and attacker_grudge.rival == victim_index:
            grudge_relieve(attacker_grudge, attacker_rivals, 2)
        attacker_rivals[victim_index].score += 2
    elif victim_is_ai and not attacker_is_ai:
        rec.anger = min(rec.anger + GRUDGE_ANGER_PER_HIT, rec.anger_limit)
        if rec.anger == rec.anger_limit:
            start_grudge(victim_grudge, victim_rivals, attacker_index, frame)


def start_grudge(g: Grudge, rivals, target: int, frame: int):
    if g.active and g.rival != target:
        grudge_relieve(g, rivals, 0)
    g.active, g.cooled, g.frame, g.rival = True, False, frame, target
    rec = rivals[target]
    if rec.level >= GRUDGE_KIND2_LEVEL:
        g.kind, rec.score = 2, rec.score + 3
    elif rec.level >= GRUDGE_KIND1_LEVEL:
        g.kind, rec.score = 1, rec.score + 2
    else:
        g.kind, rec.score = 0, rec.score + 1
    if rec.friend:
        g.kind = 0
    rec.level = min(rec.level + GRUDGE_LEVEL_STEP, rec.level_max)
    rec.anger = 0


def grudge_relieve(g: Grudge, rivals, how: int):
    """how 0: cooled (-15); 1: -45; 2: satisfied (-max(45, level/2)). Level never below 0."""
    if not g.active:
        return
    rec = rivals[g.rival]
    if how == 0:
        g.cooled = True
        rec.level -= 15
    elif how == 1:
        rec.level -= 45
    elif how == 2:
        rec.level -= max(45, int(rec.level / 2))
    rec.level = max(rec.level, 0)


def grudge_cooldown(g: Grudge, rivals, frame: int):
    if g.active and not g.cooled and frame - g.frame >= GRUDGE_COOL_FRAMES:
        grudge_relieve(g, rivals, 0)


def grudge_wants_attack(g: Grudge, other: int, mode: EventMode) -> bool:
    if mode == EventMode.PRACTICE_KNOCKDOWN:
        return True
    if not g.active or g.rival != other:
        return False
    return g.kind == 2 or (g.kind == 1 and not g.cooled)


def boost_pad_velocity(vel, direction, target_speed, gain):
    """Boost-type node acting on a rider inside it: raise the velocity component along `direction`
    toward `target_speed` (cm/s) with a first-order gain (1/s)."""
    along = float(np.dot(vel, direction))
    short = target_speed - along
    if short > 0.0:
        return vel + direction * (short * gain * FRAME_DT)
    return vel


def lap_boost_velocity(vel, direction, target_speed, gain):
    """Lap boost: damp the horizontal velocity (x0.92), then boost along `direction`."""
    v = vel.copy()
    v[0] *= 0.92
    v[1] *= 0.92
    return boost_pad_velocity(v, direction, target_speed, gain)


def burst_effect(burst_timer: float, seconds: float) -> float:
    """Effect 17: boost_burst_timer = max(current, seconds) (forces the top speed tier)."""
    return max(burst_timer, seconds)


def launch_boost_effect(launch_timer: float, seconds: float) -> float:
    """Effect 18: launch_boost_timer = max(current, seconds) and its flag set."""
    return max(launch_timer, seconds)


COUNTDOWN_START = -300.0              # race clock at countdown enter (clock units)
COUNTDOWN_RATE = 2.0                  # added per frame; GO (phase RACE) when the clock reaches 0
COUNTDOWN_CUE_PERIOD = 60             # clock units between countdown audio cues
START_SLOTS = 6
START_SIZE_SPACING = 90.0             # cm per unit of rider size (profile+0x60) along the start tangent


def gate_start_mode(mode: EventMode, track_id: int) -> bool:
    """Racers use the start gate (GateAnticipate) in these modes; otherwise they sit and wait."""
    if track_id == 7:
        return False
    return mode in (EventMode.PRACTICE_KNOCKDOWN, EventMode.PRACTICE_RACE, EventMode.PRACTICE_SHOWOFF,
                    EventMode.CIRCUIT_RACE, EventMode.CIRCUIT_SHOWOFF, EventMode.PRACTICE_TIMEDRACE,
                    EventMode.TEST)


def start_slot(racer_index: int, racer_count: int) -> int:
    return racer_index + (START_SLOTS - racer_count) // 2


def start_pose(track: Track, racer_index: int, racer_count: int, rider_size: float):
    """Reset placement: the slot's AI line start moved (size * 90 - 90) cm along the line's start
    tangent; heading = tangent heading; velocity 0; motion Static, control Void."""
    path = track.ai_paths[track.start_paths[start_slot(racer_index, racer_count)]]
    t = path.tangent_at(0.0)
    return path.start + t * (rider_size * START_SIZE_SPACING - START_SIZE_SPACING), math.atan2(t[1], t[0]), path


@dataclass
class Countdown:
    clock: float = COUNTDOWN_START

    def enter(self, controls, gate_mode: bool):
        """Every racer -> GateAnticipate (8) with gates, else SitAndWait (17)."""
        self.clock = COUNTDOWN_START
        return [8 if gate_mode else 17 for _ in controls]

    def step(self) -> bool:
        """Returns True on GO (switch to phase RACE)."""
        self.clock += COUNTDOWN_RATE
        return self.clock >= 0.0


def reset_allowed(ai_path: Path, path_param: float) -> bool:
    """No forced reset while the race-line parameter is inside an AI no-reset zone (file type 104)."""
    return not any(e.type == 29 and e.start <= path_param <= e.end for e in ai_path.events)


RESPAWN_CROWD_RANGE = 1000.0          # cm: racers this close to the respawn point are avoided
RESPAWN_MAX_SHIFT = 800.0             # cm: larger sideways shifts fall back to a point 500 cm ahead
RESPAWN_ALIGN_COS = 0.866             # adopt the nearest racer's heading when within 30 deg of the tangent


def respawn_point(path: Path, path_param: float, others, sphere_free=lambda p: True):
    """Respawn placement: the race-line point and horizontal tangent at path_param. With other racers
    within 10 m, repeatedly step 200 cm sideways (relative to each nearby racer's own side axis) away
    from any racer closer than 199 cm sideways, then accept the shifted point if the world sphere test
    is free and the total shift is <= 800 cm; otherwise return None (caller re-picks 500 cm ahead).
    Returns (point, heading, velocity)."""
    p = path.point_at(path_param)
    t = path.tangent_at(path_param)
    raw = path.dirs[0] if len(path.lens) else np.array([1.0, 0.0, 0.0])
    rest = np.float32(path_param)
    for seg in range(len(path.lens)):
        raw = path.dirs[seg]
        if seg == len(path.lens) - 1 or rest <= np.float32(path.lens[seg]):
            break
        rest = np.float32(rest - np.float32(path.lens[seg]))
    heading = math.atan2(t[1], t[0])
    near = [(np.asarray(op), np.asarray(ov)) for op, ov in others
            if np.linalg.norm(np.asarray(op) - p) < RESPAWN_CROWD_RANGE]
    if near:
        frames = []
        for op, ov in near:
            sp = np.linalg.norm(ov)
            d = ov / sp if sp > 0.0 else t
            side = np.cross(np.array([0.0, 0.0, 1.0]), d)
            side = side / max(np.linalg.norm(side), 1e-9)
            frames.append([op, d, side, False])
        shift = np.zeros(3)
        moved, last = False, -1
        direction = 1
        while True:
            pick, best = -1, RESPAWN_STEP
            for k, (op, d, side, done) in enumerate(frames):
                if done:
                    continue
                lat = float(np.dot(p + shift - op, side))
                if abs(lat) < RESPAWN_CROWD_LATERAL and abs(lat) < best:
                    pick, best, cur_lat = k, abs(lat), lat
            if pick < 0:
                break
            op, d, side, _ = frames[pick]
            direction = 1 if cur_lat >= 0.0 else -1
            shift = shift + side * (direction * RESPAWN_STEP - cur_lat)
            frames[pick][3] = True
            moved, last = True, pick
        if moved:
            if np.linalg.norm(shift) <= RESPAWN_MAX_SHIFT and sphere_free(p + shift):
                d = frames[last][1]
                if float(np.dot(d, t)) > RESPAWN_ALIGN_COS:
                    t, heading = d, math.atan2(d[1], d[0])
                    raw = None
                p = p + shift
            else:
                return None
    # 0x119220 -> 0x197738: the velocity uses the segment's RAW record (unit horizontal dir, z = slope), not the
    # normalised tangent (live replay_rep0001 racer 1 f569: |v| = 833.33 on a steep segment); float segment walk
    if raw is not None:
        vel = np.array([np.float32(raw[0]) * np.float32(RESPAWN_SPEED), np.float32(raw[1]) * np.float32(RESPAWN_SPEED), 0.0])
    else:
        vel = np.array([t[0], t[1], 0.0]) * RESPAWN_SPEED
    return p + np.array([0.0, 0.0, 0.0]), heading, vel


WORLD_FLOOR_MARGIN = 10000.0          # forced reset below (.ltg bbox min z) - 10000


def kill_height(ltg_world_min_z: float) -> float:
    return ltg_world_min_z - WORLD_FLOOR_MARGIN
