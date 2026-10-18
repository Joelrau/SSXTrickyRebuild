"""Decoder for the camera section of SSX Tricky .pbd files (used for the 62 front-end cameras in
unpacked/MODELS/SSXFE/data/models/ssxfe.pbd; names come from the matching .map 'CAMERAS' section, UID = index).

pbd header: counts[...,'cameras'] and offsets 'camera_pointer' (int32[n], relative to 'cameras') - see tools/pbd.py.
Camera record (offsets from record start; little endian):
  +0x00 u32 recordSize
  +0x04 f32 pos[3]        rest position, game units (cm), Z up
  +0x10 f32 rot[3]        rest rotation, radians (Maya export; X-90deg tilt converts Maya Y-up to game Z-up)
  +0x1C u32 0
  +0x20 f32 focalLength (mm)     (FOV = 2*atan(8.997951/focal) [code: cFE3DCameraScripted::vfunc_11 0x26AC98])
  +0x24 f32 1.5 (?)  +0x28 f32 hFilmAperture 1.417 (in)  +0x2C f32 vFilmAperture 0.945 (in)
  +0x30 f32 lensSqueeze 1.0     +0x34 f32 10000 (far clip?)
  +0x38..+0x53 misc (TODO)       +0x54 u32 animBlockOffset (always 0x58)
  anim block (at +animBlockOffset = B):
  B+0x00 f32 pos[3], rot[3] (degrees? no: radians copy of rest pose)
  B+0x18 u32 channelMask     B+0x1C u32 nTracks (= popcount(mask))
  B+0x20 u32 offs[nTracks+1] (offs[0] = header size 0x24; offs[1..n] = track offsets from B, in channel-bit order)
  track: u32 nSeg, u32 8, then nSeg x {f32 c3,c2,c1,c0, tStart, tEnd}; value(t) = ((c3 t + c2) t + c1) t + c0 with
         t = absolute time in seconds (segments are continuous at boundaries); duration = last tEnd.
  channel bit -> runtime field (cam+0x68+4*bit) [code: 0x26AC98 reads +0x68 pos, +0x74 rot (deg->rad), +0x94 focal]:
     0 posX 1 posY 2 posZ 3 rotX(deg) 4 rotY(deg) 5 rotZ(deg) 6-8 unused here 9 hFilmAperture 10 vFilmAperture
     11 focalLength 12 lensSqueeze 13 fStop 14 focusDistance 15 shutterAngle(rad)   (Maya camera attribute set)
  View matrix built as: R = Rz(rotZ) * Rx(rotX - 90deg) [* Ry(rotY), see code], camera at -pos (code 0x26AC98).

    python scripts/fe_cameras.py [pbd] [map] [--json out.json] [--sample fps]
"""
import struct, sys, json, re, os
REB = os.path.normpath(os.path.join(os.path.dirname(os.path.abspath(__file__)), '..', '..', '..'))
PBD = REB + r'\unpacked\MODELS\SSXFE\data\models\ssxfe.pbd'
MAP = REB + r'\unpacked\MODELS\SSXFE\data\models\ssxfe.map'
CH = {0: 'posX', 1: 'posY', 2: 'posZ', 3: 'rotX', 4: 'rotY', 5: 'rotZ', 9: 'hAperture', 10: 'vAperture',
      11: 'focal', 12: 'lensSqueeze', 13: 'fStop', 14: 'focusDist', 15: 'shutterAngle'}
HEADER_COUNTS = 15  # see tools/pbd.py

def map_cameras(path):
    names = {}
    sec = False
    for line in open(path, encoding='latin-1'):
        if 'CAMERAS BEGIN' in line: sec = True; continue
        if 'CAMERAS END' in line: break
        if sec and line.strip() and not line.startswith('#'):
            p = line.split()
            names[int(p[1])] = p[0]
    return names

def load(pbd=PBD, mapf=MAP):
    d = open(pbd, 'rb').read()
    counts = struct.unpack_from('<15i', d, 4)
    offs = struct.unpack_from('<18i', d, 4 + 60)
    n = counts[13]; cptr, cbase = offs[14], offs[15]
    ptrs = struct.unpack_from('<%di' % n, d, cptr)
    names = map_cameras(mapf) if mapf and os.path.exists(mapf) else {}
    cams = []
    for ci, p in enumerate(ptrs):
        o = cbase + p
        F = lambda k, c=1: struct.unpack_from('<%df' % c, d, o + k)
        I = lambda k: struct.unpack_from('<I', d, o + k)[0]
        cam = dict(index=ci, name=names.get(ci, 'cam%d' % ci), size=I(0), pos=F(4, 3), rot=F(0x10, 3),
                   focal=F(0x20)[0], unk24=F(0x24)[0], hAperture=F(0x28)[0], vAperture=F(0x2C)[0],
                   lensSqueeze=F(0x30)[0], far=F(0x34)[0])
        B = I(0x54); mask = I(B + 0x18); nt = I(B + 0x1C)
        toffs = [I(B + 0x20 + 4 * j) for j in range(nt + 1)]
        bits = [b for b in range(32) if mask >> b & 1]
        tracks = {}
        dur = 0.0
        for b, to in zip(bits, toffs[1:]):
            k = B + to; ns = I(k)
            segs = [F(k + 8 + 24 * m, 6) for m in range(ns)]
            tracks[CH.get(b, 'bit%d' % b)] = [list(s) for s in segs]
            if segs: dur = max(dur, segs[-1][5])
        cam.update(channelMask=mask, duration=dur, tracks=tracks)
        cams.append(cam)
    return cams

def value(segs, t):
    for s in segs:
        if t <= s[5] or s is segs[-1]:
            c3, c2, c1, c0 = s[:4]
            return ((c3 * t + c2) * t + c1) * t + c0

if __name__ == '__main__':
    a = [x for x in sys.argv[1:] if not x.startswith('--')]
    cams = load(*(a[:2] if a else []))
    js = sys.argv.index('--json') if '--json' in sys.argv else -1
    for c in cams:
        st = {k: round(value(v, 0.0), 2) for k, v in c['tracks'].items()}
        en = {k: round(value(v, c['duration']), 2) for k, v in c['tracks'].items()}
        print('%2d %-44s dur %.3fs mask %#06x start %s end %s' % (c['index'], c['name'], c['duration'], c['channelMask'],
              [st.get(k) for k in ('posX', 'posY', 'posZ', 'rotX', 'rotY', 'rotZ', 'focal')],
              [en.get(k) for k in ('posX', 'posY', 'posZ', 'rotX', 'rotY', 'rotZ', 'focal')]))
    if js > 0:
        json.dump(cams, open(sys.argv[js + 1], 'w'), indent=1)
