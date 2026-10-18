"""Career progression reference model.

Covers the World Circuit medal rules, the unlocks a medal (or trick-book progress) grants, the
stat-point distribution screen, the record (high score) tables, the new-save defaults and the
save-blob checksum.  Validated by a validator against the exe data and the
RAM snapshots (traces/live/*/spawn_ram.bin).

Track ids: 0 Garibaldi, 1 Snowdream, 2 Elysium, 3 Mesablanca, 4 Merqury, 5 Aloha, 6 Pipedream,
7 Untracked, 8 Tokyo, 9 (unused), 10 tutorial, 11 Alaska, 12 (unused).
Medal codes: 0 gold, 1 silver, 2 bronze, 3 none.  Game modes: 2 SE race, 3 SE showoff,
4 WC race, 5 WC showoff, 7 time challenge.

Usage:
    s = new_save()
    rec = s.careers[4]                               # Moby
    unlocks = award_event(s, char=4, mode=4, venue=0, heat=2, place=0)   # WC race final, 1st
    pd = PointSession.open(rec, char=4)              # PointDistribution overlay
    pd.add(1); pd.remove(1)
    insert_record(s.records[0]['race'], 20323, 'JOE', 4)
"""
from dataclasses import dataclass, field
from typing import Dict, List, Optional, Tuple
import numpy as np

from models.rider_profile import RIDERS, BOARDS, attribute_base

f32 = np.float32

GOLD, SILVER, BRONZE, NO_MEDAL = 0, 1, 2, 3
MEDAL_RANK_POINTS = (15, 10, 5, 0)          # rank points per medal code
TRACKS = 13
UNTRACKED, PIPEDREAM, TUTORIAL_TRACK = 7, 6, 10

# Circuit order (race and showoff use the same venue order).  Showoff thresholds are the
# minimum score for gold / silver / bronze.
CIRCUIT_VENUES = (0, 1, 2, 3, 4, 8, 5, 11)
SHOWOFF_THRESHOLDS = {0: (55000, 40000, 25000), 1: (95000, 65000, 35000),
                      2: (225000, 150000, 75000), 3: (225000, 150000, 75000),
                      4: (275000, 175000, 125000), 8: (350000, 225000, 100000),
                      5: (175000, 115000, 75000), 11: (500000, 300000, 150000)}
PIPEDREAM_THRESHOLDS = (800000, 500000, 250000)     # hard-coded for track 6 (SE showoff only)

RIDER_UNLOCK_ORDER = (3, 4, 7, 0, 10, 5, 6, 1, 11, 8, 9, 2)   # Mac Moby Elise Eddie, then Brodi..Luther
NEW_SAVE_RIDERS = 4                          # first four of the order are open on a new save
NEW_SAVE_VENUE_MASK = (1 << 0) | (1 << 1) | (1 << 2) | (1 << TUTORIAL_TRACK)

# Rank: index 0 Master .. 9 Newbie; RANK_THRESHOLDS[i] = rank points needed for rank i.
RANK_THRESHOLDS = (240, 200, 160, 120, 90, 60, 35, 20, 5, 0)
UBER_BOARD = 11
CHROME_OUTFIT = 6
TRICK_BOOK_CHAPTERS = 6
TRICKS_PER_CHAPTER = 5

# Unlock-list entry types (cUnlockedOverlay strings).
U_SHOWOFF_VENUE, U_RACE_VENUE, U_COURSE, U_RIDER, U_BOARD, U_OUTFIT, U_RANK, U_MEDAL, \
    U_BOOK_DONE, U_BOOK_ENTRIES = range(10)

POINT_SCALE = f32(0.4)                       # raw (save) points -> displayed points


def scaled(v) -> int:
    """int(v * 0.4 + 0.5) in single precision (displayed points / bar maxima)."""
    return int(f32(v) * POINT_SCALE + f32(0.5))


def rank_of(rank_points: int) -> int:
    for i, t in enumerate(RANK_THRESHOLDS):
        if rank_points >= t:
            return i
    return 9


def board_costs(char: int) -> Tuple[int, ...]:
    return tuple(b[2] for b in BOARDS[char])


def stat_maximum(char: int) -> Tuple[int, ...]:
    """Raw attribute cap per attribute (static rider record alt[4])."""
    return tuple(RIDERS[char][2])


def bar_maximum(char: int) -> Tuple[int, ...]:
    """Displayed stat-bar maximum per attribute (before the board bonus)."""
    return tuple(scaled(a) for a in stat_maximum(char))


