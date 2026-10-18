"""Export the .afl animation curves, skeletons and controller tables the rider animation reads at run time.
Usage:
    python tools/export_anim_clips.py              # -> Unreal/SSXTrickyRebuild/SimData/anim_clips.bin
    python tools/export_anim_clips.py --out FILE
"""
# 0x15ADA8 / 0x169BE8 / 0x15FCF8
import libpath  # noqa: F401,E402  (tools/lib on sys.path)
import argparse
import os
import struct
import sys

import numpy as np

HERE = os.path.dirname(os.path.abspath(__file__))
REPO = os.path.dirname(HERE)
OUT = os.path.join(REPO, "Unreal", "SSXTrickyRebuild", "SimData", "anim_clips.bin")
MDL_DIR = os.path.join(REPO, "unpacked", "CHAR", "MDLPS2", "data", "char")
BOARD_MPF = os.path.join(REPO, "unpacked", "CHAR", "BRDPS2", "data", "char", "board.mpf")

FIRST_ID, N_IDS = 0x1F0, 0x110
N_CLASSES, N_CHARS, N_VARIANTS = 3, 12, 10
EXTRA_IDS = [0]
RIDERS = ["eddie", "kaori", "luther", "mac", "moby", "zoe", "jp", "elise", "psymon", "seeiah", "brodi",
          "marisol", "zz_mmm"]
BOARD_MODELS = ["Bx", "BxGoofy", "Fr", "FrGoofy", "Al", "AlGoofy"]


def s(name: str) -> bytes:
    b = name.encode("latin1")
    return struct.pack("<B", len(b)) + b


