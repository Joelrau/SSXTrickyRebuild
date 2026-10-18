"""Payload decoder for SSX Tricky camera script files DATA/CAMERA/*.CML.

Field meanings come from the code that reads
them; every field name below carries the function that consumes it. Unread fields stay 'unk_XXX'.

File layout (little-endian, unused bytes = 0xDEADC0ED):
  header: i32 h0 (unknown), i32 listHead[6] (type 0..5, -1 = empty), i32 rootNode (0x20)
          list lookup: FUN_0016e248(file, name, type) walks listHead[type] via rec+0x0
  record: i32 nextSameType, i32 nextInSameTreeNode, i32 dataOffset (always rec+0x20), u32 nameHash
          (FUN_0016df80), char name[16]; then the payload. The game uses the payload pointer directly
          (FUN_0016dda8 returns it).
  types (record size incl. 0x20 header):
     0 Camera        0x194  (FUN_0016e900 position part, FUN_001722e8 target part)
     1 Animation     0x070  (SCRIPTS.CML only; CamAnimation, FUN_00176850)       -- raw dump
     2 DirectorScript 0x394 (FUN_00175f50)
     3 Path          0x154  (CamPath: char ptName[20][15], i32 count @0x12C, f32 @0x130; FUN_00170610/001708e8/00170ca8)
     4 CameraRegion  0x1B4  (cBxCamera::vfunc_02 0x177078, FUN_00171610/00171500)
     5 Scene         0x1A0  (CamScene)                                            -- raw dump
  Files are searched in the order <course>.cml, commonob.cml, scripts.cml (FUN_0016e518, loader 0x1783E8).

Usage: python cml_payload.py FILE.CML [--type N] [--name NAME] [--json]
"""
import struct, sys, json

DEAD = 0xDEADC0ED
TYPE_NAMES = {0: 'Camera', 1: 'Animation', 2: 'DirectorScript', 3: 'Path', 4: 'CameraRegion', 5: 'Scene'}
DATA_SIZE = {0: 0x174, 1: 0x50, 2: 0x374, 3: 0x134, 4: 0x194, 5: 0x180}

