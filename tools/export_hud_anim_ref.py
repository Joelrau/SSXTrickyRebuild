"""Reference data for SSX.HUD.Anim / SSX.Audio.Tricky: runs tools/lib/models/hud_anim.py on the RAM snapshots
and live traces in SSX_TRACES, writing converted/hud/hud_anim_ref.json (git-ignored: derived from game RAM / traces).
Without SSX_TRACES it prints "skipped (no traces)".
  python tools/export_hud_anim_ref.py
"""
import libpath  # noqa: F401,E402  (tools/lib on sys.path)
import collections
import csv
import json
import os
import struct

import traces

traces.require(*[("live", s, "spawn_ram.bin") for s in ("smoke_test", "ride_session_1", "ride_session_2")],
               *[("live", s, "summary.csv") for s in ("ride_session_1", "ride_session_2")])
import hud_anim as H  # noqa: E402

HUD = 0x1967880
out = {"ram": [], "orb": [], "live": []}

for s in ("smoke_test", "ride_session_1", "ride_session_2"):
    r = traces.path("live", s, "spawn_ram.bin").read_bytes()
    f = lambda o: struct.unpack_from("<f", r, o)[0]
    i = lambda o: struct.unpack_from("<i", r, o)[0]
    S = HUD + 0x114
    out["ram"].append({"session": s, "meter": f(S + 0x1F * 4), "pending": f(S + 0x20 * 4),
                       "scale": [f(S + 4 * (3 + 2 * k)) for k in range(14)],
                       "lit": [i(S + 4 * (4 + 2 * k)) for k in range(14)],
                       "seg": [list(struct.unpack_from("<ffIff", r, 0x338008 + 20 * k)) for k in range(14)]})

p = H.HudPlayer()
t = 20.0
while t > 0:
    o = p.draw_orb(t, True, 1.0)
    out["orb"].append([t, o[3] / 1.25, p.bounce])
    t = H.tricky_timer_step(t, 2)

for s in ("ride_session_1", "ride_session_2"):
    rows = collections.defaultdict(list)
    for row in csv.DictReader(open(traces.path("live", s, "summary.csv"))):
        rows[row["rider"]].append((int(row["frame"]), float(row["tricky_timer"]), int(row["motion"]),
                                   float(row["boost_meter"])))
    for rid, L in rows.items():
        au = H.TrickyAudio()
        ev = []
        prev = None
        for k, (fr, tm, mo, me) in enumerate(L):
            if prev is not None and tm > prev + 0.5:
                for e in au.meter_full(prev, 0, tm):
                    ev.append([k, e])
            for e in au.frame(tm, 0):
                ev.append([k, e])
            prev = tm
        out["live"].append({"session": s, "rider": rid, "frames": [list(x) for x in L], "events": ev})

dst = os.path.join(os.path.dirname(os.path.dirname(os.path.abspath(__file__))), "converted", "hud", "hud_anim_ref.json")
json.dump(out, open(dst, "w"))
print(dst, len(out["orb"]), "orb frames,", sum(len(x["frames"]) for x in out["live"]), "live samples,",
      sum(1 for x in out["live"] for e in x["events"] if "#107" in e[1]), "activations")
