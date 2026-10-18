"""SSX Tricky `.afl` animation banks: header, per-animation length, flag markers and events.

Format, little endian:

    u16 magic 0x114C ("L\\x11"), u16 n_records, u32 off_tracks, u32 off_data
    n_records x 36-byte records starting at 0x0C:
      animation record:  u32 name_hash (ELF-style hash, shift 23), u32 0,
                         u8 0xFF, u8 n_sub, u16 n_frames,
                         u16 flag_marker[4]  (0xFFFF = none)   -> anim flags 1..4
                         u16 event_frame[4]  (0xFFFF = none)
                         u16 event_id[4]
                         u32 0
      followed by n_sub sub-records (u32 0, u32 key offset into off_tracks, u16 bone/channel, u16 n_frames,
      same 24 trailing bytes, all 0xFFFF / 0); the sub-records carry the compressed curves.
    off_tracks: u32 table, off_data: packed key data (not decoded, not needed for timing).

Runtime semantics (animation player 0x15A030):
    frame += speed * 30 / 60  each game frame   (authored at 30 fps; NOT scaled by the AI time_scale)
    flag k (k = 1..4) is set on a frame iff flag_marker[k-1] < frame (after the advance); the byte is
    cleared and rebuilt every update, so it is a level, not a latch.
    "done" (0x15FF40) iff frame >= n_frames - 1.
    hold frame (+0x60): loop mode 1 (grabs, tweaks) = flag_marker[1]; mode 2 = n_frames - 1. While a
    hold frame is set, frame is clamped to it after the flags are computed.

Animation ids (0x1F0..0x2FF, channel 0) are mapped to names by 0x15ADA8 via the property table at .data
0x325FA8 (28 bytes per id) and lookup tables that 0x15AEC0 fills at boot (BSS, built by running 0x15AEC0 on the exe).

Usage:
    python scripts/afl.py info <file.afl>              list records
    python scripts/afl.py id 0x23E [board_class] [character]   resolve an animation id
    python scripts/afl.py table                         timing table of the gameplay-gated animations
"""
from __future__ import annotations

import glob
import math
import os
import struct
import sys
from dataclasses import dataclass, field

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))     # tools/lib
REPO = os.path.dirname(os.path.dirname(ROOT))
EXE = os.path.join(REPO, "extracted", "SLUS_203.26")
AFL_DIR = os.path.join(REPO, "unpacked", "CHAR", "ANM", "data", "char")

MAGIC = 0x114C
REC_SIZE = 0x24
NONE16 = 0xFFFF
ANIM_FPS = 30.0          # authored rate: the player advances 30/60 frames per game frame at speed 1
GAME_HZ = 60.0

# exe tables (.data)
NAME_TABLE = 0x3242A0    # char* [1858]
PROP_TABLE = 0x325FA8    # 28 bytes per animation id
GRAB_TABLE = 0x333098   # 3 board classes x 12 characters x 15 x {grab id, grab, tweak, uber}
NAME_NONE = 0x585       # 1413 = "no animation for this style" (0x15F060 returns 0x300)
# BSS tables filled by 0x15AEC0
TAB_STYLE = 0x3C0BD0     # type 0: u32 [id][3 board classes]
TAB_CHAR = 0x3C2FC8      # type 1: u32 [id][12 characters]
TAB_GLOBAL = 0x3BFFD0    # type 2: u32 [id]
TAB_RANDOM_PK = 0x3BF180  # type 3: u32 [id] = count << 16 | first
TAB_RANDOM = 0x3BFD80    # type 3: u32 [variant][3 board classes]


def elf23(name: str) -> int:
    """Name hash used in .afl records (ELF/PJW hash with a 23-bit fold-back instead of 24)."""
    h = 0
    for c in name.encode("latin1"):
        h = ((h << 4) + c) & 0xFFFFFFFF
        g = h & 0xF0000000
        if g:
            h ^= g >> 23
        h &= ~g & 0xFFFFFFFF
    return h


