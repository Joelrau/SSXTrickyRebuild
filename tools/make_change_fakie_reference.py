"""Live ChangeFakie segments for the SSX.Ground.ChangeFakie test.
Usage:  python tools/make_change_fakie_reference.py
Reads   SSX_TRACES/live/ride_session_{1,2}/summary.csv
Writes  Unreal/SSXTrickyRebuild/SimData/change_fakie_live.csv (git-ignored derived data): one row per
        ChangeFakie (control 4) segment: session, rider, entry frame, control before, yaw jump in the
"""
import libpath  # noqa: F401,E402  (tools/lib on sys.path)
import traces  # noqa: E402

traces.require()
TR = traces.root()

import csv
import math
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
TRACES = TR / "live"
OUT = ROOT / "Unreal" / "SSXTrickyRebuild" / "SimData" / "change_fakie_live.csv"


def nose_yaw(r):
    w, x, y, z = (float(r[k]) for k in ("quat_w", "quat_x", "quat_y", "quat_z"))
    return math.degrees(math.atan2(1 - 2 * (x * x + z * z), 2 * (x * y - w * z)))


def main():
    out = []
    for sess in ("ride_session_1", "ride_session_2"):
        path = TRACES / sess / "summary.csv"
        if not path.exists():
            continue
        riders = {}
        with open(path, newline="") as fh:
            for r in csv.DictReader(fh):
                riders.setdefault(r["rider"], []).append(r)
        for rid, rs in riders.items():
            rs.sort(key=lambda r: int(r["frame"]))
            i = 1
            while i < len(rs):
                if rs[i]["control"] == "4" and rs[i - 1]["control"] != "4":
                    j = i
                    while j < len(rs) and rs[j]["control"] == "4":
                        j += 1
                    dyaw = (nose_yaw(rs[i]) - nose_yaw(rs[i - 1]) + 180.0) % 360.0 - 180.0
                    after = rs[j] if j < len(rs) else None
                    out.append([sess, rid, rs[i]["frame"], rs[i - 1]["control"], f"{abs(dyaw):.3f}", j - i,
                                after["control"] if after else -1, after["motion"] if after else -1])
                    i = j
                else:
                    i += 1
    with open(OUT, "w", newline="") as fh:
        w = csv.writer(fh)
        w.writerow(["session", "rider", "frame", "control_before", "yaw_jump_deg", "frames", "control_after", "motion_after"])
        w.writerows(out)
    print(f"{len(out)} segments -> {OUT}")


if __name__ == "__main__":
    main()
