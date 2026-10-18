"""Wipe-out entries of the live REP0001 crash trace -> reference rows for SSX.WipeOut.LiveEntry.
Usage:  python tools/make_wipeout_entry_reference.py
Reads   SSX_TRACES/live/replay_rep0001_crash (PCSX2 recording --crash: the original replaying the
        user's replay REP0001 on Snowdream; per rider per sample the cBoarder blocks +0x0..+0x600, +0x4600..+0x4B00,
        +0x57B0..+0x5800, the crash block +0x600..+0xB58 (sphere set +0x470.., pose object +0x7C0..) and the wipe-out
"""
import libpath  # noqa: F401,E402  (tools/lib on sys.path)
import traces  # noqa: E402

traces.require()
TR = traces.root()

import json
import struct
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
TRACE = TR / "live" / "replay_rep0001_crash"
OUT = ROOT / "Unreal" / "SSXTrickyRebuild" / "SimData" / "snow.wipeout_entry.csv"
HUMAN = 4
BEFORE, AFTER = 4, 40


def main():
    meta = json.loads((TRACE / "meta.json").read_text())
    raw = (TRACE / "frames.bin").read_bytes()
    size = meta["rider_record_size"]
    riders = meta["riders"]
    rec = 8 + size * len(riders)
    index = {struct.unpack_from("<I", raw, i * rec)[0]: i * rec + 8 for i in range(len(raw) // rec)}
    layout = meta["record_layout"]

    def block(frame, r, base, start, n):
        out = bytearray()
        at = index[frame] + r * size
        for o in range(start, start + n, 4):
            for l in layout:
                if l["base"] == base and l["offset"] <= o < l["offset"] + l["size"]:
                    p = at + l["record_pos"] + o - l["offset"]
                    out += raw[p:p + 4]
                    break
            else:
                raise KeyError(f"{base}+{o:#x}")
        return out.hex()

    def motion(frame, r):
        return struct.unpack_from("<i", raw, index[frame] + r * size + 0x424)[0]

    frames = sorted(index)
    rows = []
    for r, addr in enumerate(riders):
        prof = bytes.fromhex(meta["profiles"][addr])
        pinfo = [str(struct.unpack_from("<i", prof, 0x3C)[0]), str(struct.unpack_from("<i", prof, 0x40)[0]),
                 str(struct.unpack_from("<i", prof, 0x44)[0]), repr(struct.unpack_from("<f", prof, 0x60)[0])]
        for i in range(1, len(frames)):
            if motion(frames[i], r) != 5 or motion(frames[i - 1], r) != 1:
                continue
            j = i
            while j < len(frames) and j < i + AFTER and motion(frames[j], r) in (5, 6):
                j += 1
            case = r * 100000 + frames[i]
            for k in range(max(0, i - BEFORE), min(j + 1, len(frames))):
                f = frames[k]
                rows.append([str(case), str(r), str(f), "1" if r == HUMAN else "0"] + pinfo
                            + [block(f, r, "boarder", 0, 0xB58), block(f, r, "boarder", 0x4600, 0x500),
                               block(f, r, "boarder", 0x57B0, 0x50), block(f, r, "handler", 0x4F0, 0x110)])
    with open(OUT, "w", newline="") as fh:
        fh.write("case,rider,frame,human,char,stance,bclass,scale,boarder,anim,pred,handler\n")
        for row in rows:
            fh.write(",".join(row) + "\n")
    print(f"{len(rows)} rows -> {OUT}")


if __name__ == "__main__":
    main()