CAMERA_FIELDS = {
    0x000: ('pos_attach_rider', 'i', 'FUN_0016e900: base point = rider anchor (entry+0x14) when !=0'),
    0x004: ('pos_rider_slot', 'i', 'FUN_0016e900: index into camera rider table (0x20 per entry)'),
    0x010: ('orbit_lag_enable', 'i', 'FUN_0016e900: lag orbit yaw/elev when !=0'),
    0x014: ('orbit_lag_div', 'f', 'FUN_0016e900: state += wrap(target-state)/div per frame'),
    0x018: ('pos_smooth_enable', 'i', 'FUN_0016e900 -> view+0x38; FUN_00174238 position filter'),
    0x01C: ('pos_smooth_param', 'i', 'FUN_0016e900 -> view+0x3C (int, >0 enables 0.8/0.2 filter in FUN_00174238)'),
    0x020: ('unk_view40_enable', 'i', 'FUN_0016e900 -> view+0x40; consumer TODO'),
    0x024: ('unk_view44_value', 'f', 'FUN_0016e900 -> view+0x44; consumer TODO'),
    0x028: ('anchor_use_bone', 'i', 'FUN_0016e900: replace anchor with bone/point below'),
    0x02C: ('bone_index', 'i', 'FUN_0016e900/001722e8: -1 rider+0x4A90 / -2 rider+0x2D0 contact pt / n skeleton matrix n'),
    0x030: ('stage_area', 'i', 'FUN_001777d0: 1 Mdl_StageArea_Start_0, 2 Mdl_StageArea_Finish_0 (base point in that model space)'),
    0x03C: ('base_x', 'f', 'FUN_0016e900: base offset X (cm)'),
    0x068: ('base_y', 'f', 'FUN_0016e900: base offset Y (cm)'),
    0x094: ('base_z', 'f', 'FUN_0016e900: base offset Z (cm)'),
    0x0B8: ('shake_time', 'f', 'FUN_0016e900: >0 -> random XY jitter, -= 1/60 per eval (runtime)'),
    0x0C4: ('shake_amp', 'f', 'FUN_0016e900: jitter amplitude (table 0x337798)'),
    0x0C8: ('orbit_enable', 'i', 'FUN_0016e900: orbit offset + collision when !=0'),
    0x0CC: ('orbit_dist', 'f', 'FUN_0016e900: orbit radius (cm)'),
    0x0D0: ('orbit_dist_boost', 'f', 'FUN_0016e900: + this * rider boost_strength (+0x130)'),
    0x0D4: ('yaw_from_heading', 'i', 'FUN_0016e900: yaw += -atan2(dir.x, dir.y) of smoothed rider dir'),
    0x0D8: ('yaw_from_bone', 'i', 'FUN_0016e900: yaw -= atan2(bone.x, bone.y)'),
    0x0DC: ('yaw_offset', 'f', 'FUN_0016e900: orbit yaw offset (rad), blended by FUN_00175450'),
    0x0E0: ('yaw_goofy_flip', 'i', 'FUN_0016e900: yaw = wrap(pi - yaw) + pi if rider_profile+0x40 (stance)'),
    0x0F8: ('elev_from_velocity', 'i', 'FUN_0016e900: elev += (pi/2 - angle_from_vertical(dir))/div'),
    0x0FC: ('elev_from_bone', 'i', 'FUN_0016e900: elev += atan2(bone.z, |bone.xy|)'),
    0x100: ('elev_base', 'f', 'FUN_0016e900: orbit elevation (rad); negative = camera above'),
    0x108: ('elev_clamp_enable', 'i', 'FUN_0016e900'),
    0x10C: ('elev_min', 'f', 'FUN_0016e900'),
    0x110: ('elev_max', 'f', 'FUN_0016e900'),
    0x114: ('elev_div_descending', 'f', 'FUN_0016e900: divisor when dir.z < 0'),
    0x118: ('elev_div_ascending', 'f', 'FUN_0016e900: divisor when dir.z >= 0'),
    0x11C: ('rt_self_ptr', 'i', 'runtime (FUN_0016dda8 writes)'),
    0x120: ('tgt_attach_rider', 'i', 'FUN_001722e8: 0 -> fixed yaw/pitch/roll = tgt_yaw/pitch/roll'),
    0x124: ('tgt_rider_slot', 'i', 'FUN_001722e8'),
    0x128: ('tgt_off_x', 'f', 'FUN_001722e8: look-at point offset X (cm)'),
    0x12C: ('tgt_off_y', 'f', 'FUN_001722e8'),
    0x130: ('tgt_off_z', 'f', 'FUN_001722e8'),
    0x134: ('tgt_yaw', 'f', 'FUN_001722e8: added to look yaw (rad)'),
    0x138: ('tgt_pitch', 'f', 'FUN_001722e8: added to look pitch (rad)'),
    0x13C: ('tgt_roll', 'f', 'FUN_001722e8: added to roll (rad)'),
    0x140: ('fov', 'f', 'FUN_001722e8 -> view+0x18; FUN_0017f390 half-angle = fov/2 clamped [0.0548,1.3963]'),
    0x144: ('angle_smooth_div', 'f', 'FUN_001722e8 -> view+0x48 (min 1); FUN_00174238 yaw/pitch/fov divisor'),
    0x148: ('roll_div', 'f', 'FUN_001722e8: roll = slope_angle / roll_div'),
    0x14C: ('roll_follow_slope', 'i', 'FUN_001722e8: roll from ground_normal (+0x2A0) in Ground/Static'),
    0x150: ('tgt_use_bone', 'i', 'FUN_001722e8'),
    0x154: ('tgt_bone_index', 'i', 'FUN_001722e8: -1 skeleton root(+0x4498) / -2 anchor(+0x4A90, z-adjusted) / n'),
    0x158: ('auto_fov', 'i', 'FUN_001722e8: fov = auto_fov_ref/(dist*fov) clamped [0.25,2]'),
    0x15C: ('auto_fov_ref', 'f', 'FUN_001722e8'),
    0x160: ('tgt_reverse', 'i', 'FUN_001722e8: pitch=-pitch, yaw+=pi'),
    0x164: ('rt_tgt_self_ptr', 'i', 'runtime (FUN_0016ddb8 writes)'),
    0x168: ('near_clip', 'f', 'FUN_00174c58 -> cam+0x448 -> render slot+0x104 (min 1)'),
    0x16C: ('unk_16c', 'f', 'consumer TODO'),
    0x170: ('static_cut', 'i', 'FUN_00174c98: !=0 -> camera evaluated 3 frames then frozen (cam+0xC==3)'),
}

