"""Race loading screen data -> converted/frontend/loading/ (git-ignored game data).
    python tools/export_loading_screen.py
"""
# FUN_001F9A80 / 0x245CB8 / 0x245EC0
import libpath  # noqa: F401,E402  (tools/lib on sys.path)
import json
import os
import struct
import sys
from pathlib import Path

here = Path(__file__).resolve().parent
repo = here.parent
sys.path.insert(0, str(here))
import eabig  # noqa: E402
import sshtex  # noqa: E402

sys.path.insert(0, str(here / 'lib'))
from slus import EXE, off  # noqa: E402

OUT = repo / 'converted' / 'frontend' / 'loading'
TEX = repo / 'extracted' / 'DATA' / 'TEXTURES'
PS2LOAD = repo / 'unpacked' / 'TEXTURES' / 'PS2LOAD'

VFUNC11_LO, VFUNC11_HI = 0x245EC0, 0x246700        # FUN_00246700
REC_LO, REC_SIZE, REC_COUNT = 0xB14, 0x1C, 28   # loader +0xB14 .. +0xE24 (+0xE24 = the "sprites set" flag)
SHEET_PAGES = {0x15E8: 'loa1', 0x15EC: 'loa2', 0x15F0: 'lcon'}
S0 = 16
# 0x24C928 / 0x24D168
QUIT_LO, QUIT_HI, QUIT_REC_LO = 0x24C928, 0x24D168, 0x1B50


def scan_records(lo=VFUNC11_LO, hi=VFUNC11_HI, rec_lo=REC_LO):
    REC_LO = rec_lo  # noqa: N806 (the record window of this loader)
    gpr, fpr, store = {}, {}, {}
    for va in range(lo, hi, 4):
        ins = struct.unpack_from('<I', EXE, off(va))[0]
        op, rs, rt, imm = ins >> 26, (ins >> 21) & 31, (ins >> 16) & 31, ins & 0xFFFF
        simm = imm - 0x10000 if imm & 0x8000 else imm
        if op == 0x0F:
            gpr[rt] = imm << 16
        elif op == 0x0D:
            v = gpr.get(rs)
            gpr[rt] = (v | imm) if isinstance(v, int) else None
        elif op == 0x11 and rs == 4:
            fpr[(ins >> 11) & 31] = gpr.get(rt)
        elif op == 0x23:
            gpr[rt] = SHEET_PAGES.get(simm)
        elif op in (0x2B, 0x39) and rs == S0:
            v = gpr.get(rt) if op == 0x2B else fpr.get(rt)
            if REC_LO <= simm < REC_LO + REC_SIZE * REC_COUNT:
                store[simm] = v
    recs = []
    for k in range(REC_COUNT):
        base = REC_LO + REC_SIZE * k
        vals = [store.get(base + 4 * i) for i in range(7)]
        if vals[0] is None or any(v is None for v in vals[1:]):
            raise SystemExit('record %d (+0x%X) incomplete: %r' % (k, base, vals))
        f = [struct.unpack('<f', struct.pack('<I', v))[0] for v in vals[1:]]
        w, h, v0, u0, u1, v1 = f
        recs.append(dict(id=k, offset='0x%X' % base, page=vals[0], w=w, h=h, u0=u0, v0=v0, u1=u1, v1=v1))
    return recs


def main():
    OUT.mkdir(parents=True, exist_ok=True)
    sshtex.convert(TEX / 'LOADING.SSH', OUT / 'LOADING')
    if not PS2LOAD.exists():
        n = eabig.unpack(TEX / 'PS2LOAD.BIG', PS2LOAD)
        print('unpacked PS2LOAD.BIG (%d files)' % n)
    pictures = {}
    for ssh in sorted(PS2LOAD.glob('*.ssh')):
        sshtex.convert(ssh, OUT / ssh.stem)
        pngs = sorted((OUT / ssh.stem).glob('*.png'))
        if pngs:
            pictures[ssh.stem.lower()] = '%s/%s' % (ssh.stem, pngs[0].name)
    pages = {}
    for png in sorted((OUT / 'LOADING').glob('*.png')):
        pages[png.stem.split('_', 1)[1]] = 'LOADING/%s' % png.name
    recs = scan_records()
    with open(OUT / 'loading_screen.json', 'w') as f:
        json.dump(dict(source='c1PTrackLoad::vfunc_11 0x245EC0 records, loading.ssh, ps2load.big',
                       pages=pages, pictures=pictures, sprites=recs), f, indent=1)
    # 0x249948 / 0x24B320 / 0x24ECA8 / 0x24D9B0
    strip = lambda r: {k: v for k, v in r.items() if k != 'offset'}  # noqa: E731
    others = [('cFELoadFromQuit', QUIT_LO, QUIT_HI, QUIT_REC_LO), ('c1PFELoad', 0x249948, 0x24A188, 0xC04),
              ('c2PFELoad', 0x24B320, 0x24BB60, 0xE5C), ('cRiderRankingLoad', 0x24ECA8, 0x24F4E8, 0xA60),
              ('cTrickTutorialLoad', 0x24D9B0, 0x24E1F0, 0x6DC)]
    for name, lo, hi, rec_lo in others:
        if [strip(r) for r in recs] != [strip(q) for q in scan_records(lo, hi, rec_lo)]:
            raise SystemExit('%s records differ from the c1PTrackLoad ones: the port needs its own table' % name)
    print('wrote %d sprites, %d pages, %d pictures to %s (%s records identical)'
          % (len(recs), len(pages), len(pictures), OUT, ' / '.join(o[0] for o in others)))


if __name__ == '__main__':
    main()
