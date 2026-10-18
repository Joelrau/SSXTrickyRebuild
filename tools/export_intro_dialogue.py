"""Pre-race / end-race intro dialogue data + RAM reference (port: Race/IntroDialogue.h).
The RAM reference ("ram") needs the recordings in SSX_TRACES (tools/lib/traces.py); without them it is empty.
Usage:
    python tools/export_intro_dialogue.py
      -> Unreal/SSXTrickyRebuild/SimData/intro_dialogue.json
           story_tables   the five intro-slot tables the slot builder scores besides heats_data.json's tables 0..2
"""
# 0x37C1B8 / 0x37C4B0 / 0x37CD60 / 0x37C720
import libpath  # noqa: F401,E402  (tools/lib on sys.path)
import json
import os
import struct
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
REPO = os.path.dirname(HERE)
SIMDATA = os.path.join(REPO, "Unreal", "SSXTrickyRebuild", "SimData")
import afl  # noqa: E402
import traces  # noqa: E402

EXE = open(os.path.join(REPO, "extracted", "SLUS_203.26"), "rb").read()
RS, CTX, OBJ = 0x32DB6C, 0x32ECF0, 0x32EDC0
TABLES = {"t3": (0x37C1B8, 63), "t4": (0x37C4B0, 52), "count": (0x37CD60, 1), "pair": (0x37C720, 133)}


def off(va):
    return va - 0x100000 + 0x1000


def s16(va):
    return struct.unpack_from("<h", EXE, off(va))[0]


def u16(va):
    return struct.unpack_from("<H", EXE, off(va))[0]


def i32m(b, a):
    return struct.unpack_from("<i", b, a)[0]


def main():
    doc = {"source": "SLUS_203.26 .data (see the tool docstring); .afl scene clips; RAM snapshots"}
    doc["story_tables"] = {k: [list(EXE[off(a) + 12 * i:off(a) + 12 * i + 12]) for i in range(n)]
                           for k, (a, n) in TABLES.items()}
    doc["intro_ids"] = [s16(0x37AB50 + 2 * i) for i in range(901)]
    doc["speaker_rows"] = [list(EXE[off(0x37B260) + 6 * g:off(0x37B260) + 6 * g + 6]) for g in range(20)]
    doc["group_sizes"] = [s16(0x37B2D8 + 2 * g) for g in range(20)]
    doc["variant_lines"] = [[s16(0x32F468 + 12 * i + 2 * j) for j in range(6)] for i in range(901)]
    doc["line_records"] = [[u16(0x37CD70 + 8 * k), u16(0x37CD72 + 8 * k), struct.unpack_from("<i", EXE, off(0x37CD74 + 8 * k))[0]]
                           for k in range((0x37DD10 - 0x37CD70) // 8)]
    bank = afl.Bank()
    clips = {}
    ids = sorted({x for row in doc["variant_lines"] for x in row if x > 0})
    for lid in ids:
        recs = bank.resolve(lid, 0, 0)
        if not recs:
            continue
        a = recs[0]
        typ = bank.props(lid)[1]
        clips[str(lid)] = {"name": a.name, "frames": a.n_frames, "events": [list(e) for e in a.events], "lookup": typ}
    doc["line_clips"] = clips
    ram = []
    sessions = ("ride_session_1", "ride_session_2")
    if not all(traces.available("live", s, "spawn_ram.bin") for s in sessions):
        print("intro dialogue: RAM snapshot reference skipped (no traces)")
        sessions = ()
    for s in sessions:
        b = traces.path("live", s, "spawn_ram.bin").read_bytes()
        n = i32m(b, RS)
        ram.append({
            "name": s, "racers": n, "venue": i32m(b, RS + 0x31C), "mode": i32m(b, RS + 0x320),
            "heat_word": i32m(b, RS + 0x330) & 0xFFFF,
            "riders": [i32m(b, RS + 4 + i * 0x84 + 0x38) for i in range(n)],
            "type80": [i32m(b, RS + 4 + i * 0x84 + 0x80) for i in range(n)],
            "port": [struct.unpack_from("<b", b, RS + 4 + i * 0x84 + 0x65)[0] for i in range(n)],
            "focus": i32m(b, RS + 0x918),
            "ctx_riders": [i32m(b, CTX + 4 * k) for k in range(5)], "ctx_player": i32m(b, CTX + 0xC8),
            "ctx_ids": [i32m(b, CTX + 0x14 + 4 * k) for k in range(15)],
            "ctx_types": [i32m(b, CTX + 0x50 + 4 * k) for k in range(15)],
            "ctx_variants": [i32m(b, CTX + 0x8C + 4 * k) for k in range(15)],
            "slots": [{"type": i32m(b, RS + 0x33C + 100 * k),
                       "lines": [[i32m(b, RS + 0x340 + 100 * k + 16 * j + 4 * w) for w in range(4)] for j in range(6)]}
                      for k in range(15)],
            "obj_slot": i32m(b, OBJ),
            "obj_list": [i32m(b, OBJ + 0x440 + 4 * k) for k in range(6)],
        })
    doc["ram"] = ram
    os.makedirs(SIMDATA, exist_ok=True)
    json.dump(doc, open(os.path.join(SIMDATA, "intro_dialogue.json"), "w"))
    print(f"intro dialogue: {len(doc['intro_ids'])} variants, {len(doc['line_records'])} line records, "
          f"{len(clips)} line clips, {len(ram)} RAM snapshots -> {SIMDATA}")


if __name__ == "__main__":
    main()
