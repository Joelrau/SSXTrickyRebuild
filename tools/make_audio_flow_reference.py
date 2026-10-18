"""Reference values for SSX.Audio.RaceFlow / SSX.Audio.UiSounds (tools/lib/models/race_audio.py,
RAM snapshots in SSX_TRACES; prints "skipped (no traces)" without them).
Usage:
    python tools/make_audio_flow_reference.py   # -> Unreal/SSXTrickyRebuild/SimData/audio_flow_reference.json
"""
import libpath  # noqa: F401,E402  (tools/lib on sys.path)
import traces  # noqa: E402

traces.require()
TR = traces.root()

import json
import os
import struct
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
REPO = os.path.dirname(HERE)
OUT = os.path.join(REPO, "Unreal", "SSXTrickyRebuild", "SimData", "audio_flow_reference.json")
import race_audio as RA  # noqa: E402


def seq_rand(values):
    it = iter(values)
    return lambda: next(it)


def main():
    out = {}
    songs = RA.song_list()
    out["songs"] = {"list": songs, "masks": {sec: RA.musicmap_mask(sec) for sec, _ in RA.read_inf("MUSICMAP.INF")}}
    picks = []
    for mask_sec in ("GARI", "SNOW", "MERQUERY", "TRICK"):
        mask = RA.musicmap_mask(mask_sec)
        for r in (0, 1, 5000, 16383, 20000, 32767):
            picks.append({"mask": mask, "rand": r, "song": RA.pick_race_song(mask, len(songs), seq_rand([r]))})
    out["pick"] = picks
    intro = []
    for track in (0, 1, 3, 5, 7):
        for restart in (False, True):
            for rs in ([0, 0, 0], [32767, 32767, 32767], [12000, 25000, 4000]):
                intro.append({"track": track, "restart": restart, "rand": rs,
                              "queue": RA.initial_intro_queue(track, restart, seq_rand(rs))})
    out["intro"] = intro
    out["countdown"] = [[f, e] for f, e in RA.countdown_events()]
    grid = []
    for racers in (1, 2, 4, 6):
        for rank in range(1, racers + 1):
            for boost in (0.0, 0.3, 0.5, 0.66, 1.0):
                for track in (0, 10):
                    grid.append([racers, rank, boost, track, RA.intensity(racers, rank, boost, track)])
    out["intensity"] = grid

    rand_vals = [1000 * (k % 30) for k in range(1000)]
    rnd = seq_rand(rand_vals)
    a = RA.RaceAudio(track=0, intro_duck=0.70)
    a.queue = RA.initial_intro_queue(0, False, rnd)
    rows = [{"frame": 0, "state": a.state, "queue": list(a.queue)}]
    for frame in range(1, 1201):
        if frame % 350 == 0 and a.queue:
            a.queue.pop(0)
        if frame == 900:
            a.intro_end()
        if frame == 1050:
            a.go()
        a.frame(rnd, 4, 2, 0.5)
        rows.append({"frame": frame, "state": a.state, "queue": list(a.queue), "duck_mode": a.duck_mode,
                     "duck": a.duck_level, "control": a.control})
    out["timeline"] = {"rand": rand_vals, "drain_every": 350, "intro_end": 900, "go": 1050, "racers": 4, "rank": 2,
                       "boost": 0.5, "rows": rows}

    zones = []
    for script in ([("enter", 2), ("leave", 2), ("tick", 1000)],
                   [("enter", 2), ("enter", 3), ("leave", 2), ("tick", 1000), ("leave", 3), ("tick", 999), ("tick", 1)],
                   [("enter", 4), ("enter", 2), ("leave", 4), ("tick", 500), ("enter", 4), ("tick", 600)],
                   [("enter", 6), ("reset", 0)]):
        z = RA.MusicZones()
        for op, v in script:
            if op == "enter":
                z.enter(v)
            elif op == "leave":
                z.leave(v)
            elif op == "tick":
                z.tick(v)
            else:
                z.reset()
        zones.append({"script": script, "posted": z.posted})
    out["zones"] = zones
    out["triggers"] = [[t, l, RA.trigger_event(t, l)] for t in (1, 9, 10, 18, 12) for l in (0, 1)]

    ram = []
    for s in ("ride_session_1", "ride_session_2", "smoke_test"):
        p = os.path.join(TR, "live", s, "spawn_ram.bin")
        if not os.path.exists(p):
            continue
        r = open(p, "rb").read()
        u = lambda a: struct.unpack_from("<I", r, a)[0]  # noqa: E731
        f = lambda a: struct.unpack_from("<f", r, a)[0]  # noqa: E731
        snd, track = u(0x343050), u(0x32DE88)
        names = [r[snd + 0x12E0 + 0x20 * k: snd + 0x12E0 + 0x20 * k + 0x20].split(b"\0")[0].decode("latin-1")
                 for k in range(u(snd + 0x36E0))]
        ram.append({"session": s, "track": track, "songs": names, "mask": u(snd + 0x36EC), "song": u(snd + 0x36E8),
                    "duck": f(snd + 0x7440), "state": u(snd + 0x6E34)})
    out["ram"] = ram
    os.makedirs(os.path.dirname(OUT), exist_ok=True)
    with open(OUT, "w") as fh:
        json.dump(out, fh, indent=1)
    print(f"wrote {OUT}: {len(songs)} songs, {len(ram)} RAM snapshots")
    for x in ram:
        print(" ", x["session"], "track", x["track"], "mask", hex(x["mask"]), "song", x["song"], "duck", x["duck"], "state", x["state"])


if __name__ == "__main__":
    main()
