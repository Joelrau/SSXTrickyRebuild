"""Reference values for the SSX.Progression automation tests, computed with tools/lib/models/progression.py
and a PCSX2 RAM snapshot in SSX_TRACES (prints "skipped (no traces)" without it).
Usage:  python tools/make_progression_reference.py [session]      (default ride_session_1)
"""
import libpath  # noqa: F401,E402  (tools/lib on sys.path)
import traces  # noqa: E402

traces.require()
TR = traces.root()

import json
import struct
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
from models import progression as P  # noqa: E402

session = sys.argv[1] if len(sys.argv) > 1 else "ride_session_1"
ram = (TR / "live" / session / "spawn_ram.bin").read_bytes()
OUT = ROOT / "Unreal" / "SSXTrickyRebuild" / "SimData" / "progression_reference.json"
MGR = 0x32B390


def i32(a, n=1):
    return list(struct.unpack_from("<%di" % n, ram, a))


def career(c):
    R = MGR + 0x100 + c * 0x1C4
    return dict(
        char=c, raw=i32(R)[0], race_points=i32(R + 4)[0], showoff_points=i32(R + 8)[0],
        attr=list(ram[R + 0xC:R + 0x10]), points=list(ram[R + 0x10:R + 0x14]),
        book=i32(R + 0x14, 4), tracks=[list(ram[R + 0x24 + 4 * t:R + 0x28 + 4 * t]) for t in range(13)],
        boards=struct.unpack_from("<I", ram, R + 0x58)[0], outfits=struct.unpack_from("<I", ram, R + 0x5C)[0],
        opponents=[i32(R + 0x164 + 8 * k, 2) for k in range(12)])


sim = P.new_save()
careers = []
for c in range(12):
    rec = career(c)
    if rec["race_points"] + rec["showoff_points"] == 0:
        continue
    s = sim.careers[c]
    s.book_done_a, s.book_done_b = rec["book"][1], rec["book"][2]
    events = []
    for v in P.CIRCUIT_VENUES:
        m = rec["tracks"][v][1]
        if m < 3:
            events.append(dict(mode=4, venue=v, heat=2, place=m, score=0,
                               unlocks=P.award_event(sim, c, 4, v, heat=2, place=m)))
    for v in P.CIRCUIT_VENUES:
        m = rec["tracks"][v][0]
        if m < 3:
            score = P.SHOWOFF_THRESHOLDS[v][m]
            events.append(dict(mode=5, venue=v, heat=2, place=0, score=score,
                               unlocks=P.award_event(sim, c, 5, v, score=score)))
    rec["events"] = events
    rec["model"] = dict(race_points=s.race_rank_points, showoff_points=s.showoff_rank_points,
                        tracks=[[t.showoff_medal, t.race_medal, t.race_open, t.showoff_open] for t in s.tracks],
                        boards=s.boards, outfits=s.outfits, raw=s.raw_points,
                        bonus=[o[0] for o in s.opponents])
    careers.append(rec)

venue_mask, rider_mask = struct.unpack_from("<II", ram, MGR + 8)
masks = dict(ram_venue=venue_mask, ram_rider=rider_mask, model_venue=sim.venue_mask, model_rider=sim.rider_mask)

defaults = json.loads((ROOT / "converted" / "frontend" / "progression_tables.json").read_text())["records"]
records = []
for t in range(13):
    for k, kind in enumerate(("race", "showoff", "tc")):
        b = MGR + 0x16C4 + t * 0x150 + k * 0x70
        cnt, hb, tm = i32(b + 0x64, 3)
        rows = [[ram[b + e * 0x14:b + e * 0x14 + 12].split(b"\0")[0].decode("latin-1"),
                 i32(b + e * 0x14 + 0xC)[0], i32(b + e * 0x14 + 0x10)[0]] for e in range(cnt)]
        tab = P.new_record_tables(t, defaults)[kind]
        dflt = [[n, v] for n, _, v in tab.rows]
        inserts = []
        if [[n, v] for n, _, v in rows] != dflt:
            new = [[n, v] for n, _, v in rows if [n, v] not in dflt]
            for nm, v in sorted(new, key=lambda x: x[1], reverse=not tab.higher_better):
                row = P.insert_record(tab, v, nm, 0)
                inserts.append([nm, v, row])
        records.append(dict(track=t, kind=kind, count=cnt, higher_better=hb, is_time=tm, rows=rows, inserts=inserts,
                            model=[[n, v] for n, _, v in tab.rows]))

blob = bytearray(ram[MGR + 8:MGR + 8 + 0x27D4])
stored = struct.unpack_from("<I", blob, 0x27CC)[0]
struct.pack_into("<I", blob, 0x27CC, 0)
crc_cases = []
for seed in (1, 2, 12345, 0xFBEA, 20326):
    x, g = seed, bytearray()
    for _ in range(0x27D4):
        x = (x * 1103515245 + 12345) & 0x7FFFFFFF
        g.append((x >> 16) & 0xFF)
    struct.pack_into("<I", g, 0x27CC, 0)
    struct.pack_into("<I", g, 0x27D0, 0x12300003)
    crc_cases.append(dict(seed=seed, crc=P.blob_checksum(bytes(g))))

ref = dict(source="models/progression.py on traces/live/%s/spawn_ram.bin" % session,
           careers=careers, masks=masks, stat_max=[list(P.bar_maximum(c)) for c in range(12)],
           alt=[list(P.stat_maximum(c)) for c in range(12)], records=records,
           blob=dict(hex=bytes(blob).hex(), stored_checksum=stored, crc=P.blob_checksum(bytes(blob))),
           crc_cases=crc_cases)
OUT.parent.mkdir(parents=True, exist_ok=True)
OUT.write_text(json.dumps(ref))
same = sum(1 for r in records if not r["inserts"])
print("careers %s, masks %#x/%#x (model %#x/%#x), records %d default + %d replayed, blob crc %#06x -> %s"
      % ([c["char"] for c in careers], venue_mask, rider_mask, sim.venue_mask, sim.rider_mask, same,
         len(records) - same, ref["blob"]["crc"], OUT))
