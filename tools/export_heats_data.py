"""World Circuit heat data + reference values (heats model, tools/lib/models/heats.py).
Usage:
    python tools/export_heats_data.py
      -> Unreal/SSXTrickyRebuild/SimData/heats_data.json       story tables (.data 0x37B390 / 0x37B400 / 0x37BA40 of the
                                                         user's extracted/SLUS_203.26) and the Aggression Manager
"""
import libpath  # noqa: F401,E402  (tools/lib on sys.path)
import json
import os
import struct
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
REPO = os.path.dirname(HERE)
SIMDATA = os.path.join(REPO, "Unreal", "SSXTrickyRebuild", "SimData")
sys.path.insert(0, os.path.join(HERE, "lib"))
import exe_tables  # noqa: E402
import traces  # noqa: E402

EXE = open(os.path.join(REPO, "extracted", "SLUS_203.26"), "rb").read()
RS, C, ELIM, AGG_PTR = 0x32DB6C, 0x32E59C, 0x335658, 0x3352B4
TABLES = ((0x37B390, 9), (0x37B400, 133), (0x37BA40, 159))


def off(va):
    return va - 0x100000 + 0x1000


def i32(b, a):
    return struct.unpack_from("<i", b, a)[0]


class Seq:
    """Deterministic stand-in for stream A: a fixed LCG so both sides replay the same values."""
    def __init__(self, seed):
        self.x = seed & 0xFFFFFFFF
        self.values = []

    def __call__(self):
        self.x = (self.x * 1103515245 + 12345) & 0xFFFFFFFF
        self.values.append(self.x)
        return self.x


def main():
    raw = [[list(EXE[off(a) + 12 * i:off(a) + 12 * i + 12]) for i in range(n)] for a, n in TABLES]
    agg = exe_tables.aggression_table()          # built by the executable at 0x167208
    os.makedirs(SIMDATA, exist_ok=True)
    json.dump({"source": "SLUS_203.26 .data 0x37B390/0x37B400/0x37BA40 (story tables); Aggression Manager RAM table",
               "story_tables": raw, "aggression": agg}, open(os.path.join(SIMDATA, "heats_data.json"), "w"))
    write_reference(agg)


def write_reference(agg):
    import heats as H
    tables = [H.parse_story_table(EXE[off(a):off(a) + 12 * n], n) for a, n in TABLES]

    ref = {"lookups": [], "grids": [], "levels": [], "results": [], "ram": []}
    for seed in range(40):
        rider, venue, mode = seed % 12, (seed * 7) % 12, 4 if seed % 3 else 2
        rnd = Seq(seed)
        res = H.story_lookup(tables, rider, venue, mode, seed % 3, rnd)
        ref["lookups"].append({"seed": seed, "rider": rider, "venue": venue, "mode": mode, "heat": seed % 3,
                               "riders": res.riders, "ids": res.intro_ids[:3], "types": res.intro_types[:3],
                               "draws": len(rnd.values)})
        rnd = Seq(1000 + seed)
        elim = H.Elimination()
        elim.init_heat0(tables, rider, venue, mode, rnd)
        seq = []
        for heat in range(3):
            grid, story = H.build_grid(elim, tables, rider, venue, mode, heat, rnd)
            finish = list(grid)
            k = seed % 6
            finish = finish[k:] + finish[:k]
            adv, factor = H.heat_result(elim, finish, rider, heat)
            seq.append({"grid": grid, "types": story.intro_types[:3], "finish": finish, "advance": adv,
                        "factor": factor if factor is not None else 0.0,
                        "status": [list(s) for s in elim.status], "flag": list(elim.flag)})
        ref["grids"].append({"seed": 1000 + seed, "rider": rider, "venue": venue, "mode": mode, "heats": seq})
    for seed in range(10):
        riders = [(seed + 3 * k) % 12 for k in range(6)]
        carried = [(-1 if (k + seed) % 3 == 0 else (k * 17 + seed) % 100) for k in range(12)]
        career = [(k * 13 + seed * 5) % 100 for k in range(12)]
        for heat in (0, 1):
            ref["levels"].append({"riders": riders, "human": seed % 6, "heat": heat, "carried": carried, "career": career,
                                  "levels": H.initial_levels(agg, riders, seed % 6, heat, carried, career)})
    sessions = ("ride_session_1", "ride_session_2")
    have_ram = all(traces.available("live", s, "spawn_ram.bin") for s in sessions) and traces.available(
        "live", "ride_session_2", "summary.csv")
    if not have_ram:
        print("heats: RAM snapshot reference skipped (no traces)")
        sessions = ()
    for s in sessions:
        b = traces.path("live", s, "spawn_ram.bin").read_bytes()
        n = i32(b, RS)
        grid = [i32(b, RS + 4 + i * 0x84 + 0x38) for i in range(n)]
        human = [i for i in range(n) if struct.unpack_from("<b", b, RS + 4 + i * 0x84 + 0x65)[0] >= 0][0]
        entry = {"name": s, "grid": grid, "human": human, "heat": i32(b, C + 0x14), "venue": i32(b, RS + 0x31C),
                 "mode": i32(b, RS + 0x320), "carried": [i32(b, C + 0x1C + 4 * k) for k in range(12)],
                 "status": [[i32(b, ELIM + r * 16 + 4 * k) for k in range(3)] for r in range(12)],
                 "flag": [i32(b, ELIM + r * 16 + 12) for r in range(12)],
                 "meters": [struct.unpack_from("<f", b, RS + 4 + i * 0x84 + 0x70)[0] for i in range(n)],
                 "slot_types": [i32(b, RS + 0x33C + 100 * k) for k in range(3)],
                 "levels": [[struct.unpack_from("<b", b, RS + 4 + i * 0x84 + 0x20 + 4 * j)[0] for j in range(6)] for i in range(6)]}
        best = None
        for slot in range(4):
            car = struct.unpack_from("<I", b, 0x32E48C + slot * 0x44 + 0x18)[0]
            if not 0x100000 < car < len(b):
                continue
            levels = [struct.unpack_from("<i", b, car + 0x168 + 8 * k)[0] for k in range(12)]
            model = H.initial_levels(agg, grid, human, entry["heat"], entry["carried"], levels)
            hits = sum(model[i][j] == entry["levels"][i][j] for i in range(6) for j in range(6))
            if best is None or hits > best[0]:
                best = (hits, levels)
        entry["career"] = best[1]
        ptr = struct.unpack_from("<I", b, AGG_PTR)[0]
        live = struct.unpack_from("<576i", b, ptr)
        entry["agg_equal"] = all(live[(m * 12 + o) * 4 + w] == agg[m][o][w] for m in range(12) for o in range(12) for w in range(4))
        ref["ram"].append(entry)
    if have_ram:
        trace = traces.path("live", "ride_session_2", "summary.csv").read_text().splitlines()
        row = next(ln.split(",") for ln in trace[1:] if ln.startswith("301,0xdd8cc0"))
        ref["live_meter_301"] = float(row[12])
    json.dump(ref, open(os.path.join(SIMDATA, "heats_reference.json"), "w"))
    print(f"heats: {sum(len(t) for t in tables)} story records, {len(ref['grids'])} grid sequences, "
          f"{len(ref['levels'])} level cases, {len(ref['ram'])} RAM snapshots -> {SIMDATA}")


if __name__ == "__main__":
    main()