# Camera region (type 4). Base (0x000..0x11C) shares the camera position layout (centre = base_xyz,
# stage_area @0x190 instead of 0x030).
REGION_FIELDS = {
    0x03C: ('centre_x', 'f', 'FUN_00171610'), 0x068: ('centre_y', 'f', 'FUN_00171610'),
    0x094: ('centre_z', 'f', 'FUN_00171610'),
    0x120: ('script_on_enter', 's15', 'vfunc_02 0x177078 (used if on_enter)'),
    0x12F: ('script_on_exit', 's15', 'vfunc_02 (used if on_exit)'),
    0x13E: ('script_while_inside', 's15', 'vfunc_02 (used if while_inside)'),
    0x150: ('radius', 'f', 'FUN_00171610'),
    0x154: ('rider_slot', 'i', 'FUN_00171610'),
    0x158: ('on_exit', 'i', 'vfunc_02 + FUN_00171e28(k=1)'),
    0x15C: ('on_enter', 'i', 'vfunc_02 + FUN_00171e28(k=0)'),
    0x160: ('while_inside', 'i', 'vfunc_02 + FUN_00171e28(k=2)'),
    0x164: ('active_in_race', 'i', 'vfunc_02 (DAT_00337774==0)'),
    0x168: ('active_in_replay', 'i', 'vfunc_02 (replay and cam mode 7)'),
    0x16C: ('only_unfinished', 'i', 'vfunc_02: skip if rider finish_result (+0x418) >= 0'),
    0x170: ('needs_lap', 'h', 'vfunc_02: skip if rider+0x114 == 0'),
    0x172: ('needs_cond', 'h', 'vfunc_02: skip unless FUN_0011f488() != 0 (TODO what it tests)'),
    0x174: ('capsule', 'i', 'FUN_00171610: capsule instead of sphere'),
    0x178: ('capsule_len', 'f', 'FUN_00171610'),
    0x17C: ('capsule_yaw', 'f', 'FUN_00171610'),
    0x180: ('capsule_pitch', 'f', 'FUN_00171610'),
    0x184: ('need_big_air', 'i', 'FUN_00171500: pred_time_left>0.33 and landing pt farther than min_air_dist'),
    0x188: ('min_air_dist', 'f', 'FUN_00171500'),
    0x18C: ('need_wipeout', 'i', 'FUN_00171500: rider motion 5/6'),
    0x190: ('stage_area', 'i', 'FUN_00171610/001777d0'),
}

# Director script (type 2): 20 events of 0x2C, i32 count @0x370 (FUN_00175f50)
OPCODES = {
    0: 'CUT_TO', 1: 'BLEND_TO', 2: 'ORBIT_BLEND_TO', 3: 'ORBIT_BLEND_TO_LONG', 4: 'PATH',
    5: 'SET_MODE_CHASE_NEAR', 6: 'SET_MODE_CHASE_FAR', 7: 'RETURN_TO_GAME_CAM', 8: 'HOLD_ON',
    9: 'HOLD_OFF', 10: 'RANDOM_GOTO', 11: 'GOSUB', 12: 'CUT_IF_NOT_HOLD', 13: 'MARK_CUT',
    14: 'ANIMATION', 15: 'STOP_ANIMATIONS', 0x11: 'AUDIO_214E08', 0x12: 'AUDIO_214E00',
    0x16: 'SET_420_424', 0x17: 'FLASH', 0x18: 'ROLL_FOLLOW_ON', 0x19: 'ROLL_FOLLOW_OFF',
    0x1B: 'WAIT_LAP', 0x1C: 'BLEND_TO_B', 0x1E: 'DISABLE_REGIONS', 0x1F: 'WAIT_ANIM_END', 0x20: 'END',
    0x21: 'STREAM_PLAY', 0x22: 'STREAM_STOP', 0x25: 'SPEECH_CameraScript_%d', 0x26: 'RACEMGR_4C_ON',
    0x27: 'RACEMGR_4C_OFF', 0x28: 'WAIT_AUDIO', 0x29: 'AUDIO_RIDER_0', 0x2A: 'AUDIO_RIDER_2',
    0x2B: 'AUDIO_RIDER_1',
}
EASE = {0: 'linear', 1: 'hermite(0,1,0,1)', 2: 'hermite(0,1,0,0)=smoothstep', 3: 'hermite(0,1,1,0)'}


def u32(d, o): return struct.unpack_from('<I', d, o)[0]
def i32(d, o): return struct.unpack_from('<i', d, o)[0]
def f32(d, o): return struct.unpack_from('<f', d, o)[0]
def cstr(d, o, n): return d[o:o + n].split(b'\0')[0].decode('latin-1')


def records(d):
    h = struct.unpack_from('<8i', d, 0)
    out = []
    for t in range(6):
        o = h[t + 1]
        while o != -1:
            nxt, nxt2, data, hsh = struct.unpack_from('<3iI', d, o)
            out.append(dict(type=t, off=o, data=data, hash=hsh, name=cstr(d, o + 16, 16)))
            o = nxt
    return out


