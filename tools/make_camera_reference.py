"""Reference sequences for SSX.Camera.* -> Unreal/SSXTrickyRebuild/SimData/cameras/camera_reference.csv (git-ignored).
    python tools/make_camera_reference.py [out.csv]
"""
# 0x174D28
import libpath  # noqa: F401,E402  (tools/lib on sys.path)
import traces  # noqa: E402

traces.require()
TR = traces.root()

import json
import math
import os
import struct
import sys

here = os.path.dirname(os.path.abspath(__file__))
repo = os.path.normpath(os.path.join(here, '..'))
out = sys.argv[1] if len(sys.argv) > 1 else os.path.join(repo, 'Unreal', 'SSXTrickyRebuild', 'SimData', 'cameras',
                                                          'camera_reference.csv')
import camera as cm  # noqa: E402

cml_dir = os.path.join(repo, 'extracted', 'DATA', 'CAMERA')


def load_set(names):
    defs, scripts = {}, {}
    for n in reversed(names):
        d = cm.load_cml(os.path.join(cml_dir, n.upper() + '.CML'))
        defs.update(d['cameras']); scripts.update(d['scripts'])
    return defs, scripts


def fmt(x): return '%.9g' % x


lines = []


def stage_areas(track):
    """{area: 16 floats} of SimData/cameras/stage_areas.json (tools/export_cameras.py) for a rebuild track id."""
    path = os.path.join(os.path.dirname(out), 'stage_areas.json')
    areas = json.load(open(path)).get(track, {}) if track else {}
    return {int(k): v['matrix'] for k, v in areas.items()}


def run(seq, cmls, frames, track=''):
    """frames: list of (rider, cut, view, script, world_z). track: stage areas (C++: LoadStageAreas(track))."""
    defs, scripts = load_set(cmls)
    cam = cm.Camera(defs=defs, scripts=scripts, stage_areas=stage_areas(track))
    lines.append('#seq,%s,%s,%s' % (seq, '|'.join(cmls), track))
    for k, (r, cut, view, script, wz) in enumerate(frames):
        cam.world = None if wz is None else cm.plane_world(wz)
        if view >= 0: cam.set_view_index(view)
        if cut: cam.mark_cut()
        if script: cam.play_script(script)
        cam.step(r)
        C, V = cam.C, cam.V
        row = [seq, str(k), str(cut), str(view), script or '-', 'none' if wz is None else fmt(wz)]
        row += [fmt(x) for x in r.velocity] + [fmt(x) for x in r.forward] + [fmt(r.yaw)]
        row += [fmt(x) for x in r.anchor] + [fmt(r.boost_strength), str(r.motion), str(r.control)]
        row += [fmt(x) for x in r.ground_normal] + [str(r.surface), fmt(r.pred_time_left)]
        row += [fmt(x) for x in r.pred_land_velocity] + [fmt(x) for x in r.pred_land_normal]
        row += [fmt(x) for x in C[:8]] + [fmt(V[0]), fmt(V[1]), fmt(V[2]), fmt(V[19]), fmt(V[20]), fmt(cam.blend)]
        lines.append(','.join(row))


