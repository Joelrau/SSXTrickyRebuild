"""Validate held / released flips of models/air.py (0x100908) on live traces.

Each case replays one flip from its first air frame: the take-off seed p comes from the first recorded steps
(flip_angle +0x1D4 after n updates = p dt (1 + k + .. k^(n-1)), k = the stat decay), the envelope is
envelope_from_seed(p) (5.2), the flip direction is held (target -flip_in |lo + hi|, 5.4) up to the release step R,
then the stick is neutral (settle, 5.5). `stagger` = updates before R where the flip direction is already let go
but the spin direction is still held (a d-pad diagonal released one frame apart): the phase stays active and the
flip rate slews toward 0 by 75 rad/s^2 dt. R is fitted; every R before the 40 deg band gives the same curve while
the envelope bound is above its floor (the settle error c -+ 2 pi is clamped to the same bound the held target
is). Each recorded sample is compared with the model at its own step, allowing the sampler's +-1 frame skew
(repeated / skipped frames in ride_session_1/2; a sample of 0 after the take-off is stale and skipped).

  rs1_joe_backflip      JOE f1618: back flip at 307 deg/s decaying by k per update, d-pad let go (Down one frame
                        before Right) at f1676, flies on at the envelope bound into the 40 deg band, eases toward
                        -360 and touches down at -353.1 deg: the air ends before the settle arrives.
  rs1_eddie_tripleflip  Eddie f797: three flips held at 500 deg/s decaying to 172 deg/s, let go 15 deg before
                        -1080, settles onto -1079.2 deg.

usage: python scripts/heldflip_validate.py [--dump]   (exit 1 on a mismatch)
"""
import csv
import json
import math
import os
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
sys.path.insert(0, os.path.join(ROOT, "models"))
sys.path.insert(0, ROOT)
import air  # noqa: E402
import traces  # noqa: E402

CASES = (
    # name, session, rider, first air frame of the flip (angle still 0), last airborne frame, stagger
    ("rs1_joe_backflip", "ride_session_1", "0xdd8cc0", 1618, 1735, 1),
    ("rs1_eddie_tripleflip", "ride_session_1", "0xbdaf30", 797, 1067, 0),
)
TOL_DEG = 0.25


def load(session, rider):
    meta = json.load(open(traces.path("live", session, "meta.json")))
    prof = bytes.fromhex(meta["profiles"][rider])
    rows = {}
    for r in csv.DictReader(open(traces.path("live", session, "summary.csv"))):
        if r["rider"] == rider:
            rows[int(r["frame"])] = float(r["flip"])
    return prof, rows


def flip_dir(p):
    return -1 if p > 0 else 1        # target = -flip_in |lo + hi| has the seed's sign


def inputs(p, release, stagger, steps):
    """(spin_in, flip_in) per update."""
    f = flip_dir(p)
    out = []
    for k in range(steps):
        if k < release - stagger:
            out.append((0, f))
        elif k < release:
            out.append((1, 0))       # flip let go, spin direction still held: active phase
        else:
            out.append((0, 0))
    return out


def replay(stat, p, schedule):
    """flip_angle after each update (seed p rad/s on the flip axis, no spin seed)."""
    stats = air.RiderStats(spin=stat)
    b = air.AirBody(pos=[0, 0, 0], vel=[0, 0, 0])
    st = air.SpinState(phase=1, flip_rate=p)
    st.flip_hi, st.flip_lo, st.flip_floor = air.envelope_from_seed(p)
    sc = air.TrickScorer()
    out = []
    for s_in, f_in in schedule:
        st = air.spin_update(b, st, air.SpinInput(spin=s_in, flip=f_in), stats, sc, [0, 0, 1])
        out.append(b.flip_angle)
    return out


def score(model, samples):
    """max |error| (deg) of the samples (step, value) against the model at step-1 .. step+1."""
    worst = 0.0
    for k, v in samples:
        e = min(abs(v - model[j]) for j in (k - 1, k, k + 1) if 0 <= j < len(model))
        worst = max(worst, e)
    return math.degrees(worst)


def case_result(session, rider, f0, f1, stagger):
    """stat, seed, samples [(step, rad)], (max err deg, release step, schedule, model angles)."""
    prof, flip = load(session, rider)
    stat = prof[0x1A]
    k = air.RiderStats(spin=stat).decay
    fn = next(f for f in range(f0 + 1, f1) if flip.get(f, 0.0) != 0.0)
    n = fn - f0
    p = flip[fn] * 60.0 * (1.0 - k) / (1.0 - k ** n)
    steps = f1 - f0
    samples = [(f - f0 - 1, flip[f]) for f in range(f0 + 1, f1 + 1) if flip.get(f, 0.0) != 0.0]
    best = None
    for release in range(stagger + 1, steps + 1):
        sch = inputs(p, release, stagger, steps)
        m = replay(stat, p, sch)
        e = score(m, samples)
        if best is None or e < best[0] - 1e-9:
            best = (e, release, sch, m)
    return stat, p, samples, best


def main():
    bad = 0
    for name, session, rider, f0, f1, stagger in CASES:
        stat, p, samples, (err, release, sch, m) = case_result(session, rider, f0, f1, stagger)
        held = replay(stat, p, [(0, flip_dir(p))] * (f1 - f0))
        print("%-21s stat %3d seed %+7.1f deg/s  let go at update %3d (stagger %d)  touchdown: trace %+8.1f model %+8.1f"
              " (held all air %+8.1f)  max err %.3f deg" % (name, stat, math.degrees(p), release, stagger,
                                                           math.degrees(samples[-1][1]), math.degrees(m[-1]),
                                                           math.degrees(held[-1]), err))
        if "--dump" in sys.argv:
            for k, v in samples:
                print("   step %3d trace %+8.2f model %+8.2f" % (k, math.degrees(v), math.degrees(m[k])))
        if err > TOL_DEG:
            bad += 1
    print("OK" if not bad else "%d case(s) above %.2f deg" % (bad, TOL_DEG))
    return 1 if bad else 0


if __name__ == "__main__":
    sys.exit(main())