@dataclass
class TrackState:
    showoff_medal: int = NO_MEDAL
    race_medal: int = NO_MEDAL
    race_open: int = 0
    showoff_open: int = 0


@dataclass
class Career:
    """Per-rider career record (0x1C4 bytes; layout in)."""
    char: int
    raw_points: int = 0                      # +0x00 unspent stat points (raw scale)
    race_rank_points: int = 0                # +0x04
    showoff_rank_points: int = 0             # +0x08
    attr: List[int] = field(default_factory=list)      # +0x0C raw attribute (base .. alt)
    points: List[int] = field(default_factory=list)    # +0x10 displayed attribute points
    book_char: int = 0                       # +0x14
    book_done_a: int = 0                     # +0x18 trick done bits (bit = chapter*5 + trick)
    book_done_b: int = 0                     # +0x1C trick done bits
    book_complete: int = 0                   # +0x20 set once chapter 6 is done
    tracks: List[TrackState] = field(default_factory=list)  # +0x24
    boards: int = 0                          # +0x58
    outfits: int = 1                         # +0x5C
    best_time: List[int] = field(default_factory=lambda: [-1] * TRACKS)    # +0xFC (WC race)
    best_score: List[int] = field(default_factory=lambda: [-1] * TRACKS)   # +0x130 (WC showoff)
    opponents: List[List[int]] = field(default_factory=list)  # +0x164 {rank bonus, second word}

    @property
    def rank_points(self) -> int:
        return self.race_rank_points + self.showoff_rank_points

    def trick_done(self, k: int) -> bool:
        return bool(((self.book_done_b | self.book_done_a) >> k) & 1)

    def chapter_done(self, ch: int) -> bool:
        return all(self.trick_done(ch * TRICKS_PER_CHAPTER + t) for t in range(TRICKS_PER_CHAPTER))


def new_career(char: int, opponent_seed: Optional[List[int]] = None) -> Career:
    """New-save record.  opponent_seed = the 12 second words (from a loaded table; TODO)."""
    base = attribute_base(RIDERS[char][1])
    c = Career(char=char, attr=list(base), points=[scaled(b) for b in base], book_char=char)
    c.tracks = [TrackState() for _ in range(TRACKS)]
    c.tracks[0].race_open = c.tracks[0].showoff_open = 1
    c.boards = sum(1 << b for b, cost in enumerate(board_costs(char)) if cost == 0)
    c.outfits = 1
    c.opponents = [[0, (opponent_seed or [0] * 12)[k]] for k in range(12)]
    return c


@dataclass
class Save:
    venue_mask: int = NEW_SAVE_VENUE_MASK
    rider_mask: int = 0
    careers: List[Career] = field(default_factory=list)
    records: List[Dict[str, 'RecordTable']] = field(default_factory=list)
    race_circuit_beaten: bool = False        # session flags (not saved)
    showoff_circuit_beaten: bool = False
    rank_up: bool = False


def new_save(default_records: Optional[dict] = None) -> Save:
    s = Save()
    s.rider_mask = sum(1 << c for c in RIDER_UNLOCK_ORDER[:NEW_SAVE_RIDERS])
    s.careers = [new_career(c) for c in range(12)]
    s.records = [new_record_tables(t, default_records) for t in range(TRACKS)]
    return s


def venue_unlocked(s: Save, mode: int, venue: int, char: Optional[int], mode_mask: int,
                   unlock_all: bool = False) -> bool:
    """VenueSel / SelectNextVenue lock test.  mode_mask = the per-mode mask."""
    if unlock_all:
        return bool((mode_mask >> venue) & 1)
    if mode in (4, 5, 6):
        if venue == TUTORIAL_TRACK:
            return True
        if char is None:
            return False
        t = s.careers[char].tracks[venue]
        return bool(t.race_open if mode == 4 else t.showoff_open)
    return bool(s.venue_mask & (1 << venue) & mode_mask)


def race_medal(place: int) -> int:
    """place = 0-based finishing position of the human in the WC race final."""
    return place if place < 3 else NO_MEDAL


def showoff_medal(score: int, venue: int) -> int:
    th = PIPEDREAM_THRESHOLDS if venue == PIPEDREAM else SHOWOFF_THRESHOLDS.get(venue)
    if th is None:
        return NO_MEDAL
    for m, need in enumerate(th):
        if score >= need:
            return m
    return NO_MEDAL


def attribute_capacity(c: Career) -> int:
    return sum(a - v for a, v in zip(stat_maximum(c.char), c.attr))