class Exporter:
    def __init__(self):
        sys.path.insert(0, HERE)
        import afl  # noqa: E402  (boot-time animation lookup tables built from the exe, exe_tables.anim_boot_image)
        import afl_curves  # noqa: E402
        self.afl, self.C = afl, afl_curves
        self.bank = afl.Bank()
        self.files = {}
        self.clips = []
        self.clip_events = []
        self.clip_index = {}
        self.tracks = []
        self.track_index = {}      # (file name, byte offset) -> track index

    def afl_file(self, name):
        if name not in self.files:
            self.files[name] = self.C.load_afl(name)
        return self.files[name]

    def clip_for_record(self, rec) -> int:
        key = (rec.file, rec.index)
        if key not in self.clip_index:
            f = self.afl_file(rec.file)
            self.clip_index[key] = len(self.clips)
            self.clips.append((f, f.anims[rec.index]))
            self.clip_events.append(list(rec.events))
        return self.clip_index[key]

    def variants(self, aid: int, bc: int, ch: int) -> list:
        """Clip per name index of 0x15ADA8, positional (a missing name stays -1 at its draw slot)."""
        v = []
        for ni in self.bank.name_indices(aid, bc, ch)[:N_VARIANTS]:
            recs = [] if ni == self.afl.NAME_NONE or ni >= len(self.bank.names) else                 self.bank.by_hash.get(self.afl.elf23(self.bank.names[ni]), [])
            v.append(self.clip_for_record(recs[0]) if recs else -1)
        return v + [-1] * (N_VARIANTS - len(v))

    def variant_count(self, aid: int) -> int:
        """Random-variant count of a lookup-type-3 id; 1 otherwise."""
        return len(self.bank.name_indices(aid, 0, 0)) if self.bank.props(aid)[1] == 3 else 1

    def clip_for_name(self, name: str) -> int:
        recs = self.bank.by_hash.get(self.afl.elf23(name), [])
        return self.clip_for_record(recs[0]) if recs else -1

    def track(self, f, idx: int) -> int:
        off = f.track_offset(idx)
        key = (f.name, off)
        if key not in self.track_index:
            self.track_index[key] = len(self.tracks)
            self.tracks.append(self.C.parse_track(f.d, off))
        return self.track_index[key]

    def build(self) -> bytes:
        bank = self.bank
        props = [bank.props(FIRST_ID + i) for i in range(N_IDS)]
        lookup = []
        for i in range(N_IDS):
            for bc in range(N_CLASSES):
                for ch in range(N_CHARS):
                    lookup += self.variants(FIRST_ID + i, bc, ch)
        counts = [self.variant_count(FIRST_ID + i) for i in range(N_IDS)]
        # 0x169BE8
        extra_ids = list(EXTRA_IDS) + [i for i in range(1, FIRST_ID) if bank.resolve(i, 0, 0)]
        extras = struct.pack("<I", len(extra_ids))
        for aid in extra_ids:
            extras += struct.pack("<i7ih", aid, *bank.props(aid), self.variant_count(aid))
            for bc in range(N_CLASSES):
                for ch in range(N_CHARS):
                    extras += struct.pack(f"<{N_VARIANTS}h", *self.variants(aid, bc, ch))
        fe = [r for i in range(0x2F9, 0x2FF) for ch in range(N_CHARS) for r in bank.resolve(i, 0, ch)[:1]]
        if len(fe) != 72 or any(r.file != "feanim.afl" for r in fe):
            raise SystemExit("feanim.afl: expected 72 FE clips (ids 0x2F9..0x2FE x 12 characters)")
        T = self.C.controller_tables()
        tables = []
        for c in range(3):
            tables += [self.clip_for_name(n) for n in T["riding"][c]]
        for c in range(3):
            tables += [self.clip_for_name(n) for n in T["air_dir"][c]]
        tables += [self.clip_for_name(T["air_idle"][c]) for c in range(3)]
        for c in range(3):
            tables += [self.clip_for_name(n) for n in T["switch"][c]]
        for c in range(3):
            for r in range(10):
                tables += [self.clip_for_name(n) for n in T["prewind"][c][r]]
        for c in range(3):
            for r in range(6):
                tables += [self.clip_for_name(n) for n in T["rail"][c][r]]

        clip_blobs = []
        body = self.C.load_skeleton("brodi")
        board = self.C.load_skeleton("brodi", "board")
        n_tracks = {0: sum(3 * ((b.unknown & 1) + ((b.unknown >> 1) & 1)) for b in body),
                    1: sum(3 * ((b.unknown & 1) + ((b.unknown >> 1) & 1)) for b in board)}
        k = 0
        while k < len(self.clips):
            f, a = self.clips[k]
            blocks = list(a.blocks)
            blob = s(a.name) + struct.pack("<H4hB", a.n_frames, *[-1 if m is None else m for m in a.markers],
                                           len(blocks))
            for blk in blocks:
                n = n_tracks.get(blk.chan) or self.block_run(f, blk)
                blob += struct.pack("<HH", blk.chan, n)
                blob += struct.pack(f"<{n}I", *[self.track(f, blk.first + t) for t in range(n)])
            ev = self.clip_events[k]
            blob += struct.pack("<B", len(ev))
            for frame, value in ev:
                blob += struct.pack("<HH", frame, value)
            clip_blobs.append(blob)
            k += 1

        track_blob = bytearray()
        for segs in self.tracks:
            track_blob += struct.pack("<H", len(segs))
            for typ, n, c in segs:
                track_blob += struct.pack("<BH", typ, n)
                if typ <= 3:
                    track_blob += struct.pack(f"<{typ + 1}f", *c)
                elif typ == 4:
                    track_blob += struct.pack(f"<{n}f", *c)
                else:
                    base, scale, q = c
                    track_blob += struct.pack(f"<ff{len(q)}H", base, scale, *q)

        sys.path.insert(0, HERE)
        import mpf
        skel_blob = bytearray()
        skeletons = []
        for r in RIDERS:
            path = os.path.join(MDL_DIR, f"{r}_body.mpf")
            if os.path.exists(path):
                skeletons.append((r, mpf.load(path)[0].bones))
        skeletons.append(("board", mpf.load(BOARD_MPF)[0].bones))
        for name, bones in skeletons:
            skel_blob += s(name) + struct.pack("<B", len(bones))
            for b in bones:
                parent = b.parent_index if b.parent_file != -1 else -1
                skel_blob += s(b.name) + struct.pack("<bBh3f3f9f", parent, b.unknown, b.bone_id,
                                                     *b.translation, *b.rotation, *b.extra)
        models = {m.name: m for m in mpf.load(BOARD_MPF)}
        board_blob = bytearray()
        for name in BOARD_MODELS:
            m = models[name]
            pos = np.concatenate([c.positions for g in m.groups for blk in g.blocks for c in blk.chunks])
            ik = np.asarray(m.ik_points, dtype=np.float64)[:2].reshape(-1)
            board_blob += s(name) + struct.pack("<6ff", *ik, float(pos[:, 2].min()))

        out = bytearray(struct.pack("<4s7I", b"SSXC", 4, FIRST_ID, N_IDS, len(self.clips), len(self.tracks),
                                    len(skeletons), len(BOARD_MODELS)))
        for p in props:
            out += struct.pack("<7i", *p)
        out += struct.pack(f"<{len(lookup)}h", *lookup)
        out += struct.pack(f"<{len(counts)}h", *counts)
        out += extras
        out += struct.pack(f"<{len(tables)}h", *tables)
        out += track_blob
        for b in clip_blobs:
            out += b
        out += skel_blob
        out += board_blob
        out += self.parts_blob(mpf)
        return bytes(out)

    def block_run(self, f, blk) -> int:
        """Track count of a head-part block: the track-table entries up to the next block of the file."""
        if not hasattr(f, "_firsts"):
            f._firsts = sorted({b.first for an in f.anims.values() for b in an.blocks})
            f._table_end = (f.off_data - f.off_tracks) // 4
        i = f._firsts.index(blk.first)
        return (f._firsts[i + 1] if i + 1 < len(f._firsts) else f._table_end) - blk.first

    def parts_blob(self, mpf) -> bytes:
        """Per rider the model parts with bones or morph keys besides body (0) and board (1), highest LOD, in the
        order of the game's pose objects: face, eyes, goggles, hair, the rest."""
        order = {10: 0, 5: 1, 6: 2, 9: 2}
        riders = []
        for r in RIDERS:
            models = []
            for part in ("body", "head"):
                path = os.path.join(MDL_DIR, f"{r}_{part}.mpf")
                if os.path.exists(path):
                    models += [m for m in mpf.load(path) if "3000" in m.name and not m.is_shadow]
            parts = [m for m in models if m.file_id >= 2 and (m.bones or m.morph_key_count)]
            parts.sort(key=lambda m: (order.get(m.file_id, 3), models.index(m)))
            riders.append((r, parts))
        blob = bytearray(struct.pack("<I", len(riders)))
        for r, parts in riders:
            blob += s(r) + struct.pack("<B", len(parts))
            for m in parts:
                blob += struct.pack("<BBB", m.file_id, m.morph_key_count, len(m.bones))
                for b in m.bones:
                    blob += s(b.name) + struct.pack("<bbBh3f3f9f", b.parent_file, b.parent_index, b.unknown, b.bone_id,
                                                    *b.translation, *b.rotation, *b.extra)
        return bytes(blob)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--out", default=OUT)
    args = ap.parse_args()
    ex = Exporter()
    blob = ex.build()
    os.makedirs(os.path.dirname(os.path.abspath(args.out)), exist_ok=True)
    with open(args.out, "wb") as fh:
        fh.write(blob)
    print(f"wrote {args.out}: {len(blob)} bytes, {len(ex.clips)} clips, {len(ex.tracks)} tracks")


if __name__ == "__main__":
    main()
