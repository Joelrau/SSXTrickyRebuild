"""Facial animation and secondary (hair / accessory) motion: reference model.

Clean-room: behaviour only. Game space: Z up, cm, row-vector 4x4 matrices (v' = v M, row 3 = translation) like the RAM palettes.

What the original does (summary):
  * every model part file with bones or morph keys gets its own pose object (cBoarder+0x7C0 + 0x394 i); the
    animation controller hands every playing clip's curve block whose channel byte equals the pose object's
    part (file id) to that pose object as a layer (0x15F7C8), so the head parts (eyes 5, goggles 6 / 9, face 10,
    hair / ponytails / backpack / strap) are driven by the same .afl clips as the body - there is no spring or
    jiggle simulation anywhere;
  * bone parts: the same sampler / blend as the body (0x202B00); the bones are posed by plain FK from the bone
    of the parent part they hang on (0x206818: world = local(euler, trans) x parent part palette[parent index]),
    run from the render path (0x208400) after the body palette exists. Parts no clip reaches stay at their
    (rider-scaled) bind pose relative to the head every frame (0x202868 resets the pose each frame);
  * the face (part 10, no bones, 23 morph keys): 0x2029D8 sums weight x track value per key over the layers;
    the draw (cPS2GraphicsMan vfunc 0x2E4 = 0x1DF478) applies key 0 always and keys 1..22 only when their weight
    is > 0.01, each as pos += s8 delta / 32768 x (weight x rider scale x 2560) on VU1 (program at 0x358 of the
    rider VU image 0x318080) before skinning; with no face layer this frame (+0x288 == 0) the base mesh is drawn;
  * in a race the only clips with head-part blocks are the channel-2 "character idles" 0x2F0..0x2F8
    (<xx>2anim.afl: hair / ponytails / backpack only) and one Zoe boardercross uber (face); the selector
    0x11D4E8 picks the idle from the rider's velocity in its rendered body frame every 18th race frame;
  * in the front end the feanim.afl clips (FEINIT / TRANS1-4 / CYCLE) carry eyes, goggles, face and hair blocks:
    blinks, expressions and the lip movement of the spoken TRANS line are authored in those clips (the speech
    event only plays the sound; no viseme / amplitude data drives the face).
"""
from __future__ import annotations

import math
import os
import struct
import sys

import numpy as np

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(HERE)
sys.path.insert(0, os.path.join(ROOT, "scripts"))

PART_BODY, PART_BOARD, PART_HEAD, PART_EYES, PART_GOGGLES, PART_GOGGLES2, PART_FACE, PART_BACKPACK = 0, 1, 2, 5, 6, 9, 10, 12
FACE_KEYS = 23

IDLE_STILL, IDLE_BWD, IDLE_BWDFAST, IDLE_DOWN, IDLE_FWD, IDLE_FWDFAST, IDLE_LEFT, IDLE_RIGHT, IDLE_UP = range(0x2F0, 0x2F9)
CHANNEL_IDLE = 2
SELECT_PERIOD = 18                       # race_state+0x18 % 18 == 0
CONTROL_VOID, CONTROL_RESETWAIT = 1, 0x16
# float32 constants of the selector (cm/s): 5, 30, 22 and 15 km/h
V_STILL = struct.unpack("<f", struct.pack("<I", 0x430AE38E))[0]      # 138.8889
V_FAST = struct.unpack("<f", struct.pack("<I", 0x44505555))[0]       # 833.3333
V_VERTICAL = struct.unpack("<f", struct.pack("<I", 0x4418C71C))[0]   # 611.1111
V_SIDE = struct.unpack("<f", struct.pack("<I", 0x43D05555))[0]       # 416.6667
SIT_IDS = (0, 0x1FE, 0x1FD, 0x1FC)       # channel-0 ids of the sitting / gate placement (start line)


def f32(x: float) -> float:
    return struct.unpack("<f", struct.pack("<f", x))[0]


def dot4(a, b) -> float:
    """VU dot of 0x11D4E8: vmul.xyzw then x + y + z + w (all four components; w is 0 in the data)."""
    return f32(f32(f32(f32(a[0] * b[0]) + f32(a[1] * b[1])) + f32(a[2] * b[2])) + f32(a[3] * b[3]))


def hair_idle_due(race_frame: int, control: int) -> bool:
    """cBoarder vfunc_08 calls the selector unless control is ResetWait (0x16); the selector returns at once for
    control Void (1) and acts only when the race frame counter is a multiple of 18."""
    return control not in (CONTROL_VOID, CONTROL_RESETWAIT) and race_frame % SELECT_PERIOD == 0


