"""Loader for the live race traces (SSX_TRACES/live/<name>/).

frames.bin = per recorded frame: u32 frame, u32 rider_count, then per rider the raw cBoarder blocks
listed in meta.json (offset, size). This module maps any cBoarder offset that lies inside a recorded
block to a numpy array over frames, per rider.

Usage (library):  t = RaceTrace('ride_session_1'); x = t.f32(rider, 0x140, 3)  # positions
CLI: python scripts/race_trace.py <name>   prints a per-rider summary.
"""
import json
import struct
import sys
from pathlib import Path

import numpy as np

sys.path.insert(0, str(Path(__file__).resolve().parent.parent))
import traces  # noqa: E402
ROOT = (traces.root() or Path(".")) / "live"
PLAYER_RIDER = 0xDD8CC0


class RaceTrace:
    def __init__(self, name):
        d = ROOT / name
        self.meta = json.loads((d / "meta.json").read_text())
        self.riders = [int(r, 16) for r in self.meta["riders"]]
        self.blocks = [tuple(b) for b in self.meta["blocks"]]
        # --crash / --ai traces append more blocks per rider; the boarder BLOCKS come first
        self.layout = [(e["base"], e["offset"], e["size"], e["record_pos"]) for e in self.meta.get("record_layout", [])]
        self.rider_bytes = self.meta.get("rider_record_size", sum(s for _, s in self.blocks))
        raw = (d / "frames.bin").read_bytes()
        n = len(self.riders)
        rec = 8 + n * self.rider_bytes
        self.count = len(raw) // rec
        buf = np.frombuffer(raw[:self.count * rec], dtype=np.uint8).reshape(self.count, rec)
        self.frame = buf[:, :4].copy().view("<u4")[:, 0].astype(np.int64)
        self.data = buf[:, 8:].reshape(self.count, n, self.rider_bytes)
        self.profiles = {int(k, 16): bytes.fromhex(v) for k, v in self.meta.get("profiles", {}).items()}
        self.ram_path = d / "spawn_ram.bin"

    def _loc(self, off, nbytes):
        pos = 0
        for boff, bsize in self.blocks:
            if boff <= off and off + nbytes <= boff + bsize:
                return pos + off - boff
            pos += bsize
        raise KeyError(f"offset {off:#x} not recorded")

    def handler(self, rider, off, n=1, dtype="<f4"):
        """A field of the rider's handler (*(cBoarder+0x5AE0)) from a --crash / --ai trace."""
        size = np.dtype(dtype).itemsize
        for b, o, s, p in self.layout:
            if b == "handler" and o <= off and off + n * size <= o + s:
                a = self.data[:, self.index(rider), p + off - o:p + off - o + n * size].copy().view(dtype)
                return a[:, 0] if n == 1 else a
        raise KeyError(f"handler offset {off:#x} not recorded")

    def index(self, rider):
        return self.riders.index(rider) if rider > 16 else rider

    def raw(self, rider, off, n, dtype):
        size = np.dtype(dtype).itemsize
        p = self._loc(off, n * size)
        a = self.data[:, self.index(rider), p:p + n * size].copy().view(dtype)
        return a[:, 0] if n == 1 else a

    def f32(self, rider, off, n=1):
        return self.raw(rider, off, n, "<f4").astype(np.float64)

    def i32(self, rider, off, n=1):
        return self.raw(rider, off, n, "<i4").astype(np.int64)

    def u32(self, rider, off, n=1):
        return self.raw(rider, off, n, "<u4").astype(np.int64)

    def is_player(self, rider):
        if PLAYER_RIDER in self.riders:
            return self.riders[self.index(rider)] == PLAYER_RIDER
        return int(self.i32(rider, 0x38)[0]) >= 0     # local player index (-1 for an AI screen fade)

    def spawn_ram(self):
        return self.ram_path.read_bytes()


def main():
    t = RaceTrace(sys.argv[1])
    print(f"{t.count} frames ({t.frame[0]}..{t.frame[-1]}), riders {[hex(r) for r in t.riders]}")
    for i, r in enumerate(t.riders):
        sp = np.linalg.norm(t.f32(i, 0x150, 3), axis=1)
        ts = t.f32(i, 0x12C)
        fin = t.i32(i, 0x418)
        print(f"{r:#x} {'PLAYER' if t.is_player(i) else 'AI    '} speed mean {sp.mean():7.1f} max {sp.max():7.1f} "
              f"time_scale {ts.min():.3f}..{ts.max():.3f} rank_end {t.i32(i, 0x110)[-1]} finish {fin[-1]}")


if __name__ == "__main__":
    main()
