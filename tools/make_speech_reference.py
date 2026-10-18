"""Reference values for SSX.Speech.* (tools/lib/models/speech.py; RAM requests from SSX_TRACES,
prints "skipped (no traces)" without them).
Usage:
    python tools/make_speech_reference.py   # -> Unreal/SSXTrickyRebuild/SimData/speech_reference.json
"""
# 0x343710
import libpath  # noqa: F401,E402  (tools/lib on sys.path)
import traces  # noqa: E402

traces.require()
TR = traces.root()

import json
import os
import random
import struct
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
REPO = os.path.dirname(HERE)
OUT = os.path.join(REPO, "Unreal", "SSXTrickyRebuild", "SimData", "speech_reference.json")
ROOT = os.path.join(REPO, "unpacked", "AUDIO", "SPEECH", "data", "speech")
NASCRIPT = os.path.join(REPO, "extracted", "DATA", "CONFIG", "NASCRIPT.INF")
import speech as S  # noqa: E402

EXE = open(os.path.join(REPO, "extracted", "SLUS_203.26"), "rb").read()
RACE = ("mc", "char", "narr", "anim")


def off(va):
    return va - 0x100000 + 0x1000


def line_json(eng, ln):
    return {"event": ln.event.id, "variant": ln.variant_index,
            "samples": [[eng.groups[g].name, si, o, n] for g, si, o, n in ln.samples]}


def engine(seed=0, rng_state=None, names=RACE):
    eng = S.SpeechEngine(seed=seed, rng_state=rng_state)
    for nm in names:
        eng.register(*S.load_section(ROOT, nm))
    return eng


def random_request(rnd, dbs):
    db = rnd.choice(dbs)
    evdb = db
    ev = rnd.choice(list(evdb.events.values()))
    words = [S.request_id(evdb.db, ev.id)]
    for _ in range(rnd.randint(0, 6)):
        k = rnd.random()
        if k < 0.6:
            words.append(1 << rnd.randint(0, 12))
        elif k < 0.8:
            words.append(rnd.randint(0, 120))
        else:
            words.append((1 << rnd.randint(0, 12)) | 1)
    return words


def main():
    out = {"sequences": [], "nascript": [], "fe": [], "ram": [], "rng": []}
    sections = {nm: S.load_section(ROOT, nm) for nm in RACE}
    dbs = [sections[nm][0] for nm in RACE]
    for seed in range(60):
        rnd = random.Random(seed)
        eng = engine(seed=seed)
        steps = []
        for _ in range(40):
            req = random_request(rnd, dbs)
            ev, ch = eng._event(req[0])
            posted = eng.post(req)
            lines = eng.service(ch)
            steps.append({"req": req, "posted": posted, "channel": ch, "lines": [line_json(eng, ln) for ln in lines]})
        out["sequences"].append({"seed": seed, "steps": steps, "rng": eng.rng.s})
    text = open(NASCRIPT, encoding="latin-1").read()
    for v in ["ALASKA", "SNOW", "MESA", "ELYSIUM", "ALOHA", "MERQUER", "MEGAPLE", "UNTRACK", "PIPE", "GARI"]:
        scripts = S.parse_nascript(text, v, "_ENG")
        for sid in sorted(scripts):
            eng = engine(seed=1, names=("mc", "narr"))
            els = []
            for req in S.narration_requests(scripts[sid], 0):
                eng.post(req)
                lines = eng.service(0)
                els.append({"req": req, "lines": [line_json(eng, ln) for ln in lines]})
            out["nascript"].append({"venue": v, "script": sid, "items": scripts[sid], "elements": els})
    for i in range(0x51):
        cat, item = struct.unpack_from("<II", EXE, off(0x343710 + 8 * i))
        eng = engine(seed=i, names=("fe",))
        ln = eng.resolve([0, cat, item, 1])
        out["fe"].append({"id": i, "req": [0, cat, item, 1], "line": line_json(eng, ln) if ln else None})
    for path in ("live/ride_session_1/spawn_ram.bin", "live/ride_session_2/spawn_ram.bin", "ram/paused_race_1.bin"):
        b = open(os.path.join(TR, path), "rb").read()
        state = [struct.unpack_from("<I", b, 0x349528 + 4 * k)[0] for k in range(6)]
        for s in range(16):
            o = 0x348EAC + 0x60 * s
            req = list(struct.unpack_from("<20I", b, o + 0x10))
            if not req[0]:
                continue
            eng = engine(rng_state=state)
            ln = eng.resolve(req)
            out["ram"].append({"snapshot": path, "slot": s, "rng": state, "req": req, "line": line_json(eng, ln) if ln else None})
    for seed in (0, 1, 7, 123456):
        r = S.SpeechRng(seed)
        vals = [r.next() for _ in range(64)]
        n = S.NoRepeatRandom(S.SpeechRng(seed))
        draws = [n.draw(k % 13 + 1, k % 3) for k in range(64)]
        out["rng"].append({"seed": seed, "values": vals, "draws": draws})
    os.makedirs(os.path.dirname(OUT), exist_ok=True)
    json.dump(out, open(OUT, "w"))
    nlines = sum(len(st["lines"]) for sq in out["sequences"] for st in sq["steps"])
    print(f"speech reference: {len(out['sequences'])} sequences ({nlines} lines), {len(out['nascript'])} scripts "
          f"({sum(len(s['elements']) for s in out['nascript'])} elements), {sum(1 for f in out['fe'] if f['line'])}/81 FE ids, "
          f"{len(out['ram'])} RAM requests -> {OUT}")


if __name__ == "__main__":
    main()
