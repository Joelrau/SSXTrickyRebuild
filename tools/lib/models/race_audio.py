"""Race audio flow reference model.

60 Hz frames.

Modelled:
  * song selection from MUSICMAP.INF and the per-race random advance;
  * the venue intro-music segment queue (INTROMUS.INF .big streams, section 3.2);
  * the per-frame audio state machine and the music intensity control;
  * the countdown beep schedule;
  * line-path triggers -> Pathfinder events, including the three breakdown zones.

The random source is passed in as a callable returning 15-bit values (the sound manager's own
generator, not identified).

Self-test (`python models/race_audio.py`): the MUSICMAP mask for every track section against the
MUSIC.INF song list, the RAM snapshots' sound-manager fields (song mask, song index, duck level,
audio state), and the countdown schedule.
"""
from __future__ import annotations

import os
import re
import sys
from dataclasses import dataclass, field

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
CONFIG = os.path.join(os.path.dirname(os.path.dirname(ROOT)), "extracted", "DATA", "CONFIG")


def read_inf(name: str):
    sections, cur = [], None
    for raw in open(os.path.join(CONFIG, name), encoding="latin-1"):
        line = raw.split("#", 1)[0].strip()
        if not line:
            continue
        m = re.match(r"\[(.*)\]", line)
        if m:
            cur = (m.group(1), [])
            sections.append(cur)
        elif cur is not None:
            if "=" in line:
                k, v = line.split("=", 1)
                cur[1].append((k.strip().upper(), v.strip().strip('"')))
            else:
                cur[1].append((line.upper(), None))
    return sections


def song_list():
    """MUSIC.INF song sections in file order (GLOBAL excluded) = the song index space."""
    return [s for s, _ in read_inf("MUSIC.INF") if s.upper() != "GLOBAL"]


def musicmap_mask(track_name: str):
    """Allowed-song bit mask for a track."""
    songs = song_list()
    for sec, keys in read_inf("MUSICMAP.INF"):
        if sec.lower() in track_name.lower():
            mask, count = 0, 0
            for k, v in keys:
                if k != "SONG" or count > 3:
                    continue
                for i, name in enumerate(songs):
                    if v in name:     # case-sensitive substring of the song title (strstr)
                        mask |= 1 << i
                        count += 1
                        break
            return mask or 1
    return 1


def next_allowed(index: int, mask: int, n_songs: int) -> int:
    while True:
        index += 1
        if index >= n_songs:
            index = 0
        if (mask >> index) & 1:
            return index


def pick_race_song(mask: int, n_songs: int, rand15) -> int:
    """MUSICMAP load sets index -1 then advances once; race load advances rand*21/0x7fff more."""
    index = next_allowed(-1, mask, n_songs)
    steps = ((rand15() & 0x7FFF) * 21) // 0x7FFF
    for _ in range(steps):
        index = next_allowed(index, mask, n_songs)
    return index


SEQUENTIAL_TRACKS = {3, 7, 8, 11}     # Mesablanca, Untracked, Megaplex, front end
END_SEGMENT = 16