@dataclass
class Anim:
    file: str
    index: int               # record index (the runtime binding stores this)
    name_hash: int
    n_frames: int
    n_sub: int
    markers: tuple           # flag markers 1..4 (None = absent)
    events: list = field(default_factory=list)   # [(frame, event_id)]
    name: str = "?"

    def seconds(self, speed: float = 1.0) -> float:
        """Time from start to 'done' (frame >= n_frames - 1)."""
        return (self.n_frames - 1) / (ANIM_FPS * speed)

    def game_frames(self, speed: float = 1.0, start: float = 0.0) -> int:
        """Game frames until 'done' when started at `start` (first advance on the first frame)."""
        return max(0, math.ceil((self.n_frames - 1 - start) / (0.5 * speed) - 1e-9))

    def flag_frame(self, k: int, speed: float = 1.0, start: float = 0.0):
        """Game frames after the start until flag k (1..4) is first set: marker < frame."""
        m = self.markers[k - 1]
        if m is None:
            return None
        n = math.floor((m - start) / (0.5 * speed)) + 1
        return max(1, n)


def read_afl(path: str) -> list:
    d = open(path, "rb").read()
    magic, n, off_tracks, off_data = struct.unpack_from("<HHII", d, 0)
    if magic != MAGIC:
        raise ValueError(f"{path}: bad magic {magic:#x}")
    out = []
    for i in range(n):
        o = 0x0C + i * REC_SIZE
        h, zero, b0, n_sub, nf = struct.unpack_from("<IIBBH", d, o)
        if zero != 0 or h == 0:
            continue                       # sub-record (curve block) of the previous animation
        mk = struct.unpack_from("<8H", d, o + 12)
        ev_id = struct.unpack_from("<4H", d, o + 28)
        markers = tuple(None if m == NONE16 else m for m in mk[:4])
        events = [(mk[4 + j], ev_id[j]) for j in range(4) if mk[4 + j] != NONE16]
        out.append(Anim(os.path.basename(path), i, h, nf, n_sub, markers, events))
    return out


class Bank:
    """All .afl files indexed by name hash, plus the exe/RAM tables that map animation ids to names."""

    def __init__(self, afl_dir=AFL_DIR, exe=EXE, ram=None):
        """ram: an EE memory image (bytes) or a RAM dump path; default = the boot-time tables built by running
        0x15AEC0 on the executable (exe_tables.anim_boot_image)."""
        self.exe = open(exe, "rb").read()
        if ram is None:
            if ROOT not in sys.path:
                sys.path.insert(0, ROOT)
            import exe_tables
            self.ram = exe_tables.anim_boot_image()
        else:
            self.ram = ram if isinstance(ram, (bytes, bytearray)) else open(ram, "rb").read()
        self.names = self._name_table()
        self.by_hash = {}
        for f in sorted(glob.glob(os.path.join(afl_dir, "*.afl"))):
            for a in read_afl(f):
                self.by_hash.setdefault(a.name_hash, []).append(a)
        for n in self.names:
            for a in self.by_hash.get(elf23(n), []):
                a.name = n

    def _x32(self, va):
        return struct.unpack_from("<I", self.exe, va - 0x100000 + 0x1000)[0]

    def _r32(self, va):
        if self.ram is None:
            raise RuntimeError("BSS animation tables need a memory image")
        return struct.unpack_from("<I", self.ram, va)[0]

    def _cstr(self, va):
        o = va - 0x100000 + 0x1000
        return self.exe[o:self.exe.index(b"\0", o)].decode("latin1")

    def _name_table(self):
        out, va = [], NAME_TABLE
        while va < PROP_TABLE:
            out.append(self._cstr(self._x32(va)))
            va += 4
        return out

    def props(self, anim_id: int):
        """(category, lookup type, loop mode, priority, channel, blend, flags) from .data 0x325FA8.

        Field 1 (lookup type) is rewritten at boot (the exe image holds 4 everywhere), so the boot image is
        preferred when present."""
        if self.ram is not None:
            return struct.unpack_from("<7i", self.ram, PROP_TABLE + anim_id * 28)
        o = PROP_TABLE - 0x100000 + 0x1000 + anim_id * 28
        return struct.unpack_from("<7i", self.exe, o)

    # id -> names (0x15ADA8)
    def name_indices(self, anim_id: int, board_class: int = 0, character: int = 0) -> list:
        typ = self.props(anim_id)[1]
        if typ == 0:
            return [self._r32(TAB_STYLE + anim_id * 12 + board_class * 4)]
        if typ == 1:
            return [self._r32(TAB_CHAR + anim_id * 48 + character * 4)] if character < 12 else [NAME_NONE]
        if typ == 3:                                   # random variant per play
            pk = self._r32(TAB_RANDOM_PK + anim_id * 4)
            first, count = pk & 0xFFFF, pk >> 16
            return [self._r32(TAB_RANDOM + board_class * 4 + (first + r) * 12) for r in range(count)]
        return [self._r32(TAB_GLOBAL + anim_id * 4)]

    def grab_table(self, board_class: int, character: int) -> dict:
        """Per-rider grab table (.data GRAB_TABLE, static in the exe): grab id -> (grab, tweak, uber) ids.

        Indexed by board class (profile+0x44) and trick-set character (profile+0x5C); 15 entries of
        {grab id, grab anim, tweaked anim, uber anim or 0x300 = none}."""
        out = {}
        base = GRAB_TABLE + board_class * 0xF0 + character * 0x2D0
        for i in range(15):
            gid, g, t, u = struct.unpack_from("<4i", self.exe, base + 16 * i - 0x100000 + 0x1000)
            out[gid] = tuple(None if a == 0x300 else a for a in (g, t, u))
        return out

    def resolve(self, anim_id: int, board_class: int = 0, character: int = 0) -> list:
        """Animation records for an id (several when the game picks a random variant)."""
        out = []
        for ni in self.name_indices(anim_id, board_class, character):
            if ni == NAME_NONE or ni >= len(self.names):
                continue
            recs = self.by_hash.get(elf23(self.names[ni]), [])
            if recs:
                out.append(recs[0])
        return out