def award_event(s: Save, char: int, mode: int, venue: int, heat: int = 2, place: int = 0,
                score: int = 0, player: int = 0) -> List[Tuple[int, int, int]]:
    """End-of-event progression for the (single) World Circuit player.

    Returns the unlock list [(player, type, value), ...] in the game's order.  Trick-book outfit
    unlocks are evaluated for this player too (the game runs them for every human slot)."""
    c = s.careers[char]
    s.race_circuit_beaten = s.showoff_circuit_beaten = False
    s.rank_up = False
    medal, old_rp, new_rp = NO_MEDAL, 0, 0
    if mode == 5 or (mode == 4 and heat == 2):
        t = c.tracks[venue]
        old_medal = t.showoff_medal if mode == 5 else t.race_medal
        medal = showoff_medal(score, venue) if mode == 5 else race_medal(place)
        old_rp = c.rank_points
        if medal < old_medal:
            delta = MEDAL_RANK_POINTS[medal] - MEDAL_RANK_POINTS[old_medal]
            if mode == 5:
                c.showoff_rank_points += delta
                t.showoff_medal = medal
            else:
                c.race_rank_points += delta
                t.race_medal = medal
            c.raw_points += delta
            for o in c.opponents:
                o[0] += delta
            cap = attribute_capacity(c)
            if cap < c.raw_points:
                c.raw_points = cap
            elif all(c.tracks[v].race_medal == GOLD for v in CIRCUIT_VENUES) and \
                    all(c.tracks[v].showoff_medal == GOLD for v in CIRCUIT_VENUES):
                c.raw_points = cap
        new_rp = c.rank_points
    return _unlocks(s, c, mode, venue, old_rp, new_rp, medal, player)


def _unlocks(s, c, mode, venue, old_rp, new_rp, medal, player):
    out = []
    if medal != NO_MEDAL:
        out.append((player, U_MEDAL, medal))
    # trick book: chapters 1..5 -> outfits 1..5; chapter 6 with all five -> book complete
    done = 0
    for ch in range(5):
        if c.chapter_done(ch):
            done += 1
            if not (c.outfits >> (ch + 1)) & 1:
                out.append((player, U_OUTFIT, ch + 1))
    book_done = c.chapter_done(5) and done == 5
    if book_done and not c.book_complete:
        out.append((player, U_BOOK_DONE, c.char))
    # (type 9 "new trick-book entries" = count of this event's new-trick bits; not modelled)
    for ch in range(5):
        if c.chapter_done(ch):
            c.outfits |= 1 << (ch + 1)
    if book_done:
        c.book_complete = 1
        c.boards |= 1 << UBER_BOARD
    if old_rp == new_rp:
        return out
    idx = CIRCUIT_VENUES.index(venue) if venue in CIRCUIT_VENUES else 8
    if idx < 7:
        nxt = CIRCUIT_VENUES[idx + 1]
        if not (s.venue_mask >> nxt) & 1:
            s.venue_mask |= 1 << nxt
            out.append((player, U_COURSE, nxt))
        t = c.tracks[nxt]
        if mode == 4 and not t.race_open:
            t.race_open = 1
            out.append((player, U_RACE_VENUE, nxt))
        elif mode != 4 and not t.showoff_open:
            t.showoff_open = 1
            out.append((player, U_SHOWOFF_VENUE, nxt))
    last = c.tracks[CIRCUIT_VENUES[-1]]
    if last.race_medal < NO_MEDAL and not (s.venue_mask >> UNTRACKED) & 1:
        s.venue_mask |= 1 << UNTRACKED
        out.append((player, U_COURSE, UNTRACKED))
        s.race_circuit_beaten = True
    if last.showoff_medal < NO_MEDAL and not (s.venue_mask >> PIPEDREAM) & 1:
        s.venue_mask |= 1 << PIPEDREAM
        out.append((player, U_COURSE, PIPEDREAM))
        s.showoff_circuit_beaten = True
    if medal == GOLD:
        for r in RIDER_UNLOCK_ORDER:
            if not (s.rider_mask >> r) & 1:
                s.rider_mask |= 1 << r
                out.append((player, U_RIDER, r))
                break
    for b, cost in enumerate(board_costs(c.char)):
        if cost <= new_rp and not (c.boards >> b) & 1:
            c.boards |= 1 << b
            out.append((player, U_BOARD, b))
    # rank-up: the last matching test wins, i.e. the LOWEST crossed threshold is reported
    crossed = -1
    for i, t in enumerate(RANK_THRESHOLDS):
        if new_rp >= t and old_rp < t:
            crossed = i
    if crossed >= 0:
        s.rank_up = True
        out.append((player, U_RANK, crossed))
        if crossed == 0:
            c.outfits |= 1 << CHROME_OUTFIT
    return out