def hair_idle_id(v, x_row, y_row, z_row, character: int, channel0_id: int) -> int:
    """0x11D4E8: idle id from the velocity +0x150 and the rendered body matrix rows +0x4AA0 (X right),
    +0x4AB0 (Y forward), +0x4AC0 (Z up), all 4-vectors."""
    speed = f32(math.sqrt(dot4(v, v)))
    if speed < V_STILL:
        if character in (2, 11) and channel0_id in SIT_IDS:
            return IDLE_FWD
        return IDLE_STILL
    fwd = dot4(v, y_row)
    if speed < V_FAST:
        return IDLE_BWD if fwd < 0.0 else IDLE_FWD
    side = dot4(v, x_row)
    up = dot4(v, z_row)
    if abs(side) < abs(up) and V_VERTICAL < abs(up):
        return IDLE_UP if up < 0.0 else IDLE_DOWN
    if V_SIDE < abs(side):
        return IDLE_LEFT if side < 0.0 else IDLE_RIGHT
    return IDLE_BWDFAST if fwd < 0.0 else IDLE_FWDFAST


class HairIdleSelector:
    """Per-rider state: the channel-2 current id (controller +0x10). Play(id, 0, mode 5) only when the id differs;
    an id that resolves to no clip (lookup 0x585) leaves the playing instance alone and sets current = 0x300."""

    def __init__(self, resolves):
        self.current = 0x300
        self.resolves = resolves        # callable(id) -> bool (the per-character lookup has a clip)
        self.plays = []

    def step(self, race_frame, control, v, x_row, y_row, z_row, character, channel0_id):
        if not hair_idle_due(race_frame, control):
            return None
        want = hair_idle_id(v, x_row, y_row, z_row, character, channel0_id)
        if want == self.current:
            return None
        self.current = want if self.resolves(want) else 0x300
        if self.current != 0x300:
            self.plays.append((race_frame, want))
        return want


def euler_rows(e) -> np.ndarray:
    """Row-vector 3x3 of the local rotation Rz(-z) Ry(-y) Rx(-x) (column form) = its transpose."""
    import afl_curves as C
    return C.euler_matrix(e).T


def local_rows(e, t) -> np.ndarray:
    m = np.eye(4)
    m[:3, :3] = euler_rows(e)
    m[3, :3] = t
    return m


def bind_pose(bones, scale: float):
    """0x202868 with no layer: Euler = bind Euler, translation = bind translation (rider-scaled at load by
    0x2054F0)."""
    e = np.array([b.rotation for b in bones], float)
    t = np.array([np.asarray(b.translation, float) * scale for b in bones])
    return e, t


def part_pose(layers, bones, scale: float, shift: int):
    """0x202B00 for a bone part. layers: [(block, frame, weight, mirror, yaw, mask)]. Starts from the bind pose;
    pass 0 = masked layers, pass 1 = full layers, a layer touches bone i when mask bit (i + shift) is set;
    animated translations x scale (pose +0x380), the rest keeps the scaled bind."""
    import afl_curves as C
    nb = len(bones)
    e0, t0 = bind_pose(bones, scale)
    acc = [dict(w=np.zeros(nb), r=np.zeros((nb, 3)), t=np.zeros((nb, 3))) for _ in range(2)]
    for p in range(2):
        for blk, fr, w, mir, yaw, mask in layers:
            if (p == 0) == (mask == 0xFFFFFFFF):
                continue
            e, tr = C.sample_block(blk, bones, fr, mirror=mir, yaw=yaw, trans_scale=scale)
            for i, b in enumerate(bones):
                if not (b.unknown & 1):
                    tr[i] = t0[i]
            A = acc[p]
            for i in range(nb):
                if not (mask >> ((i + shift) & 31)) & 1 or A["w"][i] == 1.0:
                    continue
                if A["w"][i] == 0.0:
                    A["w"][i], A["t"][i], A["r"][i] = w, tr[i], e[i]
                else:
                    W = A["w"][i] + w
                    fo, fn = A["w"][i] / W, w / W
                    A["w"][i] = W
                    A["t"][i] = A["t"][i] * fo + tr[i] * fn
                    A["r"][i] = C.euler_blend(A["r"][i], e[i], fn)
    er, et = e0.copy(), t0.copy()
    for i in range(nb):
        w0, w1 = acc[0]["w"][i], acc[1]["w"][i]
        if w0 == 1.0:
            er[i], et[i] = acc[0]["r"][i], acc[0]["t"][i]
        elif w0 == 0.0:
            if w1 > 0.0:
                er[i], et[i] = acc[1]["r"][i], acc[1]["t"][i]
        else:
            base_r, base_t = (acc[1]["r"][i], acc[1]["t"][i]) if w1 > 0.0 else (e0[i], t0[i])
            et[i] = base_t * (1 - w0) + acc[0]["t"][i] * w0
            er[i] = C.euler_blend(base_r, acc[0]["r"][i], w0)
    return er, et


