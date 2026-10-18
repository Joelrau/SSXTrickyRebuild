"""Held / released flip reference for SSX.Air.HeldFlip -> Unreal/SSXTrickyRebuild/SimData/heldflip_reference.csv (git-ignored).
    python tools/make_heldflip_reference.py [out.csv]
"""
import libpath  # noqa: F401,E402  (tools/lib on sys.path)
import traces  # noqa: E402

traces.require()
TR = traces.root()

import os
import sys

here = os.path.dirname(os.path.abspath(__file__))
repo = os.path.normpath(os.path.join(here, '..'))
out = sys.argv[1] if len(sys.argv) > 1 else os.path.join(repo, 'Unreal', 'SSXTrickyRebuild', 'SimData', 'heldflip_reference.csv')
import heldflip_validate as hv  # noqa: E402


def main():
    lines = []
    for name, session, rider, f0, f1, stagger in hv.CASES:
        stat, p, samples, (err, release, sch, m) = hv.case_result(session, rider, f0, f1, stagger)
        trace = dict(samples)
        lines.append('case,%s,%d,%.9g,%d' % (name, stat, p, len(sch)))
        for k, (s_in, f_in) in enumerate(sch):
            t = ('%.9g' % trace[k]) if k in trace else ''
            lines.append('s,%d,%d,%d,%.9g,%s' % (k, s_in, f_in, m[k], t))
        print('%s: %d updates, let go at %d, model vs trace %.3f deg' % (name, len(sch), release, err))
    os.makedirs(os.path.dirname(out), exist_ok=True)
    with open(out, 'w', newline='\n') as fh:
        fh.write('\n'.join(lines) + '\n')
    print('wrote', out)


if __name__ == '__main__':
    main()
