"""Reference data for SSX.Air.DemoFlat, generated from tools/lib/models/air.py.
Usage:  python tools/make_air_reference.py
Writes  Unreal/SSXTrickyRebuild/SimData/air_demo.csv   one row per run_jump() case of the model's flat-world demo
                                                 (launch (0, 1500, 900), rider stats 128, spin direction +1):
                                                 prewind spin, stick-held frames, airtime frames, apex,
"""
import libpath  # noqa: F401,E402  (tools/lib on sys.path)
import csv
import sys
from pathlib import Path

import numpy as np

ROOT = Path(__file__).resolve().parent.parent
import air  # noqa: E402

SIMDATA = ROOT / "Unreal" / "SSXTrickyRebuild" / "SimData"
LAUNCH = (0.0, 1500.0, 900.0)
CASES = (   # (prewind spin, stick-held frames)
    (1.0, 0), (1.0, 20), (1.0, 45), (1.0, 90),
    (0.0, 40),   # late spin: no prewind, stick held -> late trick re-seed
)


def main():
    SIMDATA.mkdir(parents=True, exist_ok=True)
    out = SIMDATA / "air_demo.csv"
    with open(out, "w", newline="") as fh:
        w = csv.writer(fh)
        w.writerow(["prewind", "hold", "frames", "apex", "total_spin_deg", "spins180", "reward", "meter", "landing"])
        for prewind, hold in CASES:
            sim, frames = air.run_jump(LAUNCH, prewind_spin=prewind, hold_frames=hold, spin_dir=1, stats=air.RiderStats())
            w.writerow([repr(prewind), hold, frames, repr(float(sim.apex_z)), repr(float(sim.scorer.total_spin_deg)),
                        sim.last_summary["spins_180"], repr(float(sim.last_reward)), repr(float(sim.boost_meter)),
                        sim.landed])
            print(f"prewind {prewind} hold {hold:3d}: {frames} f, total spin {sim.scorer.total_spin_deg:.2f} deg, "
                  f"half-turns {sim.last_summary['spins_180']}, reward {sim.last_reward:.5f}, {sim.landed}")
    print(f"wrote {out}")


if __name__ == "__main__":
    main()
