"""Live wipe-outs of the player for the SSX.WipeOut.Live comparison.
Usage:  python tools/make_wipeout_live_reference.py
Reads   SSX_TRACES/live/ride_session_1/summary.csv (player cBoarder 0xdd8cc0, Moby: profile
        scale 0.87, wipe-outs at 1304 / 3810 / 4160 / 5563 / 7051)
Writes  Unreal/SSXTrickyRebuild/SimData/gari.wipeout_live.csv (git-ignored derived data): one row per frame from the
"""
import libpath  # noqa: F401,E402  (tools/lib on sys.path)
import traces  # noqa: E402

traces.require()
TR = traces.root()

import struct
import csv
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
LIVE = TR / "live"
SESSIONS = [(1, "ride_session_1"), (2, "ride_session_2")]
TRACE = LIVE / "ride_session_1" / "summary.csv"
OUT = ROOT / "Unreal" / "SSXTrickyRebuild" / "SimData" / "gari.wipeout_live.csv"
PLAYER = "0xdd8cc0"
FRAMES = TRACE.parent / "frames.bin"
BLOCKS = [(0x0000, 0x600), (0x4600, 0x500), (0x57B0, 0x50)]   # traced cBoarder blocks (offset, size)
REC_RIDER = sum(b[1] for b in BLOCKS)
# column "rec": cBoarder +0x10..+0x460 as hex, the record SSXReplayFile::RestoreBoarderRecord reads (the pose-root inputs
# lean +0x244, smoothed up +0x2C0, +0x308, contact left +0x330, suspension / chatter / spin / flip, all filters)
REC_START, REC_SIZE = 0x10, 0x450
PLAYER_INDEX = 4
RAW = [("rpx", 0x140, "f"), ("rpy", 0x144, "f"), ("rpz", 0x148, "f"), ("rvx", 0x150, "f"), ("rvy", 0x154, "f"),
       ("rvz", 0x158, "f"), ("rqw", 0x170, "f"), ("rqx", 0x174, "f"), ("rqy", 0x178, "f"), ("rqz", 0x17C, "f"),
       ("mx", 0x2F0, "f"), ("my", 0x2F4, "f"), ("mz", 0x2F8, "f"), ("stuck", 0x2E4, "f"), ("nx", 0x2A0, "f"),
       ("ny", 0x2A4, "f"), ("nz", 0x2A8, "f"), ("h", 0x1BC, "f"), ("lx", 0x350, "f"), ("ly", 0x354, "f"),
       ("lz", 0x358, "f"), ("kind", 0x2E0, "i"), ("anim", 0x46C8, "i"), ("rmotion", 0x424, "i"),
       ("ts", 0x12C, "f"), ("ax", 0x360, "f"), ("ay", 0x364, "f"), ("az", 0x368, "f"), ("p7", 0x294, "f"),
       ("rsurf", 0x290, "i"), ("goofy", 0x1B4, "i"), ("ptime", 0x57B8, "f"),
       ("plx", 0x57C0, "f"), ("ply", 0x57C4, "f"), ("plz", 0x57C8, "f"), ("pvx", 0x57D0, "f"), ("pvy", 0x57D4, "f"),
       ("pvz", 0x57D8, "f"), ("pnx", 0x57E0, "f"), ("pny", 0x57E4, "f"), ("pnz", 0x57E8, "f"), ("psurf", 0x57F0, "i"),
       ("pflight", 0x57F4, "f"), ("papex", 0x57F8, "f"), ("pelapsed", 0x57FC, "f")]


RAW_TAIL = [("amirror", 0x46D4, "i"), ("ayaw", 0x46E0, "f"),
            ("crouch", 0x208, "f"), ("brake", 0x1FC, "f"), ("steer", 0x214, "f"), ("sfollow", 0x268, "f"),
            ("airturn", 0x27C, "f"), ("airtilt", 0x280, "f"), ("ramp", 0x1F0, "f"), ("grind", 0x1DC, "i")]


