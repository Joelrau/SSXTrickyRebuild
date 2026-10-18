"""Generate the Python-model reference CSV for the UE replay harness (SSX.Replay.* tests).
Usage:
  python tools/make_replay_reference.py                      # brod01
  python tools/make_replay_reference.py path/to/eddi05.inp   # any tutorial recording
Options:
"""
import libpath  # noqa: F401,E402  (tools/lib on sys.path)
import argparse
import os
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
REPO = os.path.dirname(HERE)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("inp", nargs="?", default=os.path.join(REPO, "unpacked", "TUTORIAL", "BROD", "data",
                                                           "tutorial", "brod01.inp"))
    ap.add_argument("--slope", type=float, default=15.0)
    ap.add_argument("--out", default=None)
    args = ap.parse_args()

    import boarder  # noqa: E402  (tools/lib/models)

    name = os.path.basename(args.inp)[:-4].lower()
    out = args.out or os.path.join(REPO, "Unreal", "SSXTrickyRebuild", "SimData", "replay",
                                   f"{name}_slope{int(round(args.slope))}.csv")
    world = boarder.SlopeWorld(args.slope, surface_id=1)
    state, rows = boarder.replay(args.inp, world, out)
    summ = boarder.summarize(state, rows)
    print(f"{os.path.basename(args.inp)} on a {args.slope:g} deg slope -> {out}")
    for k in ("frames", "max_speed_kmh", "jumps", "landings", "bails", "word_match_frames"):
        print(f"  {k:18s} {summ[k]}")


if __name__ == "__main__":
    main()
