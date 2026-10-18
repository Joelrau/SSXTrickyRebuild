"""Reference data for the SSX.World.Controllers and SSX.World.ShowOffPatches tests (git-ignored SimData output).
Usage: python tools/make_controller_reference.py
"""
import libpath  # noqa: F401,E402  (tools/lib on sys.path)
import math
import struct
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
import controllers as ctl  # noqa: E402
import pbd  # noqa: E402

OUT = ROOT / "Unreal" / "SSXTrickyRebuild" / "SimData"
FRAMES = (1, 30, 60, 90, 120, 600)


def main():
    rows, flips, showoff, traces, books = [], [], [], [], []

    def golden():
        k = [0]

        def r():
            k[0] += 1
            return math.fmod(k[0] * 0.6180339887498949, 1.0)
        return r

    def trace_flip(label, raw):
        w = struct.unpack("<8i", raw)
        f = struct.unpack("<8f", raw)
        for n in (2, 3, 5):
            t = ctl.texflip_from_record(w, f, frame_count=n)
            t.rng = golden()
            t.mode = int(w[7])
            t.__post_init__()
            seq = [t.frame]
            for _ in range(600):
                t.step()
                seq.append(t.frame)
            traces.append(",".join([label, raw.hex(), str(n), ".".join(map(str, seq))]))

    for start, direction, fps, delay, mode in ((0, 0, 3.5, 0.0, 0), (0, 1, 3.5, 0.0, 0), (-1, 0, 7.0, 0.5, 0),
                                               (-2, 0, 5.0, 0.0, 0), (0, 0, 1.0, 0.0, 1), (0, 0, 10.0, 0.25, 1)):
        trace_flip(f"synthetic_{start}_{direction}_{fps}_{delay}_{mode}",
                   struct.pack("<5i2fi", 0, 32, ctl.SUBTYPE_TEXFLIP, start, direction, fps, delay, mode))
    for p in sorted((ROOT / "unpacked" / "MODELS").rglob("*.pbd")):
        stem = p.stem
        if stem.endswith("_sky"):
            continue
        level = pbd.load(p)
        b = p.read_bytes()
        nfb = struct.unpack_from("<i", b, 4 + 9 * 4)[0]
        o = struct.unpack_from("<i", b, 0x40 + 9 * 4)[0]
        for k in range(nfb):
            n = struct.unpack_from("<i", b, o)[0]
            ids = [x & 0xFFFF for x in struct.unpack_from(f"<{n}i", b, o + 4)]
            books.append(f"{stem},{k},{'.'.join(map(str, ids))}")
            o += 4 + 4 * n
        showoff.append(f"{stem},{len(level.patches)},{sum(1 for q in level.patches if q.flag_word < 0)}")
        ssf = p.with_suffix(".ssf")
        if not ssf.is_file():
            continue
        data = ssf.read_bytes()
        for off, sub, w, f in ctl.scan_ssf(ssf):
            raw = data[off:off + w[1]].hex()
            if sub == ctl.SUBTYPE_UVSCROLL:
                s = ctl.uvscroll_from_record(w, f)
                trace = ctl.trace_uvscroll(s, max(FRAMES))
                vals = []
                for n in FRAMES:
                    vals += [repr(trace[n - 1][1]), repr(trace[n - 1][2])]
                rows.append(",".join([stem, str(off), raw] + vals))
            else:
                t = ctl.texflip_from_record(w, f)
                flips.append(",".join([stem, str(off), raw, str(t.start_frame), str(t.direction), repr(t.fps),
                                       repr(t.delay), str(t.mode)]))
                trace_flip(f"{stem}@{off}", data[off:off + 32])
    OUT.mkdir(parents=True, exist_ok=True)
    with open(OUT / "controller_reference.csv", "w", newline="\n") as fh:
        fh.write("# track,offset,record_hex," + ",".join(f"u{n},v{n}" for n in FRAMES) + "\n")
        fh.write("\n".join(rows) + "\n")
        fh.write("# texflip,track,offset,record_hex,start,direction,fps,delay,mode\n")
        fh.write("\n".join("texflip," + r for r in flips) + "\n")
        fh.write("# texflip_trace,label,record_hex,frame_count,frames 0..600\n")
        fh.write("\n".join("texflip_trace," + r for r in traces) + "\n")
        fh.write("# flipbook,track,index,texture ids\n")
        fh.write("\n".join("flipbook," + r for r in books) + "\n")
    with open(OUT / "showoff_reference.csv", "w", newline="\n") as fh:
        fh.write("# track,patches,showoff_patches\n" + "\n".join(showoff) + "\n")
    print(f"controller_reference.csv: {len(rows)} UVScroll, {len(flips)} TexFlip records; "
          f"showoff_reference.csv: {len(showoff)} tracks")
    for line in showoff:
        print("  " + line)


if __name__ == "__main__":
    main()
