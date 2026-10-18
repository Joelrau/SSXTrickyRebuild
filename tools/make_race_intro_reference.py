"""Reference values for SSX.Race.Intro.
Usage:
    python tools/make_race_intro_reference.py   # -> Unreal/SSXTrickyRebuild/SimData/race_intro_reference.json
"""
import libpath  # noqa: F401,E402  (tools/lib on sys.path)
import json
import os
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
REPO = os.path.dirname(HERE)
OUT = os.path.join(REPO, "Unreal", "SSXTrickyRebuild", "SimData", "race_intro_reference.json")
import race_intro as RI  # noqa: E402


class ScriptEnv(RI.Env):
    def __init__(self, busy, skips, loading, help_needed, keys):
        self.frame = 0
        self.busy = busy
        self.left = 0
        self.skips = set(skips)
        self.loading_frames = loading
        self.help = help_needed
        self.keys = keys
        self.log = []

    def loading(self):
        return self.frame < self.loading_frames

    def director_running(self):
        return self.left > 0

    def skip_pressed(self):
        if self.frame in self.skips:
            self.skips.discard(self.frame)
            return True
        return False

    def line_key(self, line_id):
        return self.keys.get(line_id, line_id)

    def help_needed(self):
        return self.help

    def act(self, name, *args):
        arg, text = 0, ""
        for a in args:
            if isinstance(a, str):
                text = a
            elif isinstance(a, bool):
                arg = int(a)
            elif isinstance(a, float):
                arg = int(round(a * 1000))
            else:
                arg = int(a)
        if name == "director_play" and text != "PreLoad":
            self.left = self.busy
        if name == "director_reset":
            self.left = 0
        self.log.append(f"{name}:{arg}:{text}")


def run(case):
    env = ScriptEnv(case["busy"], case["skips"], case["loading"], case["help"], {int(k): v for k, v in case["keys"].items()})
    slots = [RI.IntroSlot(t, [tuple(l) for l in lines]) for t, lines in zip(case["types"], case["lines"])]
    h = RI.PreRaceHandler(env, case["mode"], case["venue"], slots, case["entry"])
    frames = [{"step": h.step, "log": list(env.log)}]
    for f in range(1, 400):
        env.frame = f
        env.log = []
        if env.left > 0:
            env.left -= 1
        s = h.update()
        frames.append({"step": s, "log": list(env.log)})
        if s == RI.DONE:
            break
    return frames


def main():
    cases = []
    lines_a = [[0, 5, 0, 1025], [1, 3, 0, 1026], [2, 9, 0, -1], [0, 7, 0, 500], [1, 2, 0, 1100], [2, 4, 0, 1200]]
    for entry in ("normal", "next_heat", "restart"):
        for mode, venue in ((4, 0), (1, 0), (2, 7), (3, 5)):
            for types in ((0, 9, 21), (0x39, 0x39, 15), (0x39, 0x39, 0x39), (57, 57, 15)):
                for busy, skips, loading, help_ in ((0, [], 0, False), (40, [], 0, False), (120, [30, 31, 200], 0, False),
                                                    (60, [10, 75], 3, True), (30, [5, 50, 51], 0, False)):
                    cases.append({"entry": entry, "mode": mode, "venue": venue, "types": list(types),
                                  "lines": [lines_a if t != 0x39 else [] for t in types], "keys": {"9": -1, "2": 50},
                                  "busy": busy, "skips": skips, "loading": loading, "help": help_})
    for c in cases:
        c["frames"] = run(c)
    countdown = []
    for e in (211, 1, 100):
        t = RI.countdown_timeline(e)
        countdown.append({"enter": e, "beeps": t["beeps"], "go": t["go"], "race_enter": t["race_enter"],
                          "lights": t["light_offsets"], "clock_279": t["clock"](279)})
    out = {"runs": cases, "countdown": countdown,
           "live_session_2": {"enter": 211, "beeps": [240, 270, 300, 330], "go": 360, "race_enter": 361, "clock_279": -162},
           "script_names": list(RI.SCRIPT_NAMES)}
    os.makedirs(os.path.dirname(OUT), exist_ok=True)
    json.dump(out, open(OUT, "w"))
    print(f"race intro reference: {len(cases)} runs, {sum(len(c['frames']) for c in cases)} updates -> {OUT}")


if __name__ == "__main__":
    main()