def field_val(d, o, kind):
    w = u32(d, o)
    if w == DEAD: return None
    if kind == 'i': return i32(d, o)
    if kind == 'h': return struct.unpack_from('<h', d, o)[0]
    if kind == 's15': return cstr(d, o, 15)
    return round(f32(d, o), 6)


def decode_fields(d, base, table, size):
    res = {}
    for o, (nm, kind, _src) in sorted(table.items()):
        v = field_val(d, base + o, kind)
        if v is not None: res[nm] = v
    known = set()
    for o, (nm, kind, _) in table.items():
        known.update(range(o, o + (15 if kind == 's15' else 2 if kind == 'h' else 4)))
    for o in range(0, size, 4):
        if o in known: continue
        w = u32(d, base + o)
        if w in (DEAD, 0): continue
        res['unk_%03x' % o] = round(f32(d, base + o), 6) if 0x30000000 < (w & 0x7fffffff) < 0x50000000 else i32(d, base + o)
    return res


def decode_script(d, base):
    n = i32(d, base + 0x370)
    ev = []
    for k in range(min(n, 20)):
        e = base + k * 0x2C
        op = i32(d, e + 0x10)
        nm = '' if u32(d, e) == DEAD else cstr(d, e, 16)
        opt = lambda o, fn: None if u32(d, o) == DEAD else fn(d, o)
        x = dict(i=k, op=OPCODES.get(op, 'OP_%d' % op), opcode=op, name=nm,
                 a=opt(e + 0x14, i32), b=opt(e + 0x18, i32), c=opt(e + 0x1C, lambda dd, oo: round(f32(dd, oo), 4)),
                 t0=round(f32(d, e + 0x20), 4), t1=round(f32(d, e + 0x24), 4), arg=opt(e + 0x28, i32))
        if op in (1, 2, 3, 0x1C): x['ease'] = EASE.get(x['a'], x['a'])
        ev.append(x)
    return dict(count=n, events=ev)


def decode_path(d, base):
    # CamPath: up to 20 control-point camera names of 15 bytes, i32 count @0x12C, f32 @0x130 (meaning TODO;
    # 4000.0 in MESABLAN). Spline = Hermite through the named cameras' positions (FUN_00170610).
    n = i32(d, base + 0x12C)
    names = [cstr(d, base + 15 * k, 15) for k in range(min(n, 20))]
    return dict(count=n, points=names, unk_130=round(f32(d, base + 0x130), 4))


def decode(fn, want_type=None, want_name=None):
    d = open(fn, 'rb').read()
    out = []
    for r in records(d):
        if want_type is not None and r['type'] != want_type: continue
        if want_name and r['name'] != want_name: continue
        t, b = r['type'], r['data']
        if t == 0: p = decode_fields(d, b, CAMERA_FIELDS, DATA_SIZE[0])
        elif t == 4:
            tbl = dict(CAMERA_FIELDS); [tbl.pop(k) for k in list(tbl) if k >= 0x120 or k == 0x030]
            tbl.update(REGION_FIELDS); p = decode_fields(d, b, tbl, DATA_SIZE[4])
        elif t == 2: p = decode_script(d, b)
        elif t == 3: p = decode_path(d, b)
        else: p = {'raw': [hex(u32(d, b + o)) for o in range(0, DATA_SIZE[t], 4)]}
        out.append(dict(type=TYPE_NAMES[t], name=r['name'], off=hex(r['off']), payload=p))
    return out


if __name__ == '__main__':
    args = sys.argv[1:]
    t = int(args[args.index('--type') + 1]) if '--type' in args else None
    nm = args[args.index('--name') + 1] if '--name' in args else None
    files = [a for a in args if a.upper().endswith('.CML')]
    for fn in files:
        recs = decode(fn, t, nm)
        if '--json' in args:
            print(json.dumps(recs, indent=1)); continue
        for r in recs:
            print('== %s %s @%s' % (r['type'], r['name'], r['off']))
            p = r['payload']
            if r['type'] == 'DirectorScript':
                for e in p['events']:
                    print('   %2d %-22s %-16s a=%s b=%s c=%s t=[%g,%g] arg=%s%s' % (
                        e['i'], e['op'], e['name'], e['a'], e['b'], e['c'], e['t0'], e['t1'], e['arg'],
                        (' ease=' + str(e['ease'])) if 'ease' in e else ''))
            else:
                print('   ' + ', '.join('%s=%s' % kv for kv in p.items()))
