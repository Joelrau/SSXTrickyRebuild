"""AI reset request / trick plan reference for SSX.AI.State -> Unreal/SSXTrickyRebuild/SimData/ai_state_reference.json (git-ignored).
    python tools/make_ai_state_reference.py [out.json]
"""
import libpath  # noqa: F401,E402  (tools/lib on sys.path)
import traces  # noqa: E402

traces.require()

import json  # noqa: E402
import os  # noqa: E402
import sys  # noqa: E402

import ai_state_trace as ai  # noqa: E402

here = os.path.dirname(os.path.abspath(__file__))
repo = os.path.normpath(os.path.join(here, '..'))
out = sys.argv[1] if len(sys.argv) > 1 else os.path.join(repo, 'Unreal', 'SSXTrickyRebuild', 'SimData', 'ai_state_reference.json')


def main():
    res = [ai.run(n) for n in ai.TRACES]
    with open(out, 'w', encoding='utf-8') as f:
        f.write(json.dumps(res, indent=1, default=lambda o: o.item() if hasattr(o, "item") else str(o)))
    print('wrote', out)


if __name__ == '__main__':
    main()