GATED = [  # (id range, what gates on it)
    ((0x1FE, 0x1FE), "GetupFromSit sit-up"),
    ((0x203, 0x204), "turn-around (ChangeFakie)"),
    ((0x221, 0x221), "push / stand (2GO)"),
    ((0x223, 0x225), "air idle / fall recover / auto-180"),
    ((0x23E, 0x243), "LandHard"),
    ((0x244, 0x249), "Bump"),
    ((0x24A, 0x253), "get-up (WipeOutRecover)"),
]


def _fmt_markers(m):
    return "[" + ",".join("-" if x is None else str(x) for x in m) + "]"


def main(argv):
    if len(argv) < 2:
        print(__doc__)
        return
    cmd = argv[1]
    if cmd == "info":
        for a in read_afl(argv[2]):
            print(a.index, hex(a.name_hash), a.n_frames, _fmt_markers(a.markers), a.events)
        return
    bank = Bank()
    if cmd == "id":
        aid = int(argv[2], 0)
        bc = int(argv[3]) if len(argv) > 3 else 0
        ch = int(argv[4]) if len(argv) > 4 else 0
        print(hex(aid), "props", bank.props(aid))
        for a in bank.resolve(aid, bc, ch):
            print(f"  {a.name:28s} {a.file:14s} rec {a.index:4d} frames {a.n_frames:3d} "
                  f"markers {_fmt_markers(a.markers)} events {a.events} done {a.seconds():.3f}s")
        return
    if cmd == "table":
        print("id     class style name                          frames  done@1.0  markers")
        for (lo, hi), what in GATED:
            print(f"-- {what}")
            for aid in range(lo, hi + 1):
                for bc in range(3):
                    for a in bank.resolve(aid, bc):
                        print(f"{aid:#05x}  {bank.props(aid)[0]:3d}  {bc}  {a.name:30s} {a.n_frames:4d} "
                              f"{a.seconds():7.3f}s  {_fmt_markers(a.markers)}")
        return
    print(__doc__)


if __name__ == "__main__":
    main(sys.argv)