def trace_frames(path, rider_index=4, start=0, count=3000):
    R, n_riders = 2896, 6
    FR = 8 + n_riders * R
    data = open(path, 'rb').read()

    def off(o):
        return o if o < 0x600 else (0x600 + o - 0x4600 if o < 0x4B00 else 0xB00 + o - 0x57B0)
    frames, prev_anchor = [], None
    for k in range(start, min(start + count, len(data) // FR)):
        b = 8 + k * FR + rider_index * R
        f = lambda o, n=1: struct.unpack_from('<%df' % n, data, b + off(o))
        i = lambda o: struct.unpack_from('<i', data, b + off(o))[0]
        r = cm.RiderInput(velocity=f(0x150, 3), forward=f(0x180, 3), yaw=f(0x1B0)[0], anchor=f(0x4AD0, 3),
                          boost_strength=f(0x130)[0], motion=i(0x424), control=i(0x428),
                          ground_normal=f(0x2A0, 3), surface=i(0x290), pred_time_left=f(0x57B8)[0],
                          pred_land_velocity=f(0x57D0, 3), pred_land_normal=f(0x57E0, 3))
        cut = int(prev_anchor is not None and cm.v_len(cm.v_sub(r.anchor, prev_anchor)) > 1000.0)
        prev_anchor = r.anchor
        frames.append((r, cut, -1, None, None))
    return frames


def synth_frames():
    frames = []
    pos = [0.0, 0.0, 300.0]
    for k in range(900):
        t = k / 60.0
        heading = 0.6 * math.sin(t * 0.7)
        speed = 1500.0 if k > 20 else 5.0 * k
        vz = -300.0 * math.cos(t * 0.5)
        vel = (speed * math.sin(-heading), speed * math.cos(heading), vz)
        motion, control, ptl, plv = 2, 3, 0.0, (0.0, 0.0, 0.0)
        if 200 <= k < 260: motion, control, ptl, plv = 1, 13, (260 - k) / 60.0, (0.0, 1500.0, -2500.0)
        if 500 <= k < 620: motion, control = 5, 19
        if 620 <= k < 680: motion, control = 6, 20
        pos = [pos[0] + vel[0] / 60, pos[1] + vel[1] / 60, pos[2] + vel[2] / 60]
        gn = cm.v_norm((0.25 * math.sin(t), 0.1, 1.0))
        r = cm.RiderInput(velocity=vel, forward=cm.v_norm((math.sin(-heading - 0.3), math.cos(heading + 0.3), 0.0)),
                          yaw=heading, anchor=(pos[0], pos[1], pos[2] + 72.0), boost_strength=1.0 if 100 <= k < 180 else 0.0,
                          motion=motion, control=control, ground_normal=gn, surface=(k // 150) * 3 % 16,
                          pred_time_left=ptl, pred_land_velocity=plv, pred_land_normal=(0.0, 0.0, 1.0))
        view = {300: 2, 360: 3, 420: 0, 470: 1, 700: 1}.get(k, -1)
        wz = None
        if 120 <= k < 200: wz = pos[2] + 72.0 + 110.0
        if 300 <= k < 360: wz = pos[2] + 72.0 + 60.0
        if 520 <= k < 600: wz = pos[2] + 72.0 + 30.0
        if 720 <= k < 800: wz = pos[2] + 120.0
        if wz is not None: wz = round(wz, 3)
        frames.append((r, int(k == 840), view, None, wz))
    return frames


def script_frames(trace):
    return [(r, 0, -1, 'PreLoad' if k == 0 else None, None) for k, (r, *_rest) in enumerate(trace[:720])]


tr = trace_frames(os.path.join(TR, 'live', 'ride_session_1', 'frames.bin'))
run('trace', ['commonob', 'scripts'], tr)
run('synth', ['commonob', 'scripts'], synth_frames())
run('script', ['aloha', 'commonob', 'scripts'], script_frames(tr))


def stage_frames(trace, scripts, every=600):
    return [(r, 0, -1, scripts[k // every] if k % every == 0 and k // every < len(scripts) else None, None)
            for k, (r, *_rest) in enumerate(trace[:every * len(scripts)])]


for track, course in (('aloha', 'aloha'), ('merquer', 'merqury'), ('pipe', 'pipedrea')):
    run('stage_' + track, [course, 'commonob', 'scripts'], stage_frames(tr, ['STG_VEN_1', 'STG_COM_1a']), track)
os.makedirs(os.path.dirname(out), exist_ok=True)
with open(out, 'w') as f:
    f.write('# seq,frame,cut,view,script,world_z,vel3,fwd3,yaw,anchor3,boost,motion,control,gn3,surface,'
            'pred_time_left,pred_land_vel3,pred_land_normal3,C0..C7,V0..V2,V19,V20,blend\n')
    f.write('\n'.join(lines) + '\n')
print('wrote %d rows to %s' % (len(lines), out))