@dataclass
class PointSession:
    """PointDistribution overlay state for one player slot (manager +0x3820.. arrays)."""
    career: Career
    raw_at_open: int
    left: int
    initial: int
    cur: List[int]
    floor: List[int]
    maximum: List[int]

    @classmethod
    def open(cls, c: Career, max_attr_cheat: bool = False) -> 'PointSession':
        left = scaled(c.raw_points)
        mx = list(bar_maximum(c.char))
        cur = list(c.points)
        if max_attr_cheat:
            left, cur = 0, list(mx)
        cap = sum(m - v for m, v in zip(mx, cur))
        initial = min(left, cap)
        return cls(c, c.raw_points, initial, initial, cur, list(cur), mx)

    def _sync(self, a: int):
        c = self.career
        c.raw_points = int(f32(self.left) / f32(self.initial) * f32(self.raw_at_open))
        c.attr[a] = int(f32(self.cur[a]) / f32(self.maximum[a]) * f32(stat_maximum(c.char)[a])) & 0xFF

    def add(self, a: int) -> bool:
        if self.left <= 0 or self.cur[a] >= self.maximum[a]:
            return False
        self.left -= 1
        self.cur[a] += 1
        self.career.points[a] += 1
        self._sync(a)
        return True

    def remove(self, a: int) -> bool:
        if self.cur[a] <= self.floor[a]:
            return False
        self.left += 1
        self.cur[a] -= 1
        self.career.points[a] -= 1
        self._sync(a)
        return True


def fe_stat_rows(c: Career, board_stats, max_attr_cheat: bool = False):
    """Rider/board stat rows: (floor, value, board bonus, shown maximum) per attribute."""
    mx = bar_maximum(c.char)
    rows = []
    for a in range(4):
        bonus = int(f32(board_stats[a]) * f32(0.1))
        value = mx[a] if max_attr_cheat else c.points[a]
        rows.append((c.points[a], value, bonus, mx[a] + bonus))
    return rows


@dataclass
class RecordTable:
    rows: List[List]                         # [name, char, value]
    higher_better: bool                      # +0x68
    is_time: bool                            # +0x6C


def new_record_tables(track: int, defaults: Optional[dict]) -> Dict[str, RecordTable]:
    """defaults = the default record tables ({track: {kind: [[name, value]..]}}).
    The char shown per default row is random (new-save PRNG): stored as None here."""
    d = (defaults or {}).get(str(track), {})
    def mk(kind, n, hb, tm):
        rows = [[nm, None, v] for nm, v in d.get(kind, [['', 0]] * n)]
        return RecordTable(rows, hb, tm)
    return {'race': mk('race', 5, False, True), 'showoff': mk('showoff', 5, True, False),
            'tc': mk('tc', 1, False, True)}


def insert_record(t: RecordTable, value: int, name: str, char: int) -> Optional[int]:
    """Insert above the first row the new value strictly beats; the last row drops off."""
    for i, row in enumerate(t.rows):
        better = value > row[2] if t.higher_better else value < row[2]
        if better:
            t.rows.insert(i, [name, char, value])
            t.rows.pop()
            return i
    return None


def record_best(rec_list: List[int], track: int, value: int, smaller_is_better: bool):
    """WC best time (+0xFC, min; -1 = none) / best showoff score (+0x130, max)."""
    cur = rec_list[track]
    if smaller_is_better:
        if value < cur or cur == -1:
            rec_list[track] = value
    elif cur < value:
        rec_list[track] = value


def _crc_table():
    t = []
    for i in range(256):
        c = i
        for _ in range(8):
            c = (c >> 1) ^ 0xA001 if c & 1 else c >> 1
        t.append(c)
    return t


_CRC = _crc_table()
CRC_INIT = 0xFBEA


def blob_checksum(blob: bytes) -> int:
    """CRC-16 (reflected poly 0xA001, init 0xFBEA, no final xor) over the 0x27D4-byte blob with
    the checksum word (+0x27CC) zeroed and the magic (+0x27D0) in place."""
    crc = CRC_INIT
    for b in blob:
        crc = (crc >> 8) ^ _CRC[(crc ^ b) & 0xFF]
    return crc


if __name__ == "__main__":
    s = new_save()
    print('new save riders %#x venues %#x' % (s.rider_mask, s.venue_mask))
    for v in CIRCUIT_VENUES:
        print(v, award_event(s, char=4, mode=4, venue=v, heat=2, place=0))
    print('rank points', s.careers[4].rank_points, 'raw', s.careers[4].raw_points)
