"""Export the animation timing table the rider simulation reads at run time (anim_table.bin).
Usage:
    python tools/export_anim_table.py                 # -> Unreal/SSXTrickyRebuild/SimData/anim_table.bin
    python tools/export_anim_table.py --out FILE
    python tools/export_anim_table.py --dump          # also print what was exported
"""
import libpath  # noqa: F401,E402  (tools/lib on sys.path)
import argparse
import os
import struct
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
REPO = os.path.dirname(HERE)
OUT = os.path.join(REPO, "Unreal", "SSXTrickyRebuild", "SimData", "anim_table.bin")

N_CLASSES, N_CHARS, N_SLOTS, N_VARIANTS = 3, 12, 15, 3
NO_TRICK = 26
NONE16 = 0xFFFF

ONE_SHOTS = [
    0x1FD,   # G_GATE2BASE      Gate2Base push
    0x1FE,   # SG_FREERIDEGU    GetupFromSit sit-up
    0x200, 0x201,   # R_SWITCH / R_UNSWITCH  ChangeFakie, fakie un-switch
    0x203, 0x204,   # R_SWITCH180HS/TS FakieCruise turn-around
    0x221,   # J_2GO            skating push
    0x223,   # J_AIRT2S         air idle
    0x225,   # A_FAKIETOAIR     auto-180
    0x23E, 0x23F, 0x240, 0x241, 0x242, 0x243,   # LandHard family (0x23E random of 3)
    0x244, 0x245, 0x246, 0x247, 0x248, 0x249,   # Bump
    0x24A, 0x24B, 0x24C, 0x24D, 0x24E, 0x24F,   # WipeOutRecover get-ups
    0x250, 0x251, 0x252, 0x253,   # quick recovery, get-ups to fakie
    0x205, 0x206, 0x207, 0x208,   # shove overlays
    0x258, 0x259, 0x25A, 0x25B, 0x25C, 0x25D, 0x25E, 0x25F,   # rail switch RSR_*
    0x2EE, 0x2EF, 0x214, 0x215,   # FinishLine 0x106668: R_CRUISE2FINISH, cmFL_FINISHCYCLE, finish taunts
    *range(0x226, 0x22E),   # A_INTO*  Spin control into clips
    *range(0x236, 0x23E),   # A_OUTOF* play-outs 0x102800 (0x23C / 0x23D half-spins)
    *range(0x26E, 0x278),   # RSA_*    rail-to-air idles / into clips
]


def load_bank():
    import afl  # noqa: E402  (the .afl files and the exe; boot-time lookup tables run from the exe)
    return afl, afl.Bank()


def anim_record(bank, anim_id, board_class, character):
    """(id, frames, category, markers) of one grab-table animation, or None."""
    if anim_id is None:
        return None
    recs = bank.resolve(anim_id, board_class, character)
    if not recs:
        return None
    return anim_id, recs[0].n_frames, bank.props(anim_id)[0], recs[0].markers


def pack_record(rec):
    if rec is None:
        return struct.pack("<iHH4H", -1, 0, 0, NONE16, NONE16, NONE16, NONE16)
    aid, n, cat, markers = rec
    m = [NONE16 if x is None else int(x) for x in markers]
    return struct.pack("<iHH4H", aid, n, cat, *m)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--out", default=OUT)
    ap.add_argument("--dump", action="store_true")
    args = ap.parse_args()

    afl, bank = load_bank()
    blob = bytearray(struct.pack("<4s5I", b"SSXA", 1, N_CLASSES, N_CHARS, N_SLOTS, len(ONE_SHOTS)))
    lines = []
    for bc in range(N_CLASSES):
        for ch in range(N_CHARS):
            rows = list(bank.grab_table(bc, ch).items())[:N_SLOTS]
            while len(rows) < N_SLOTS:
                rows.append((NO_TRICK, (None, None, None)))
            for gid, ids in rows:
                recs = [anim_record(bank, aid, bc, ch) for aid in ids]
                blob += struct.pack("<i", gid)
                for r in recs:
                    blob += pack_record(r)
                if args.dump and gid != NO_TRICK:
                    lines.append(f"bc {bc} ch {ch:2d} grab {gid:2d}: " + "  ".join(
                        "-" if r is None else f"{r[0]:#x} n{r[1]} c{r[2]} {r[3]}" for r in recs))
    for aid in ONE_SHOTS:
        blob += struct.pack("<i", aid)
        for bc in range(N_CLASSES):
            for ch in range(N_CHARS):
                ns = [r.n_frames for r in bank.resolve(aid, bc, ch)][:N_VARIANTS]
                ns += [0] * (N_VARIANTS - len(ns))
                blob += struct.pack("<3H", *ns)
                if args.dump and ch == 0:
                    lines.append(f"one-shot {aid:#x} bc {bc}: {ns}")
    os.makedirs(os.path.dirname(os.path.abspath(args.out)), exist_ok=True)
    with open(args.out, "wb") as fh:
        fh.write(blob)
    for line in lines:
        print(line)
    print(f"wrote {args.out}: {len(blob)} bytes, {N_CLASSES}x{N_CHARS}x{N_SLOTS} trick rows, {len(ONE_SHOTS)} one-shots")


if __name__ == "__main__":
    main()