def initial_intro_queue(track: int, restart: bool, rand15):
    if track in SEQUENTIAL_TRACKS:
        return []
    if restart:
        return [4 + ((rand15() & 0x7FFF) * 4) // 0x7FFF]
    q = [0, 1 + ((rand15() & 0x7FFF) * 3) // 0x7FFF]
    q.append(4 + ((rand15() & 0x7FFF) * 4) // 0x7FFF)
    q.append(4 + ((rand15() & 0x7FFF) * 4) // 0x7FFF)
    return q


@dataclass
class RaceAudio:
    track: int
    state: int = 2
    duck_mode: int = 4
    duck_level: float = 1.0
    intro_duck: float = 0.75
    queue: list = field(default_factory=list)
    seq_counter: int = 0
    control: int = 80

    def frame(self, rand15, racers: int, player_rank: int, player_boost: float):
        if self.state == 2:
            self.duck_level, self.duck_mode = self.intro_duck, 0
            if len(self.queue) < 4:
                self.state = 3
        elif self.state in (3, 4):
            if len(self.queue) < 2:
                seg = 8 + ((rand15() & 0x7FFF) << 3) // 0x7FFF
                if self.track in SEQUENTIAL_TRACKS:
                    seg = self.seq_counter
                    self.seq_counter += 1
                    if self.seq_counter > 15:
                        self.seq_counter = 8
                self.queue.append(seg)
        elif self.state == 6:
            self.control = intensity(racers, player_rank, player_boost, self.track)

    def intro_end(self):            # pre-race step 0x12 (camera op 0x11 does the same)
        self.queue = [END_SEGMENT]
        self.state = 5

    def go(self):                   # countdown reaches 0
        self.queue = []             # stream 2 stopped
        self.state, self.duck_mode = 6, 4


def intensity(racers: int, rank: int, boost: float, track: int):
    """Pathfinder control value 0..99. None = not updated."""
    if racers < 2:
        if track == 10:
            return None
        return int(min(1.0, max(0.0, boost * 1.5)) * 99.0)
    v = 99 - ((rank - 1) * 99) // (racers - 1)
    return max(0, min(99, v))


def countdown_events():
    """(frame after the countdown starts, event) list; clock starts at -300 and adds 2 per frame."""
    out, clock, frame = [], -300.0, 0
    while True:
        frame += 1
        clock += 2.0
        if clock >= 0.0:
            out.append((frame, "go"))
            return out
        if int(clock) % 60 == 0:
            out.append((frame, "beep zbxsfx#78"))


@dataclass
class MusicZones:
    zone: int = 0           # active breakdown zone 1..3 (0 none)
    exit_zone: int = 0
    in_enter: int = 0
    in_exit: int = 0
    posted: list = field(default_factory=list)
    pending: list = field(default_factory=list)   # (time_left, kind)

    def enter(self, kind: int):
        z = (kind - 2) // 2 + 1
        if kind % 2 == 0:
            self.in_enter = z
            if self.zone == 0:
                self.zone = z
                self.posted.append(2 * z - 1)
        else:
            self.in_exit = z
            if self.zone == z:
                self.exit_zone = self.zone

    def leave(self, kind: int, delay=1000):
        z = (kind - 2) // 2 + 1
        if kind % 2 == 0:
            if self.zone != z:
                return
            self.in_enter = 0
        else:
            if self.exit_zone != z:
                return
            self.in_exit = 0
        self.pending.append([delay, kind])

    def tick(self, dt):
        due = [p for p in self.pending if p[0] - dt <= 0]
        for p in self.pending:
            p[0] -= dt
        self.pending = [p for p in self.pending if p[0] > 0]
        for _, kind in due:
            z = (kind - 2) // 2 + 1
            if kind % 2 == 0:
                if self.in_enter != z and self.exit_zone == 0:
                    self.zone = 0
                    self.posted.append(2 * z)
            else:
                if self.in_exit != z:
                    self.zone = 0
                    self.exit_zone = 0
                    self.posted.append(2 * z)

    def reset(self):              # race restart
        if self.zone > 0:
            self.posted.append(2 * self.zone)
        self.zone = self.exit_zone = 0


def trigger_event(internal_type: int, laps_left: int):
    """Pathfinder event posted when the player crosses a line-path event."""
    if internal_type == 1:
        return 0
    if internal_type == 9:
        return 9
    if internal_type in (10, 18) and laps_left == 0:
        return 10
    return None


def selftest() -> int:
    bad = 0
    songs = song_list()
    print(f"{len(songs)} songs in MUSIC.INF order")
    for sec, _ in read_inf("MUSICMAP.INF"):
        m = musicmap_mask(sec)
        print(f"  {sec:10s} mask {m:#08x} ->", [songs[i] for i in range(len(songs)) if m >> i & 1])
    # RAM cross-check of the sound-manager fields: scripts/r10_validate.py (addresses live there).
    cd = countdown_events()
    ok = [e for e in cd] == [(30, "beep zbxsfx#78"), (60, "beep zbxsfx#78"), (90, "beep zbxsfx#78"),
                             (120, "beep zbxsfx#78"), (150, "go")]
    bad += not ok
    print("countdown", cd, "ok" if ok else "FAIL")
    z = MusicZones()
    z.enter(2); z.leave(2); z.tick(1000)
    ok = z.posted == [1, 2]
    bad += not ok
    print("zone 1 enter/leave ->", z.posted, "ok" if ok else "FAIL")
    print("race_audio: OK" if not bad else f"race_audio: {bad} failure(s)")
    return bad


if __name__ == "__main__":
    sys.exit(1 if selftest() else 0)