def attach_fk(bones, euler, trans, palettes: dict, file_id: int) -> list:
    """0x206818: in bone order, world_i = local(euler_i, trans_i) x palette[parent_file][parent_index]. A bone may
    hang on an earlier bone of its own part (Luther's / Marisol's second hair bone)."""
    own = []
    palettes = dict(palettes)
    palettes[file_id] = own
    for i, b in enumerate(bones):
        parent = palettes[b.parent_file][b.parent_index]
        own.append(local_rows(euler[i], trans[i]) @ parent)
    return own


MORPH_THRESHOLD = 0.01
MORPH_VU_SCALE = 2560.0                  # second float of the per-key V2-32 unpack to VU address 0x3FF
MORPH_ITOF = 32768.0                     # ITOF15 of the s8 delta


def face_weights(layers, n_keys: int = FACE_KEYS):
    """0x2029D8: weight[k] = sum over layers of layer weight x track k (frame); no normalisation, no masks.
    layers: [(block, frame, weight)]. Returns (weights, sampled flag +0x288)."""
    import afl_curves as C
    w = np.zeros(n_keys)
    for blk, fr, lw in layers:
        for k in range(n_keys):
            w[k] += lw * C.eval_track(blk.track(k), fr)
    return w, len(layers) > 0


def key_applied(k: int, w: float) -> bool:
    """0x1DF478: key 0 always (any sign), the others only above 0.01 (negative weights are dropped)."""
    return k == 0 or w > MORPH_THRESHOLD


def morph_displacement(deltas_s8: np.ndarray, weights, scale: float, sampled: bool) -> np.ndarray:
    """Model-space vertex offset (cm, before skinning) of one face block: deltas_s8[k, v, 3]."""
    out = np.zeros(deltas_s8.shape[1:], float)
    if not sampled:
        return out
    for k, w in enumerate(weights):
        if key_applied(k, w):
            out += deltas_s8[k] / MORPH_ITOF * (w * scale * MORPH_VU_SCALE)
    return out


# Key meaning from the geometry (centroid / mean direction of the moved vertices, all riders alike)
FACE_KEY_NAMES = ["jaw_open", "mouth_1", "mouth_2", "mouth_3", "mouth_4", "smile_l", "smile_r", "corner_l",
                  "corner_r", "lips_press", "upper_lip", "lower_lip", "mouth_shift_l", "mouth_shift_r", "blink_l",
                  "blink_r", "brow_mid", "brow_up_l", "brow_up_r", "brow_down_l", "brow_down_r", "brow_in_l",
                  "brow_in_r"]


def _self_test():
    v = [1400.0, 0.0, 0.0, 0.0]
    X, Y, Z = [0, 1, 0, 0], [1, 0, 0, 0], [0, 0, 1, 0]
    assert hair_idle_id(v, X, Y, Z, 0, 0x1FF) == IDLE_FWDFAST
    assert hair_idle_id([-1400, 0, 0, 0], X, Y, Z, 0, 0x1FF) == IDLE_BWDFAST
    assert hair_idle_id([900, 500, 0, 0], X, Y, Z, 0, 0x1FF) == IDLE_RIGHT
    assert hair_idle_id([900, 0, -700, 0], X, Y, Z, 0, 0x1FF) == IDLE_UP
    assert hair_idle_id([500, 0, 0, 0], X, Y, Z, 0, 0x1FF) == IDLE_FWD
    assert hair_idle_id([0, 0, 0, 0], X, Y, Z, 2, 0x1FC) == IDLE_FWD
    assert hair_idle_id([0, 0, 0, 0], X, Y, Z, 0, 0x1FC) == IDLE_STILL
    assert not hair_idle_due(36, 1) and hair_idle_due(36, 3) and not hair_idle_due(37, 3)
    assert key_applied(0, -0.3) and not key_applied(5, 0.01) and key_applied(5, 0.0101)
    print("face_secondary self-test OK")


if __name__ == "__main__":
    _self_test()