class Frames:
    def __init__(self, path):
        self.raw = path.read_bytes()
        n_riders = struct.unpack_from("<I", self.raw, 4)[0]
        self.rec = 8 + n_riders * REC_RIDER
        self.index = {struct.unpack_from("<I", self.raw, i * self.rec)[0]: i for i in range(len(self.raw) // self.rec)}

    @staticmethod
    def _off(o):
        base = 0
        for start, size in BLOCKS:
            if start <= o < start + size:
                return base + o - start
            base += size
        raise KeyError(hex(o))

    def row(self, frame, rider=PLAYER_INDEX, fields=None):
        if frame not in self.index:
            return None
        base = self.index[frame] * self.rec + 8 + rider * REC_RIDER
        out = []
        for _, o, t in (fields or RAW):
            v = struct.unpack_from("<" + t, self.raw, base + self._off(o))[0]
            out.append(repr(v) if t == "f" else str(v))
        return out

    def block(self, frame, rider, start, size):
        """Raw cBoarder bytes [start, start + size) of one sample (must lie in one traced block)."""
        if frame not in self.index:
            return None
        base = self.index[frame] * self.rec + 8 + rider * REC_RIDER + self._off(start)
        self._off(start + size - 1)
        return self.raw[base:base + size]
KEYS = ["pos_x", "pos_y", "pos_z", "vel_x", "vel_y", "vel_z", "quat_w", "quat_x", "quat_y", "quat_z",
        "motion", "control", "contact_kind", "surface"]


def main():
    """Every wipe-out of every traced rider (player and AI) in both sessions. case = session * 1000000 + rider index *
    100000 + entry frame; profile bytes from meta.json (character +0x3C, +0x40 stance, +0x44 board class, +0x60 scale)."""
    import json
    out = []
    for sess, name in SESSIONS:
        trace = LIVE / name / "summary.csv"
        if not trace.exists():
            print(f"missing {trace}")
            continue
        meta = json.loads((LIVE / name / "meta.json").read_text())
        frames = LIVE / name / "frames.bin"
        fr = Frames(frames) if frames.exists() else None
        with open(trace, newline="") as fh:
            allrows = list(csv.DictReader(fh))
        for ri, rider in enumerate(meta["riders"]):
            prof = bytes.fromhex(meta["profiles"][rider])
            pinfo = [str(struct.unpack_from("<i", prof, 0x3C)[0]), str(struct.unpack_from("<i", prof, 0x40)[0]), str(struct.unpack_from("<i", prof, 0x44)[0]),
                     repr(struct.unpack_from("<f", prof, 0x60)[0]), "1" if rider == PLAYER else "0"]
            rs = sorted((r for r in allrows if r["rider"] == rider), key=lambda r: int(r["frame"]))
            for i in range(1, len(rs)):
                if rs[i]["motion"] == "5" and rs[i - 1]["motion"] != "5":
                    j = i
                    while j < len(rs) and rs[j]["motion"] in ("5", "6"):
                        j += 1
                    case = sess * 1000000 + ri * 100000 + int(rs[i]["frame"])
                    for k in range(max(0, i - 2), min(j + 1, len(rs))):
                        extra = fr.row(int(rs[k]["frame"]), ri) if fr else None
                        tail = fr.row(int(rs[k]["frame"]), ri, RAW_TAIL) if fr else None
                        rec = fr.block(int(rs[k]["frame"]), ri, REC_START, REC_SIZE) if fr else None
                        out.append([str(case), rs[k]["frame"]] + [rs[k][c] for c in KEYS] + (extra or [""] * len(RAW)) + pinfo
                                   + (tail or [""] * len(RAW_TAIL)) + [rec.hex() if rec else ""])
    with open(OUT, "w", newline="") as fh:
        w = csv.writer(fh)
        w.writerow(["case", "frame", "px", "py", "pz", "vx", "vy", "vz", "qw", "qx", "qy", "qz",
                    "motion", "control", "contact", "surface"] + [n for n, _, _ in RAW]
                   + ["char", "stance", "bclass", "scale", "human"] + [n for n, _, _ in RAW_TAIL] + ["rec"])
        w.writerows(out)
    print(f"{len(out)} rows -> {OUT}")


if __name__ == "__main__":
    main()
